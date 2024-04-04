package main

import (
	"bytes"
	"database/sql"
	_ "embed"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"html/template"
	"io"
	"io/ioutil"
	"log"
	"math/rand"
	"mime"
	"net"
	"net/http"
	_ "net/http/pprof"
	"net/url"
	"os"
	"os/signal"
	"path"
	"path/filepath"
	"regexp"
	"runtime/debug"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"

	_ "github.com/mattn/go-sqlite3"
)

const zeroIPString = "x'00000000'"

type namedTemplate struct {
	name string
	body string
}

func parseTemplates(ts ...namedTemplate) (tmpl *template.Template) {
	for _, t := range ts {
		if tmpl == nil {
			tmpl = template.New(t.name)
			if _, err := tmpl.Parse(t.body); err != nil {
				panic(err)
			}
		} else {
			other := tmpl.New(t.name)
			if _, err := other.Parse(t.body); err != nil {
				panic(err)
			}
		}
	}
	return tmpl
}

type cdcState struct {
	ip1      [4]byte
	port1    uint16
	ip2      [4]byte
	port2    uint16
	lastSeen msgs.EggsTime
}

type state struct {
	// we need the mutex since sqlite doesn't like concurrent writes
	mutex    sync.Mutex
	db       *sql.DB
	counters map[msgs.ShuckleMessageKind]*lib.Timings
	config   *shuckleConfig
	client   *client.Client
	// separate mutex to manage `lastAutoDecom`
	decomMutex    sync.Mutex
	lastAutoDecom time.Time
}

func (s *state) selectCDC() (*cdcState, error) {
	var cdc cdcState
	rows, err := s.db.Query("SELECT ip1, port1, ip2, port2, last_seen FROM cdc WHERE is_leader = true")
	if err != nil {
		return nil, fmt.Errorf("error selecting cdc: %s", err)
	}
	defer rows.Close()

	i := 0
	for rows.Next() {
		if i > 0 {
			return nil, fmt.Errorf("more than 1 cdc row returned from db")
		}
		var ip1, ip2 []byte
		err = rows.Scan(&ip1, &cdc.port1, &ip2, &cdc.port2, &cdc.lastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding blockService row: %s", err)
		}
		copy(cdc.ip1[:], ip1)
		copy(cdc.ip2[:], ip2)
		i += 1
	}
	return &cdc, nil
}

func (s *state) selectShards() (*[256]msgs.ShardInfo, error) {
	var ret [256]msgs.ShardInfo

	rows, err := s.db.Query("SELECT * FROM shards WHERE is_leader = true")
	if err != nil {
		return nil, fmt.Errorf("error selecting shards: %s", err)
	}
	defer rows.Close()

	i := 0
	for rows.Next() {
		if i > 255 {
			return nil, fmt.Errorf("the number of shards returned exceeded 256")
		}

		si := msgs.ShardInfo{}
		var ip1, ip2 []byte
		var id int
		var replicaId int
		var isLeader bool
		err = rows.Scan(&id, &replicaId, &isLeader, &ip1, &si.Port1, &ip2, &si.Port2, &si.LastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding shard row: %s", err)
		}
		copy(si.Ip1[:], ip1)
		copy(si.Ip2[:], ip2)
		ret[id] = si
		i += 1
	}

	return &ret, nil
}

func (s *state) selectShard(shid msgs.ShardId) (*msgs.ShardInfo, error) {
	n := sql.Named

	rows, err := s.db.Query("SELECT * FROM shards WHERE is_leader = true AND id = :id", n("id", shid))
	if err != nil {
		return nil, fmt.Errorf("error selecting shards: %s", err)
	}
	defer rows.Close()

	for rows.Next() {
		si := msgs.ShardInfo{}
		var ip1, ip2 []byte
		var id int
		var replicaId int
		var isLeader bool
		err = rows.Scan(&id, &replicaId, &isLeader, &ip1, &si.Port1, &ip2, &si.Port2, &si.LastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding shard row: %s", err)
		}
		copy(si.Ip1[:], ip1)
		copy(si.Ip2[:], ip2)
		return &si, nil
	}

	// can only happen at the very beginning of a deployment
	return &msgs.ShardInfo{}, nil
}

// if flagsFilter&flags is non-zero, things won't be returned
func (s *state) selectBlockServices(id *msgs.BlockServiceId, flagsFilter msgs.BlockServiceFlags) (map[msgs.BlockServiceId]msgs.BlockServiceInfo, error) {
	n := sql.Named

	ret := make(map[msgs.BlockServiceId]msgs.BlockServiceInfo)
	q := "SELECT * FROM block_services WHERE (flags & :flags) = 0"
	args := []any{n("flags", flagsFilter)}
	if id != nil {
		q += " AND id = :id"
		args = append(args, n("id", *id))
	}
	rows, err := s.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("error selecting blockServices: %s", err)
	}
	defer rows.Close()

	for rows.Next() {
		bs := msgs.RegisterBlockServiceInfo{}
		var hasFiles bool
		var ip1, ip2, fd, sk []byte
		err = rows.Scan(&bs.Id, &ip1, &bs.Port1, &ip2, &bs.Port2, &bs.StorageClass, &fd, &sk, &bs.Flags, &bs.CapacityBytes, &bs.AvailableBytes, &bs.Blocks, &bs.Path, &bs.LastSeen, &hasFiles)
		if err != nil {
			return nil, fmt.Errorf("error decoding blockService row: %s", err)
		}

		copy(bs.Ip1[:], ip1)
		copy(bs.Ip2[:], ip2)
		copy(bs.FailureDomain.Name[:], fd)
		copy(bs.SecretKey[:], sk)
		ret[bs.Id] = msgs.BlockServiceInfo{Info: bs, HasFiles: hasFiles}
	}
	return ret, nil
}

type shuckleConfig struct {
	blockServiceMinFreeBytes uint64
	ip1                      [4]byte
	port1                    uint16
	ip2                      [4]byte
	port2                    uint16
	minAutoDecomInterval     time.Duration
	maxDecommedWithFiles     int
}

func newState(
	log *lib.Logger,
	db *sql.DB,
	conf *shuckleConfig,
) (*state, error) {
	st := &state{
		db:     db,
		mutex:  sync.Mutex{},
		config: conf,
	}
	st.counters = make(map[msgs.ShuckleMessageKind]*lib.Timings)
	for _, k := range msgs.AllShuckleMessageKind {
		st.counters[k] = lib.NewTimings(40, 10*time.Microsecond, 1.5)
	}
	var err error
	st.client, err = client.NewClientDirectNoAddrs(log)
	if err != nil {
		return nil, err
	}
	// Take a long time to fail requests so that we don't get spurious failures
	// on restarts
	{
		blockTimeout := client.DefaultBlockTimeout
		blockTimeout.Overall = 5 * time.Minute
		st.client.SetBlockTimeout(&blockTimeout)
		shardTimeout := client.DefaultShardTimeout
		shardTimeout.Overall = 5 * time.Minute
		st.client.SetShardTimeouts(&shardTimeout)
		cdcTimeout := client.DefaultCDCTimeout
		cdcTimeout.Overall = 5 * time.Minute
		st.client.SetCDCTimeouts(&cdcTimeout)
	}
	if err := refreshClient(log, st); err != nil {
		return nil, err
	}
	return st, err
}

func (st *state) resetTimings() {
	for _, t := range st.counters {
		t.Reset()
	}
}

func handleAllBlockServices(ll *lib.Logger, s *state, req *msgs.AllBlockServicesReq) (*msgs.AllBlockServicesResp, error) {
	resp := msgs.AllBlockServicesResp{}
	blockServices, err := s.selectBlockServices(nil, 0)
	if err != nil {
		ll.RaiseAlert("error reading block services: %s", err)
		return nil, err
	}

	resp.BlockServices = make([]msgs.BlockServiceInfo, len(blockServices))
	i := 0
	for _, bs := range blockServices {
		resp.BlockServices[i] = bs
		i++
	}

	return &resp, nil
}

func handleBlockService(log *lib.Logger, s *state, req *msgs.BlockServiceReq) (*msgs.BlockServiceResp, error) {
	blockServices, err := s.selectBlockServices(&req.Id, 0)
	if err != nil {
		return nil, err
	}
	if len(blockServices) > 1 {
		panic(fmt.Errorf("impossible: %v results for id blocks selection", len(blockServices)))
	}
	for _, bs := range blockServices {
		return &msgs.BlockServiceResp{Info: bs.Info}, nil
	}
	return nil, msgs.BLOCK_SERVICE_NOT_FOUND
}

func handleRegisterBlockServices(ll *lib.Logger, s *state, req *msgs.RegisterBlockServicesReq) (*msgs.RegisterBlockServicesResp, error) {
	if len(req.BlockServices) == 0 {
		return &msgs.RegisterBlockServicesResp{}, nil
	}

	now := msgs.Now()
	has_files := false
	var fmtBuilder strings.Builder
	fmtBuilder.Write([]byte(`
		INSERT INTO block_services
			(id, ip1, port1, ip2, port2, storage_class, failure_domain, secret_key, flags, capacity_bytes, available_bytes, blocks, path, last_seen, has_files)
		VALUES
	`))
	fmtValues := []byte("(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
	values := make([]any, len(req.BlockServices)*15) // 15: number of columns
	values = values[:0]
	for i := range req.BlockServices {
		bs := req.BlockServices[i]
		if bs.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED != 0 {
			return nil, msgs.CANNOT_REGISTER_DECOMMISSIONED
		}
		flags := bs.Flags
		// ll.Info("block service %s %v key: %s", string(bs.FailureDomain[:]), bs.Id, hex.EncodeToString(bs.SecretKey[:]))
		values = append(
			values,
			bs.Id, bs.Ip1[:], bs.Port1, bs.Ip2[:], bs.Port2,
			bs.StorageClass, bs.FailureDomain.Name[:],
			bs.SecretKey[:], flags,
			bs.CapacityBytes, bs.AvailableBytes, bs.Blocks,
			bs.Path, now, has_files,
		)
		if i > 0 {
			fmtBuilder.Write([]byte(", "))
		}
		fmtBuilder.Write(fmtValues)
	}
	// Never change the storage_class/failure_domain/secret_key/ip/path,
	// see <internal-repo/issues/89>, we might
	// relax the IP restriction in the future but let's be cautious now.
	// Do not update has_files as it is set outside of the registration.
	fmtBuilder.Write([]byte(`
		ON CONFLICT DO UPDATE SET
			flags = (flags & ~?) | excluded.flags,
			capacity_bytes = excluded.capacity_bytes,
			available_bytes = excluded.available_bytes,
			blocks = excluded.blocks,
			last_seen = excluded.last_seen
		WHERE
			ip1 = excluded.ip1 AND
			port1 = excluded.port1 AND
			ip2 = excluded.ip2 AND
			port2 = excluded.port2 AND
			storage_class = excluded.storage_class AND
			failure_domain = excluded.failure_domain AND
			path = excluded.path AND
			secret_key = excluded.secret_key
	`))
	values = append(values, msgs.EGGSFS_BLOCK_SERVICE_STALE)

	s.mutex.Lock()
	defer s.mutex.Unlock()

	// ll.Info("values: %+q", values)
	result, err := s.db.Exec(fmtBuilder.String(), values...)

	if err != nil {
		ll.RaiseAlert("error registering block services: %s", err)
		return nil, err
	}

	// check that all of them fired
	rowsAffected, err := result.RowsAffected()
	if err != nil {
		panic(err)
	}
	if int(rowsAffected) != len(req.BlockServices) {
		ll.Info("failed request: %+v", req)
		ll.RaiseAlert("could not update all block services (expected %v, updated %v), some of them probably have inconsistent info, check logs", len(req.BlockServices), rowsAffected)
	}

	return &msgs.RegisterBlockServicesResp{}, nil
}

func setBlockServiceFilePresence(ll *lib.Logger, s *state, bsId msgs.BlockServiceId, hasFiles bool) {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	res, err := s.db.Exec(
		"UPDATE block_services SET has_files = :has_files WHERE id = :id",
		n("has_files", hasFiles), n("id", bsId),
	)
	if err != nil {
		ll.RaiseAlert("error updating has_files for blockservice %d: %s", bsId, err)
	}
	nrows, err := res.RowsAffected()
	if err != nil {
		ll.RaiseAlert("error fetching the number of affected rows when setting has_files for %d: %s", bsId, err)
	}
	if nrows != 1 {
		ll.RaiseAlert("unexpected number of rows affected when setting has_files for %d, got:%d, want:1", bsId, nrows)
	}
}

func checkBlockServiceFilePresence(ll *lib.Logger, s *state) {
	sleepInterval := time.Minute
	blockServicesAlert := ll.NewNCAlert(0)
	blockServicesAlert.SetAppType(lib.XMON_DAYTIME)
	shids := make([]int, 256)
	rand.Seed(time.Now().UnixNano())

	for {
		blockServices, err := s.selectBlockServices(nil, 0)
		if err != nil {
			ll.RaiseNC(blockServicesAlert, "error reading block services, will try again in %v: %s", sleepInterval, err)
			time.Sleep(sleepInterval)
			continue
		}
		ll.ClearNC(blockServicesAlert)
		client := s.client
		for _, bs := range blockServices {
			// For each blockservice we check shards in random order, otherwise we may overload shard 0
			for i := 0; i < 256; i++ {
				shids[i] = i
			}
			rand.Shuffle(len(shids), func(i, j int) { shids[i], shids[j] = shids[j], shids[i] })

			hasFiles := false
			bsId := bs.Info.Id
			for _, i := range shids {
				shid := msgs.ShardId(i)
				filesReq := msgs.BlockServiceFilesReq{BlockServiceId: bsId}
				filesResp := msgs.BlockServiceFilesResp{}

				if err := client.ShardRequest(ll, shid, &filesReq, &filesResp); err != nil {
					ll.RaiseAlert("error while trying to get files for block service %v: %v", bsId, err)
					time.Sleep(sleepInterval)
					break
				}
				if len(filesResp.FileIds) > 0 {
					ll.Debug("found %d files for blockservice %v in shard %d (req %+v, resp %+v)", len(filesResp.FileIds), bsId, i, filesReq, filesResp)
					hasFiles = true
					break
				}
			}
			ll.Debug("setting hasFiles: %v for blockservice %v", hasFiles, bsId)
			setBlockServiceFilePresence(ll, s, bsId, hasFiles)
		}
		ll.Info("finished checking file presence for all blockservices, sleeping for %s", sleepInterval)
		time.Sleep(sleepInterval)
	}
}

func handleSetBlockserviceDecommissioned(ll *lib.Logger, s *state, req *msgs.SetBlockServiceDecommissionedReq) (*msgs.SetBlockServiceDecommissionedResp, error) {
	// This lock effectively prevents decom requests running in parallel
	// we don't want to take any chances here.
	s.decomMutex.Lock()
	defer s.decomMutex.Unlock()
	ld := time.Since(s.lastAutoDecom)
	if ld < s.config.minAutoDecomInterval {
		ll.RaiseAlert("rejecting automated decommissioning of blockservice %v: last decommissioned %v ago", req.Id, ld)
		return nil, msgs.AUTO_DECOMMISSION_FORBIDDEN
	}

	blockServices, err := s.selectBlockServices(nil, 0)
	if err != nil {
		return nil, err
	}

	decommedWithFiles := 0
	for _, bs := range blockServices {
		if bs.Info.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) && bs.HasFiles {
			decommedWithFiles = decommedWithFiles + 1
		}
		if decommedWithFiles >= s.config.maxDecommedWithFiles {
			ll.RaiseAlert("rejecting automated decommissioning of blockservice %v: at least %d blockservices already decommissioned, but still have files", req.Id, decommedWithFiles)
			return nil, msgs.AUTO_DECOMMISSION_FORBIDDEN
		}
	}

	r := msgs.SetBlockServiceFlagsReq{
		Id:        req.Id,
		Flags:     msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED,
		FlagsMask: uint8(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
	}
	if _, err := handleSetBlockServiceFlags(ll, s, &r); err != nil {
		return nil, err
	}

	s.lastAutoDecom = time.Now()
	return &msgs.SetBlockServiceDecommissionedResp{}, nil
}

func handleSetBlockServiceFlags(ll *lib.Logger, s *state, req *msgs.SetBlockServiceFlagsReq) (*msgs.SetBlockServiceFlagsResp, error) {
	// Special case: the DECOMMISSIONED flag can never be unset, we assume that in
	// a couple of cases (e.g. when fetching block services in shuckle)
	if (req.FlagsMask&uint8(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED)) != 0 && (req.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) == 0 {
		return nil, msgs.CANNOT_UNSET_DECOMMISSIONED
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	res, err := s.db.Exec(
		"UPDATE block_services SET flags = ((flags & ~:mask) | (:flags & :mask)) WHERE id = :id",
		n("flags", req.Flags), n("mask", req.FlagsMask), n("id", req.Id),
	)
	if err != nil {
		ll.RaiseAlert("error setting flags for blockservice %d: %s", req.Id, err)
		return nil, err
	}
	nrows, err := res.RowsAffected()
	if err != nil {
		ll.RaiseAlert("error fetching the number of affected rows when setting flags for %d: %s", req.Id, err)
		return nil, err
	}
	if nrows != 1 {
		ll.RaiseAlert("unexpected number of rows affected when setting flags for %d, got:%d, want:1", req.Id, nrows)
		return nil, err
	}
	return &msgs.SetBlockServiceFlagsResp{}, nil
}

func handleShards(ll *lib.Logger, s *state, req *msgs.ShardsReq) (*msgs.ShardsResp, error) {
	resp := msgs.ShardsResp{}

	shards, err := s.selectShards()
	if err != nil {
		ll.RaiseAlert("error reading shards: %s", err)
		return nil, err
	}

	resp.Shards = shards[:]
	return &resp, nil
}

func handleShardsWithReplicas(ll *lib.Logger, s *state, req *msgs.ShardsWithReplicasReq) (*msgs.ShardsWithReplicasResp, error) {
	ret := []msgs.ShardWithReplicasInfo{}

	rows, err := s.db.Query("SELECT * FROM shards")
	if err != nil {
		return nil, fmt.Errorf("error selecting shards: %s", err)
	}
	defer rows.Close()

	for rows.Next() {
		si := msgs.ShardWithReplicasInfo{}
		var ip1, ip2 []byte
		var id int
		var replicaId int
		err = rows.Scan(&id, &replicaId, &si.IsLeader, &ip1, &si.Port1, &ip2, &si.Port2, &si.LastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding shard row: %s", err)
		}
		if si.Port1 == 0 {
			continue
		}
		si.Id = msgs.MakeShardReplicaId(msgs.ShardId(id), msgs.ReplicaId(replicaId))
		copy(si.Ip1[:], ip1)
		copy(si.Ip2[:], ip2)
		ret = append(ret, si)
	}

	return &msgs.ShardsWithReplicasResp{Shards: ret}, nil
}

func handleShard(ll *lib.Logger, s *state, req *msgs.ShardReq) (*msgs.ShardResp, error) {
	info, err := s.selectShard(req.Id)
	if err != nil {
		return nil, err
	}
	return &msgs.ShardResp{Info: *info}, nil
}

func handleRegisterShard(ll *lib.Logger, s *state, req *msgs.RegisterShardReq) (*msgs.RegisterShardResp, error) {
	err := handleRegisterShardCommon(ll, s, &msgs.RegisterShardReplicaReq{msgs.MakeShardReplicaId(req.Id, 0), true, req.Info})
	if err != nil {
		return nil, err
	}
	if err := refreshClient(ll, s); err != nil {
		return nil, err
	}
	return &msgs.RegisterShardResp{}, err
}

func handleRegisterShardReplica(ll *lib.Logger, s *state, req *msgs.RegisterShardReplicaReq) (*msgs.RegisterShardReplicaResp, error) {
	err := handleRegisterShardCommon(ll, s, req)
	if err != nil {
		return nil, err
	}
	if err := refreshClient(ll, s); err != nil {
		return nil, err
	}
	return &msgs.RegisterShardReplicaResp{}, err
}

func handleRegisterShardCommon(ll *lib.Logger, s *state, req *msgs.RegisterShardReplicaReq) error {
	if req.Shrid.Replica() > 4 {
		return msgs.INVALID_REPLICA
	}
	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	res, err := s.db.Exec(`
		UPDATE shards
		SET ip1 = :ip1, port1 = :port1, ip2 = :ip2, port2 = :port2, last_seen = :last_seen
		WHERE
		id = :id AND replica_id = :replica_id AND is_leader = :is_leader AND
		(
			(ip1 = `+zeroIPString+` AND port1 = 0 AND ip2 = `+zeroIPString+` AND port2 = 0) OR
			(ip1 = :ip1 AND port1 = :port1 AND ip2 = :ip2 AND port2 = :port2)
		)
		`, n("id", req.Shrid.Shard()), n("replica_id", req.Shrid.Replica()), n("is_leader", req.IsLeader), n("ip1", req.Info.Ip1[:]), n("port1", req.Info.Port1), n("ip2", req.Info.Ip2[:]), n("port2", req.Info.Port2), n("last_seen", msgs.Now()),
	)
	if err != nil {
		ll.RaiseAlert("error registering shard %d: %s", req.Shrid, err)
		return err
	}

	rowsAffected, err := res.RowsAffected()
	if err != nil {
		ll.RaiseAlert("error registering shard %d: %s", req.Shrid, err)
		return err
	}
	if rowsAffected == 0 {
		return msgs.DIFFERENT_ADDRS_INFO
	}
	if rowsAffected > 1 {
		panic(fmt.Errorf("more than one row in shards with shardReplicaId %s", req.Shrid))
	}

	return nil
}

func handleShardReplicas(ll *lib.Logger, s *state, req *msgs.ShardReplicasReq) (*msgs.ShardReplicasResp, error) {
	var ret [5]msgs.AddrsInfo
	n := sql.Named
	rows, err := s.db.Query("SELECT * FROM shards WHERE last_seen IS NOT NULL AND id = :id", n("id", req.Id))
	if err != nil {
		return nil, fmt.Errorf("error selecting shard replicas: %s", err)
	}
	defer rows.Close()

	i := 0
	for rows.Next() {
		if i > 5 {
			return nil, fmt.Errorf("the number of shards returned exceeded 5")
		}

		si := msgs.AddrsInfo{}
		var ip1, ip2 []byte
		var id int
		var replicaId int
		var isLeader bool
		var lastSeen uint64
		err = rows.Scan(&id, &replicaId, &isLeader, &ip1, &si.Port1, &ip2, &si.Port2, &lastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding shard row: %s", err)
		}
		copy(si.Ip1[:], ip1)
		copy(si.Ip2[:], ip2)
		ret[replicaId] = si
		i += 1
	}

	return &msgs.ShardReplicasResp{Replicas: ret[:]}, nil
}

func handleCdc(log *lib.Logger, s *state, req *msgs.CdcReq) (*msgs.CdcResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	resp := msgs.CdcResp{}
	cdc, err := s.selectCDC()
	if err != nil {
		log.RaiseAlert("error reading cdc: %s", err)
		return nil, err
	}
	resp.Ip1 = cdc.ip1
	resp.Port1 = cdc.port1
	resp.Ip2 = cdc.ip2
	resp.Port2 = cdc.port2
	resp.LastSeen = cdc.lastSeen

	return &resp, nil
}

func handleCdcReplicas(log *lib.Logger, s *state, req *msgs.CdcReplicasReq) (*msgs.CdcReplicasResp, error) {
	var ret [5]msgs.AddrsInfo
	rows, err := s.db.Query("SELECT replica_id, ip1, port1, ip2, port2 FROM cdc")
	if err != nil {
		return nil, fmt.Errorf("error selecting cdc replicas: %s", err)
	}
	defer rows.Close()

	i := 0
	for rows.Next() {
		if i > 5 {
			return nil, fmt.Errorf("the number of cdc replicas returned exceeded 5")
		}

		si := msgs.AddrsInfo{}
		var ip1, ip2 []byte
		var replicaId int
		err = rows.Scan(&replicaId, &ip1, &si.Port1, &ip2, &si.Port2)
		if err != nil {
			return nil, fmt.Errorf("error decoding cdc row: %s", err)
		}
		copy(si.Ip1[:], ip1)
		copy(si.Ip2[:], ip2)
		ret[replicaId] = si
		i += 1
	}

	return &msgs.CdcReplicasResp{Replicas: ret[:]}, nil
}

func handleRegisterCDC(log *lib.Logger, s *state, req *msgs.RegisterCdcReq) (*msgs.RegisterCdcResp, error) {
	err := registerCDCReplicaCommon(log, s, &msgs.RegisterCdcReplicaReq{0, true, msgs.AddrsInfo{req.Ip1, req.Port1, req.Ip2, req.Port2}})
	if err != nil {
		log.RaiseAlert("error registering cdc: %s", err)
		return nil, err
	}
	if err := refreshClient(log, s); err != nil {
		return nil, err
	}
	return &msgs.RegisterCdcResp{}, nil
}

func handleRegisterCDCReplica(log *lib.Logger, s *state, req *msgs.RegisterCdcReplicaReq) (*msgs.RegisterCdcReplicaResp, error) {
	err := registerCDCReplicaCommon(log, s, req)
	if err != nil {
		log.RaiseAlert("error registering cdc replica: %s", err)
		return nil, err
	}
	if err := refreshClient(log, s); err != nil {
		return nil, err
	}
	return &msgs.RegisterCdcReplicaResp{}, nil
}

func registerCDCReplicaCommon(log *lib.Logger, s *state, req *msgs.RegisterCdcReplicaReq) error {
	if req.Replica > 4 {
		return msgs.INVALID_REPLICA
	}
	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	res, err := s.db.Exec(`
		UPDATE cdc
		SET ip1 = :ip1, port1 = :port1, ip2 = :ip2, port2 = :port2, last_seen = :last_seen
		WHERE replica_id = :replica_id AND is_leader = :is_leader AND
		(
			(ip1 = `+zeroIPString+` AND port1 = 0 AND ip2 = `+zeroIPString+` AND port2 = 0) OR
			(ip1 = :ip1 AND port1 = :port1 AND ip2 = :ip2 AND port2 = :port2)
		)`,
		n("replica_id", req.Replica),
		n("is_leader", req.IsLeader),
		n("ip1", req.Info.Ip1[:]), n("port1", req.Info.Port1),
		n("ip2", req.Info.Ip2[:]), n("port2", req.Info.Port2),
		n("last_seen", msgs.Now()),
	)
	if err != nil {
		return err
	}

	rowsAffected, err := res.RowsAffected()
	if err != nil {
		return err
	}
	if rowsAffected == 0 {
		return msgs.DIFFERENT_ADDRS_INFO
	}
	if rowsAffected > 1 {
		panic(fmt.Errorf("more than one row in cdc with replicaId %v", req.Replica))
	}
	return nil
}

func handleInfoReq(log *lib.Logger, s *state, req *msgs.InfoReq) (*msgs.InfoResp, error) {
	resp := msgs.InfoResp{}

	rows, err := s.db.Query(
		`
		SELECT
			count(*),
			count(distinct failure_domain),
			sum(CASE WHEN (flags&:decommissioned) = 0 THEN capacity_bytes ELSE 0 END),
			sum(CASE WHEN (flags&:decommissioned) = 0 THEN available_bytes ELSE 0 END),
			sum(CASE WHEN (flags&:decommissioned) = 0 THEN blocks ELSE 0 END)
		FROM block_services
		`,
		sql.Named("decommissioned", msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
	)
	if err != nil {
		log.RaiseAlert("error getting info: %s", err)
		return nil, err
	}
	defer rows.Close()
	if !rows.Next() {
		return nil, fmt.Errorf("no row found in info query")
	}
	if err := rows.Scan(&resp.NumBlockServices, &resp.NumFailureDomains, &resp.Capacity, &resp.Available, &resp.Blocks); err != nil {
		log.RaiseAlert("error scanning info: %s", err)
		return nil, err
	}

	return &resp, nil
}

func handleInsertStats(log *lib.Logger, s *state, req *msgs.InsertStatsReq) (*msgs.InsertStatsResp, error) {
	var fmtBuilder strings.Builder
	fmtBuilder.Write([]byte("INSERT INTO stats (name, time, value) VALUES "))
	values := make([]any, 0, len(req.Stats)*3) // 3: number of columns
	for i, s := range req.Stats {
		values = append(values, s.Name, s.Time, []byte(s.Value))
		if i > 0 {
			fmtBuilder.Write([]byte(", "))
		}
		fmtBuilder.Write([]byte("(?, ?, ?)"))
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()

	_, err := s.db.Exec(fmtBuilder.String(), values...)

	if err != nil {
		log.RaiseAlert("error inserting stats: %s", err)
		return nil, err
	}

	return &msgs.InsertStatsResp{}, nil
}

func handleShardBlockServices(log *lib.Logger, s *state, req *msgs.ShardBlockServicesReq) (*msgs.ShardBlockServicesResp, error) {
	// only select block services we're willing to write
	blockServices, err := s.selectBlockServices(nil, msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE|msgs.EGGSFS_BLOCK_SERVICE_STALE|msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED)
	if err != nil {
		log.RaiseAlert("error reading block services: %s", err)
		return nil, err
	}
	// The scheme below is a very cheap way to always pick different failure domains
	// for our block services: we just set the current block services to be all of
	// different failure domains, sharded by storage type.
	//
	// It does require having at least 14 failure domains (to do RS(10,4)), which is
	// easy right now since we have ~100 failure domains in iceland.
	//
	// It should eventually be replaced with something a bit cleverer, see #44.
	blockServicesByFailureDomain := make(map[msgs.StorageClass]map[msgs.FailureDomain][]msgs.BlockServiceId)
	for _, bs := range blockServices {
		if _, found := blockServicesByFailureDomain[bs.Info.StorageClass]; !found {
			blockServicesByFailureDomain[bs.Info.StorageClass] = make(map[msgs.FailureDomain][]msgs.BlockServiceId)
		}
		if _, found := blockServicesByFailureDomain[bs.Info.StorageClass][bs.Info.FailureDomain]; !found {
			blockServicesByFailureDomain[bs.Info.StorageClass][bs.Info.FailureDomain] = []msgs.BlockServiceId{}
		}
		if bs.Info.AvailableBytes < s.config.blockServiceMinFreeBytes { // not enough free bytes
			continue
		}
		blockServicesByFailureDomain[bs.Info.StorageClass][bs.Info.FailureDomain] = append(blockServicesByFailureDomain[bs.Info.StorageClass][bs.Info.FailureDomain], bs.Info.Id)
	}
	r := wyhash.New(rand.Uint64())
	resp := &msgs.ShardBlockServicesResp{
		BlockServices: []msgs.BlockServiceId{},
	}
	for _, byFailureDomain := range blockServicesByFailureDomain {
		for _, blockServices := range byFailureDomain {
			if len(blockServices) == 0 {
				continue
			}
			resp.BlockServices = append(resp.BlockServices, blockServices[r.Uint64()%uint64(len(blockServices))])
		}
	}
	if len(resp.BlockServices) < 14 { // we need at least as many to create files
		return nil, msgs.COULD_NOT_PICK_BLOCK_SERVICES
	}
	return resp, nil
}

func handleGetStats(log *lib.Logger, s *state, req *msgs.GetStatsReq) (*msgs.GetStatsResp, error) {
	n := sql.Named
	end := req.EndTime
	if end == 0 {
		end = msgs.EggsTime(^uint64(0) & ^(uint64(1) << 63)) // sqlite doesn't have full uint64
	}
	log.Debug("start time %v", req.StartTime)
	// the limit is due to the max list size in bincode (but probably a good thing anyhow)
	rowsLimit := 1 << 16
	rows, err := s.db.Query(
		"SELECT name, time, value FROM stats WHERE time >= :start AND time < :end ORDER BY time, name LIMIT :limit",
		n("start", req.StartTime), n("end", end), n("limit", rowsLimit),
	)
	if err != nil {
		panic(err)
	}
	defer rows.Close()
	resp := &msgs.GetStatsResp{}
	for rows.Next() {
		resp.Stats = append(resp.Stats, msgs.Stat{})
		stat := &resp.Stats[len(resp.Stats)-1]
		err = rows.Scan(&stat.Name, &stat.Time, &stat.Value)
		if err != nil {
			panic(err)
		}
	}
	if len(resp.Stats) == rowsLimit {
		lastStat := resp.Stats[len(resp.Stats)-1]
		resp.Stats = resp.Stats[:len(resp.Stats)-1]
		resp.NextName = lastStat.Name
		resp.NextTime = lastStat.Time
	}
	return resp, nil
}

func handleShuckle(log *lib.Logger, s *state) (*msgs.ShuckleResp, error) {
	resp := &msgs.ShuckleResp{
		Ip1:   s.config.ip1,
		Port1: s.config.port1,
		Ip2:   s.config.ip2,
		Port2: s.config.port2,
	}
	return resp, nil
}

func handleRequestParsed(log *lib.Logger, s *state, req msgs.ShuckleRequest) (msgs.ShuckleResponse, error) {
	t0 := time.Now()
	defer func() {
		s.counters[req.ShuckleRequestKind()].Add(time.Since(t0))
	}()
	log.Debug("handling request %T", req)
	log.Trace("request body %+v", req)
	var err error
	var resp msgs.ShuckleResponse
	switch whichReq := req.(type) {
	case *msgs.RegisterBlockServicesReq:
		resp, err = handleRegisterBlockServices(log, s, whichReq)
	case *msgs.SetBlockServiceFlagsReq:
		resp, err = handleSetBlockServiceFlags(log, s, whichReq)
	case *msgs.SetBlockServiceDecommissionedReq:
		resp, err = handleSetBlockserviceDecommissioned(log, s, whichReq)
	case *msgs.ShardsReq:
		resp, err = handleShards(log, s, whichReq)
	case *msgs.ShardsWithReplicasReq:
		resp, err = handleShardsWithReplicas(log, s, whichReq)
	case *msgs.RegisterShardReq:
		resp, err = handleRegisterShard(log, s, whichReq)
	case *msgs.RegisterShardReplicaReq:
		resp, err = handleRegisterShardReplica(log, s, whichReq)
	case *msgs.ShardReplicasReq:
		resp, err = handleShardReplicas(log, s, whichReq)
	case *msgs.AllBlockServicesReq:
		resp, err = handleAllBlockServices(log, s, whichReq)
	case *msgs.CdcReq:
		resp, err = handleCdc(log, s, whichReq)
	case *msgs.CdcReplicasReq:
		resp, err = handleCdcReplicas(log, s, whichReq)
	case *msgs.RegisterCdcReq:
		resp, err = handleRegisterCDC(log, s, whichReq)
	case *msgs.RegisterCdcReplicaReq:
		resp, err = handleRegisterCDCReplica(log, s, whichReq)
	case *msgs.InfoReq:
		resp, err = handleInfoReq(log, s, whichReq)
	case *msgs.BlockServiceReq:
		resp, err = handleBlockService(log, s, whichReq)
	case *msgs.InsertStatsReq:
		resp, err = handleInsertStats(log, s, whichReq)
	case *msgs.ShardReq:
		resp, err = handleShard(log, s, whichReq)
	case *msgs.GetStatsReq:
		resp, err = handleGetStats(log, s, whichReq)
	case *msgs.ShuckleReq:
		resp, err = handleShuckle(log, s)
	case *msgs.ShardBlockServicesReq:
		resp, err = handleShardBlockServices(log, s, whichReq)
	default:
		err = fmt.Errorf("bad req type %T", req)
	}

	return resp, err
}

func isBenignConnTermination(err error) bool {
	// we don't currently use timeouts here, but can't hurt
	if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
		return true
	}

	if opErr, ok := err.(*net.OpError); ok {
		if sysErr, ok := opErr.Err.(*os.SyscallError); ok {
			if sysErr.Err == syscall.EPIPE {
				return true
			}
			if sysErr.Err == syscall.ECONNRESET {
				return true
			}
		}
	}

	return false
}

func writeShuckleResponse(log *lib.Logger, w io.Writer, resp msgs.ShuckleResponse) error {
	// serialize
	bytes := bincode.Pack(resp)
	// write out
	if err := binary.Write(w, binary.LittleEndian, msgs.SHUCKLE_RESP_PROTOCOL_VERSION); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, uint32(1+len(bytes))); err != nil {
		return err
	}
	if _, err := w.Write([]byte{uint8(resp.ShuckleResponseKind())}); err != nil {
		return err
	}
	if _, err := w.Write(bytes); err != nil {
		return err
	}
	return nil
}

func writeShuckleResponseError(log *lib.Logger, w io.Writer, err msgs.ErrCode) error {
	log.Debug("writing shuckle error %v", err)
	buf := bytes.NewBuffer([]byte{})
	if err := binary.Write(buf, binary.LittleEndian, msgs.SHUCKLE_RESP_PROTOCOL_VERSION); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, uint32(1+2)); err != nil {
		return err
	}
	if _, err := buf.Write([]byte{msgs.ERROR}); err != nil {
		return err
	}
	if err := binary.Write(buf, binary.LittleEndian, uint16(err)); err != nil {
		return err
	}
	w.Write(buf.Bytes())
	return nil
}

// returns whether the connection should be terminated
func handleError(
	log *lib.Logger,
	conn *net.TCPConn,
	err error,
) bool {
	if err == io.EOF {
		log.Debug("got EOF, terminating")
		return true
	}

	if isBenignConnTermination(err) {
		log.Info("got benign error %v for connection to %v, terminating", err, conn.RemoteAddr())
		return true
	}

	// we always raise an alert since this is almost always bad news in shuckle
	log.RaiseAlertStack("", 1, "got unexpected error %v from %v", err, conn.RemoteAddr())

	// attempt to say goodbye, ignore errors
	if eggsErr, isEggsErr := err.(msgs.ErrCode); isEggsErr {
		writeShuckleResponseError(log, conn, eggsErr)
		return false
	} else {
		writeShuckleResponseError(log, conn, msgs.INTERNAL_ERROR)
		return true
	}
}

func readShuckleRequest(
	log *lib.Logger,
	r io.Reader,
) (msgs.ShuckleRequest, error) {
	var protocol uint32
	if err := binary.Read(r, binary.LittleEndian, &protocol); err != nil {
		return nil, err
	}
	if protocol != msgs.SHUCKLE_REQ_PROTOCOL_VERSION {
		return nil, fmt.Errorf("bad shuckle protocol, expected %08x, got %08x", msgs.SHUCKLE_REQ_PROTOCOL_VERSION, protocol)
	}
	var len uint32
	if err := binary.Read(r, binary.LittleEndian, &len); err != nil {
		return nil, fmt.Errorf("could not read len: %w", err)
	}
	data := make([]byte, len)
	if _, err := io.ReadFull(r, data); err != nil {
		return nil, fmt.Errorf("could not read response body: %w", err)
	}
	kind := msgs.ShuckleMessageKind(data[0])
	var req msgs.ShuckleRequest
	switch kind {
	case msgs.REGISTER_BLOCK_SERVICES:
		req = &msgs.RegisterBlockServicesReq{}
	case msgs.SHARDS:
		req = &msgs.ShardsReq{}
	case msgs.REGISTER_SHARD:
		req = &msgs.RegisterShardReq{}
	case msgs.REGISTER_SHARD_REPLICA:
		req = &msgs.RegisterShardReplicaReq{}
	case msgs.SHARD_REPLICAS:
		req = &msgs.ShardReplicasReq{}
	case msgs.ALL_BLOCK_SERVICES:
		req = &msgs.AllBlockServicesReq{}
	case msgs.SET_BLOCK_SERVICE_DECOMMISSIONED:
		req = &msgs.SetBlockServiceDecommissionedReq{}
	case msgs.SET_BLOCK_SERVICE_FLAGS:
		req = &msgs.SetBlockServiceFlagsReq{}
	case msgs.REGISTER_CDC:
		req = &msgs.RegisterCdcReq{}
	case msgs.REGISTER_CDC_REPLICA:
		req = &msgs.RegisterCdcReplicaReq{}
	case msgs.CDC:
		req = &msgs.CdcReq{}
	case msgs.CDC_REPLICAS:
		req = &msgs.CdcReplicasReq{}
	case msgs.INFO:
		req = &msgs.InfoReq{}
	case msgs.BLOCK_SERVICE:
		req = &msgs.BlockServiceReq{}
	case msgs.INSERT_STATS:
		req = &msgs.InsertStatsReq{}
	case msgs.SHARD:
		req = &msgs.ShardReq{}
	case msgs.GET_STATS:
		req = &msgs.GetStatsReq{}
	case msgs.SHUCKLE:
		req = &msgs.ShuckleReq{}
	case msgs.SHARD_BLOCK_SERVICES:
		req = &msgs.ShardBlockServicesReq{}
	default:
		return nil, fmt.Errorf("bad shuckle request kind %v", kind)
	}
	if err := bincode.Unpack(data[1:], req); err != nil {
		return nil, err
	}
	return req, nil
}

func handleRequest(log *lib.Logger, s *state, conn *net.TCPConn) {
	defer conn.Close()

	for {
		req, err := readShuckleRequest(log, conn)
		if err != nil {
			if handleError(log, conn, err) {
				return
			} else {
				continue
			}
		}
		log.Debug("handling request %T from %s", req, conn.RemoteAddr())
		resp, err := handleRequestParsed(log, s, req)
		if err != nil {
			if handleError(log, conn, err) {
				return
			}
		} else {
			log.Debug("sending back response %T to %s", resp, conn.RemoteAddr())
			if err := writeShuckleResponse(log, conn, resp); err != nil {
				if handleError(log, conn, err) {
					return
				}
			}
		}
	}
}

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

//go:embed shuckleface.png
var shuckleFacePngStr []byte

//go:embed bootstrap.5.0.2.min.css
var bootstrapCssStr []byte

type pageData struct {
	Title string
	Body  any
}

//go:embed base.html
var baseTemplateStr string

func renderPage(
	tmpl *template.Template,
	data *pageData,
) []byte {
	content := bytes.NewBuffer([]byte{})
	if err := tmpl.Execute(content, &data); err != nil {
		panic(err)
	}
	return content.Bytes()
}

func sendPage(
	tmpl *template.Template,
	data *pageData,
	status int,
) (io.ReadCloser, int64, int) {
	content := renderPage(tmpl, data)
	return io.NopCloser(bytes.NewReader(content)), int64(len(content)), status
}

func handleWithRecover(
	log *lib.Logger,
	w http.ResponseWriter,
	r *http.Request,
	methodAllowed *string,
	// if the ReadCloser == nil, it means that
	// `handle` does not want us to handle the request.
	handle func(log *lib.Logger, query url.Values) (io.ReadCloser, int64, int),
) {
	if methodAllowed == nil {
		str := http.MethodGet
		methodAllowed = &str
	}
	statusPtr := new(int)
	var content io.ReadCloser
	defer func() {
		if content != nil {
			content.Close()
		}
	}()
	sizePtr := new(int64)
	func() {
		var status int
		var size int64
		defer func() {
			if r := recover(); r != nil {
				content, size, status = sendPage(errorPage(http.StatusInternalServerError, fmt.Sprintf("PANIC: %v\n%v", r, string(debug.Stack()))))
				*statusPtr = status
				*sizePtr = size
			}
		}()
		if r.Method != *methodAllowed {
			content, size, status = sendPage(errorPage(http.StatusMethodNotAllowed, "Only GET allowed"))
		} else {
			query, err := url.ParseQuery(r.URL.RawQuery)
			if err != nil {
				content, size, status = sendPage(errorPage(http.StatusBadRequest, "could not parse query"))
			} else {
				content, size, status = handle(log, query)
			}
		}
		*statusPtr = status
		*sizePtr = size
	}()
	if content != nil {
		w.Header().Set("Cache-Control", "no-cache")
		w.WriteHeader(*statusPtr)
		if written, err := io.CopyN(w, content, *sizePtr); err != nil {
			if !isBenignConnTermination(err) {
				log.RaiseAlert("could not send full response of size %v, %v written: %w", *sizePtr, written, err)
			}
		}
	}
}

func handlePage(
	log *lib.Logger,
	w http.ResponseWriter,
	r *http.Request,
	// third result is status code
	page func(query url.Values) (*template.Template, *pageData, int),
) {
	handleWithRecover(
		log, w, r, nil,
		func(log *lib.Logger, query url.Values) (io.ReadCloser, int64, int) {
			return sendPage(page(query))
		},
	)
}

//go:embed error.html
var errorTemplateStr string

var errorTemplate *template.Template

func errorPage(status int, body string) (*template.Template, *pageData, int) {
	return errorTemplate, &pageData{Title: "Error!", Body: body}, status
}

type indexData struct {
	NumBlockServices    int
	NumFailureDomains   int
	TotalCapacity       string
	TotalUsed           string
	TotalUsedPercentage string
	CDCAddr1            string
	CDCAddr2            string
	CDCLastSeen         string
	Blocks              uint64
}

//go:embed index.html
var indexTemplateStr string

func formatSize(bytes uint64) string {
	bytesf := float64(bytes)
	if bytes == 0 {
		return "0"
	}
	if bytes < (uint64(1) << 20) {
		return fmt.Sprintf("%.2fKiB", bytesf/float64(uint64(1)<<10))
	}
	if bytes < (uint64(1) << 30) {
		return fmt.Sprintf("%.2fMiB", bytesf/float64(uint64(1)<<20))
	}
	if bytes < (uint64(1) << 40) {
		return fmt.Sprintf("%.2fGiB", bytesf/float64(uint64(1)<<30))
	}
	if bytes < (uint64(1) << 50) {
		return fmt.Sprintf("%.2fTiB", bytesf/float64(uint64(1)<<40))
	}
	return fmt.Sprintf("%.2fPiB", bytesf/float64(uint64(1)<<50))
}

func formatPreciseSize(bytes uint64) string {
	if bytes == 0 {
		return "0"
	}
	if bytes%(1<<30) == 0 {
		return fmt.Sprintf("%vGiB", bytes>>30)
	}
	if bytes%(1<<20) == 0 {
		return fmt.Sprintf("%vMiB", bytes>>20)
	}
	if bytes%(1<<10) == 0 {
		return fmt.Sprintf("%vKiB", bytes>>10)
	}
	return fmt.Sprintf("%vB", bytes)
}

var indexTemplate *template.Template

func formatNanos(nanos uint64) string {
	var amount float64
	var unit string
	if nanos < 1e3 {
		amount = float64(nanos)
		unit = "ns"
	} else if nanos < 1e6 {
		amount = float64(nanos) / 1e3
		unit = "µs"
	} else if nanos < 1e9 {
		amount = float64(nanos) / 1e6
		unit = "ms"
	} else if nanos < 1e12 {
		amount = float64(nanos) / 1e9
		unit = "s "
	} else if nanos < 1e12*60 {
		amount = float64(nanos) / (1e9 * 60.0)
		unit = "m"
	} else {
		amount = float64(nanos) / (1e9 * 60.0 * 60.0)
		unit = "h"
	}
	return fmt.Sprintf("%7.2f%s", amount, unit)
}

func handleIndex(ll *lib.Logger, state *state, w http.ResponseWriter, r *http.Request) {
	handlePage(
		ll, w, r,
		func(_ url.Values) (*template.Template, *pageData, int) {
			if r.URL.Path != "/" {
				return errorPage(http.StatusNotFound, "not found")
			}

			blockServices, err := state.selectBlockServices(nil, 0)
			if err != nil {
				ll.RaiseAlert("error reading block services: %s", err)
				return errorPage(http.StatusInternalServerError, fmt.Sprintf("error reading block services: %s", err))
			}

			data := indexData{
				NumBlockServices: len(blockServices),
			}
			now := msgs.Now()
			formatLastSeen := func(t msgs.EggsTime) string {
				return formatNanos(uint64(now) - uint64(t))
			}
			totalCapacityBytes := uint64(0)
			totalAvailableBytes := uint64(0)
			failureDomainsBytes := make(map[string]struct{})

			cdc, err := state.selectCDC()
			if err != nil {
				ll.RaiseAlert("error reading cdc: %s", err)
				return errorPage(http.StatusInternalServerError, fmt.Sprintf("error reading cdc: %s", err))
			}
			data.CDCAddr1 = fmt.Sprintf("%v:%v", net.IP(cdc.ip1[:]), cdc.port1)
			data.CDCAddr2 = fmt.Sprintf("%v:%v", net.IP(cdc.ip2[:]), cdc.port2)
			data.CDCLastSeen = formatLastSeen(cdc.lastSeen)

			data.TotalUsed = formatSize(totalCapacityBytes - totalAvailableBytes)
			data.TotalCapacity = formatSize(totalAvailableBytes)
			data.NumFailureDomains = len(failureDomainsBytes)
			if totalAvailableBytes == 0 {
				data.TotalUsedPercentage = "0%"
			} else {
				data.TotalUsedPercentage = fmt.Sprintf("%0.2f%%", 100.0*(1.0-float64(totalAvailableBytes)/float64(totalCapacityBytes)))
			}
			return indexTemplate, &pageData{Title: "Shuckle", Body: &data}, http.StatusOK
		},
	)
}

//go:embed file.html
var fileTemplateStr string

type fileBlock struct {
	Id           string
	Crc          string
	BlockService string
	Link         string
	Hosts        string
}

type fileSpan struct {
	Offset       string
	Size         string
	Crc          string
	StorageClass string
	CellSize     string
	BodyBlocks   []fileBlock
	BodyBytes    string
	ParityBlocks int
	DataBlocks   int
	Stripes      uint8
}

type pathSegment struct {
	Segment   string
	PathSoFar string
}

type fileData struct {
	Id            string
	Path          string // might be empty
	Size          string
	Mtime         string
	Atime         string
	TransientNote string // only if transient
	DownloadLink  string
	AllInline     bool
	Spans         []fileSpan
	PathSegments  []pathSegment
	DirectoryLink string
}

//go:embed directory.html
var directoryTemplateStr string

type directoryInfoEntry struct {
	Tag           string
	Body          string
	InheritedFrom string
}

type directoryData struct {
	Id           string
	Path         string // might be empty
	PathSegments []pathSegment
	Mtime        string
	Owner        string
	Info         []directoryInfoEntry
}

func refreshClient(log *lib.Logger, state *state) error {
	shards, err := state.selectShards()
	if err != nil {
		return fmt.Errorf("error reading shards: %s", err)
	}
	cdc, err := state.selectCDC()
	if err != nil {
		return fmt.Errorf("error reading cdc: %s", err)
	}

	var shardIps [256][2][4]byte
	var shardPorts [256][2]uint16
	for i, si := range shards {
		shardIps[i][0] = si.Ip1
		shardPorts[i][0] = si.Port1
		shardIps[i][1] = si.Ip2
		shardPorts[i][1] = si.Port2
	}
	var cdcIps [2][4]byte
	var cdcPorts [2]uint16
	cdcIps[0] = cdc.ip1
	cdcPorts[0] = cdc.port1
	cdcIps[1] = cdc.ip2
	cdcPorts[1] = cdc.port2
	state.client.SetAddrs(&cdcIps, &cdcPorts, &shardIps, &shardPorts)
	return nil
}

func normalizePath(path string) string {
	newPath := ""
	for _, segment := range strings.Split(path, "/") {
		if segment == "" {
			continue
		}
		newPath = newPath + "/" + segment
	}
	if len(newPath) > 1 {
		newPath = newPath[1:] // strip leading /
	}
	return newPath
}

func lookup(log *lib.Logger, client *client.Client, path string) *msgs.InodeId {
	id := msgs.ROOT_DIR_INODE_ID
	if path == "" {
		return &id
	}
	segments := strings.Split(path, "/")
	for _, segment := range segments {
		req := msgs.LookupReq{
			DirId: id,
			Name:  segment,
		}
		resp := msgs.LookupResp{}
		if err := client.ShardRequest(log, id.Shard(), &req, &resp); err != nil {
			eggsErr, ok := err.(msgs.ErrCode)
			if ok {
				if eggsErr == msgs.NAME_NOT_FOUND {
					return nil
				}
			}
			panic(err)
		}
		id = resp.TargetId
	}
	return &id
}

func pathSegments(path string) []pathSegment {
	pathSegments := []pathSegment{}
	if path == "" {
		return pathSegments
	}
	segments := strings.Split(path, "/")
	pathSoFar := "/"
	for _, segment := range segments {
		if pathSoFar == "/" {
			pathSoFar = pathSoFar + segment
		} else {
			pathSoFar = pathSoFar + "/" + segment
		}
		pathSegments = append(pathSegments, pathSegment{Segment: segment, PathSoFar: pathSoFar})
	}
	return pathSegments
}

var fileTemplate *template.Template
var directoryTemplate *template.Template

func handleInode(
	log *lib.Logger,
	state *state,
	w http.ResponseWriter,
	r *http.Request,
) {
	handlePage(
		log, w, r,
		func(query url.Values) (*template.Template, *pageData, int) {
			path := r.URL.Path[len("/browse"):]
			path = normalizePath(path)
			var id msgs.InodeId
			idStr := query.Get("id")
			if idStr != "" {
				i, err := strconv.ParseUint(idStr, 0, 63)
				if err != nil {
					return errorPage(http.StatusBadRequest, fmt.Sprintf("cannot parse id %v: %v", idStr, err))
				}
				id = msgs.InodeId(i)
			}
			nameStr := query.Get("name")
			if id != msgs.NULL_INODE_ID && id != msgs.ROOT_DIR_INODE_ID && path == "/" {
				path = "" // path not provided
			}
			if id != msgs.NULL_INODE_ID && id != msgs.ROOT_DIR_INODE_ID && path != "" {
				return errorPage(http.StatusBadRequest, "cannot specify both id and path")
			}
			if id == msgs.ROOT_DIR_INODE_ID && path != "/" && path != "" {
				return errorPage(http.StatusBadRequest, "bad root inode id")
			}
			c := state.client
			if id == msgs.NULL_INODE_ID {
				mbId := lookup(log, c, path)
				if mbId == nil {
					return errorPage(http.StatusNotFound, fmt.Sprintf("path '%v' not found", path))
				}
				id = *mbId
			}
			if id.Type() == msgs.DIRECTORY {
				data := directoryData{
					Id: fmt.Sprintf("%v", id),
				}
				if path != "" {
					data.Path = "/" + path + "/"
				}
				if id == msgs.ROOT_DIR_INODE_ID {
					data.Path = "/"
				}
				title := fmt.Sprintf("Directory %v", data.Id)
				{
					resp := msgs.StatDirectoryResp{}
					if err := c.ShardRequest(log, id.Shard(), &msgs.StatDirectoryReq{Id: id}, &resp); err != nil {
						panic(err)
					}
					data.Owner = resp.Owner.String()
					data.Mtime = resp.Mtime.String()
				}
				data.PathSegments = pathSegments(path)
				{
					data.Info = []directoryInfoEntry{}
					dirInfoCache := client.NewDirInfoCache()
					populateInfo := func(info msgs.IsDirectoryInfoEntry) {
						inheritedFrom, err := c.ResolveDirectoryInfoEntry(log, dirInfoCache, id, info)
						if err != nil {
							panic(err)
						}
						data.Info = append(data.Info, directoryInfoEntry{
							Tag:           info.Tag().String(),
							Body:          fmt.Sprintf("%+v", info),
							InheritedFrom: fmt.Sprintf("%v", inheritedFrom),
						})
					}
					populateInfo(&msgs.SnapshotPolicy{})
					populateInfo(&msgs.SpanPolicy{})
					populateInfo(&msgs.BlockPolicy{})
					populateInfo(&msgs.StripePolicy{})
				}
				return directoryTemplate, &pageData{Title: title, Body: &data}, http.StatusOK
			} else {
				data := fileData{
					Id:        fmt.Sprintf("%v", id),
					AllInline: true,
				}
				if len(pathSegments(path)) > 0 {
					data.PathSegments = pathSegments(path)
					data.DirectoryLink = fmt.Sprintf("/browse%s?Name=%s", filepath.Dir("/"+path), url.QueryEscape("^"+regexp.QuoteMeta(filepath.Base(path)))+"$")
					data.Path = "/" + path
				}
				title := fmt.Sprintf("File %v", data.Id)
				{
					resp := msgs.StatFileResp{}
					err := c.ShardRequest(log, id.Shard(), &msgs.StatFileReq{Id: id}, &resp)
					if err == msgs.FILE_NOT_FOUND {
						transResp := msgs.StatTransientFileResp{}
						transErr := c.ShardRequest(log, id.Shard(), &msgs.StatTransientFileReq{Id: id}, &transResp)
						if transErr == nil {
							data.Mtime = transResp.Mtime.String()
							data.Size = fmt.Sprintf("%v (%v bytes)", formatSize(transResp.Size), transResp.Size)
							data.TransientNote = transResp.Note
						} else {
							panic(fmt.Errorf("normal: %v, transient: %v", err, transErr))
						}
					} else if err == nil {
						data.Mtime = resp.Mtime.String()
						data.Atime = resp.Atime.String()
						data.Size = fmt.Sprintf("%v (%v bytes)", formatSize(resp.Size), resp.Size)
					} else {
						panic(err)
					}
				}
				data.DownloadLink = fmt.Sprintf("/files/%v", id)
				if nameStr != "" {
					data.DownloadLink = fmt.Sprintf("%s?name=%s", data.DownloadLink, nameStr)
				} else if len(data.PathSegments) > 0 {
					data.DownloadLink = fmt.Sprintf("%s?name=%s", data.DownloadLink, data.PathSegments[len(data.PathSegments)-1].Segment)
				}
				{
					req := msgs.FileSpansReq{FileId: id}
					resp := msgs.FileSpansResp{}
					blockServiceToHosts := make(map[msgs.BlockServiceId]string)
					for {
						if err := c.ShardRequest(log, id.Shard(), &req, &resp); err != nil {
							panic(err)
						}
						for _, span := range resp.Spans {
							fs := fileSpan{
								Offset:       formatPreciseSize(span.Header.ByteOffset),
								Size:         formatPreciseSize(uint64(span.Header.Size)),
								Crc:          span.Header.Crc.String(),
								StorageClass: span.Header.StorageClass.String(),
							}
							if span.Header.StorageClass == msgs.INLINE_STORAGE {
								fs.BodyBytes = fmt.Sprintf("%q", span.Body.(*msgs.FetchedInlineSpan).Body)
							} else {
								data.AllInline = false
								body := span.Body.(*msgs.FetchedBlocksSpan)
								fs.CellSize = formatPreciseSize(uint64(body.CellSize))
								fs.DataBlocks = body.Parity.DataBlocks()
								fs.ParityBlocks = body.Parity.ParityBlocks()
								fs.Stripes = body.Stripes
								blockSize := body.CellSize * uint32(body.Stripes)
								for _, block := range body.Blocks {
									blockService := resp.BlockServices[block.BlockServiceIx]
									hosts, found := blockServiceToHosts[blockService.Id]
									if !found {
										names1, _ := net.LookupAddr(net.IP(blockService.Ip1[:]).String())
										names2 := []string{}
										if blockService.Ip2 != [4]byte{0, 0, 0, 0} {
											names2, _ = net.LookupAddr(net.IP(blockService.Ip2[:]).String())
										}
										hosts = strings.Join(append(names1, names2...), ",")
										blockServiceToHosts[blockService.Id] = hosts
									}
									fb := fileBlock{
										Id:           block.BlockId.String(),
										BlockService: blockService.Id.String(),
										Crc:          block.Crc.String(),
										Link:         fmt.Sprintf("/blocks/%v/%v?size=%v", blockService.Id, block.BlockId, blockSize),
										Hosts:        hosts,
									}
									fs.BodyBlocks = append(fs.BodyBlocks, fb)
								}
							}
							data.Spans = append(data.Spans, fs)
						}
						req.ByteOffset = resp.NextOffset
						if req.ByteOffset == 0 {
							break
						}
					}
				}
				return fileTemplate, &pageData{Title: title, Body: &data}, http.StatusOK
			}
		},
	)
}

func handleBlock(log *lib.Logger, st *state, w http.ResponseWriter, r *http.Request) {
	handleWithRecover(
		log, w, r, nil,
		func(log *lib.Logger, query url.Values) (io.ReadCloser, int64, int) {
			segments := strings.Split(r.URL.Path, "/")[1:]
			if segments[0] != "blocks" {
				panic(fmt.Errorf("bad path %v", r.URL.Path))
			}
			if len(segments) != 3 {
				return sendPage(errorPage(http.StatusBadRequest, fmt.Sprintf("Expected /blocks/<blockservice>/<blockid>, got %v", r.URL.Path)))
			}
			blockServiceIdU, err := strconv.ParseUint(segments[1], 0, 64)
			if err != nil {
				return sendPage(errorPage(http.StatusBadRequest, fmt.Sprintf("Expected /blocks/<blockservice>/<blockid>, got %v", r.URL.Path)))
			}
			blockServiceId := msgs.BlockServiceId(blockServiceIdU)
			blockIdU, err := strconv.ParseUint(segments[2], 0, 64)
			if err != nil {
				return sendPage(errorPage(http.StatusBadRequest, fmt.Sprintf("Expected /blocks/<blockservice>/<blockid>, got %v", r.URL.Path)))
			}
			blockId := msgs.BlockId(blockIdU)
			size, err := strconv.ParseUint(query.Get("size"), 0, 32)
			if err != nil {
				return sendPage(errorPage(http.StatusBadRequest, fmt.Sprintf("Bad block size '%v'", query.Get("size"))))
			}
			var blockService msgs.BlockService
			var conn *net.TCPConn
			{
				rows, err := st.db.Query("SELECT ip1, port1, ip2, port2, flags FROM block_services WHERE id = ?", blockServiceId)
				if err != nil {
					log.RaiseAlert("Error reading block service: %s", err)
					return sendPage(errorPage(http.StatusInternalServerError, fmt.Sprintf("Error reading block service: %s", err)))
				}
				defer rows.Close()

				if !rows.Next() {
					return sendPage(errorPage(http.StatusNotFound, fmt.Sprintf("Unknown block service id %v", blockServiceId)))
				}
				var ip1, ip2 []byte
				if err := rows.Scan(&ip1, &blockService.Port1, &ip2, &blockService.Port2, &blockService.Flags); err != nil {
					return sendPage(errorPage(http.StatusInternalServerError, fmt.Sprintf("Error reading block service: %s", err)))
				}
				blockService.Id = blockServiceId
				copy(blockService.Ip1[:], ip1[:])
				copy(blockService.Ip2[:], ip2[:])

				conn, err = client.BlockServiceConnection(log, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2)
				if err != nil {
					panic(err)
				}
			}
			if err := client.FetchBlock(log, conn, &blockService, blockId, 0, uint32(size)); err != nil {
				panic(err)
			}
			w.Header().Set("Content-Type", "application/x-binary")
			w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=\"%016x\"", uint64(blockId)))
			log.Info("serving block of size %v", size)
			return conn, int64(size), http.StatusOK
		},
	)
}

var readSpanBufPool *lib.BufPool

func handleFile(log *lib.Logger, st *state, w http.ResponseWriter, r *http.Request) {
	handleWithRecover(
		log, w, r, nil,
		func(log *lib.Logger, query url.Values) (io.ReadCloser, int64, int) {
			segments := strings.Split(r.URL.Path, "/")[1:]
			if segments[0] != "files" {
				panic(fmt.Errorf("bad path %v", r.URL.Path))
			}
			if len(segments) != 2 {
				return sendPage(errorPage(http.StatusBadRequest, fmt.Sprintf("Expected /files/<fileid>, got %v", r.URL.Path)))
			}
			fileIdU, err := strconv.ParseUint(segments[1], 0, 64)
			if err != nil {
				return sendPage(errorPage(http.StatusBadRequest, fmt.Sprintf("Expected /blocks/<fileid>, got %v", r.URL.Path)))
			}
			fileId := msgs.InodeId(fileIdU)
			fname := query.Get("name")
			mimeType := mime.TypeByExtension(path.Ext(fname))
			if fname == "" {
				fname = fileId.String()
			}
			if mimeType == "" {
				mimeType = "application/x-binary"
			}
			client := st.client
			statResp := msgs.StatFileResp{}
			err = client.ShardRequest(log, fileId.Shard(), &msgs.StatFileReq{Id: fileId}, &statResp)
			if err == msgs.FILE_NOT_FOUND {
				return sendPage(errorPage(http.StatusNotFound, fmt.Sprintf("could not find file %v", fileId)))
			}
			if err != nil {
				panic(err)
			}

			r, err := client.ReadFile(log, readSpanBufPool, fileId)
			if err != nil {
				panic(err)
			}

			w.Header().Set("Content-Type", mimeType)
			w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=%q", fname))

			return r, int64(statResp.Size), http.StatusOK
		},
	)
}

//go:embed stats.html
var statsTemplateStr string

var statsTemplate *template.Template

//go:embed chart-4.3.0.js
var chartsJsStr []byte

func handleStats(log *lib.Logger, st *state, w http.ResponseWriter, r *http.Request) {
	handlePage(
		log, w, r,
		func(query url.Values) (*template.Template, *pageData, int) {
			path := r.URL.Path[len("/stats"):]
			path = normalizePath(path)
			if len(path) > 0 {
				return errorPage(http.StatusNotFound, "path should be just /stats")
			}
			pd := pageData{
				Title: "stats",
				Body:  nil,
			}
			return statsTemplate, &pd, http.StatusOK
		},
	)
}

//go:embed transient.html
var transientTemplateStr string

var transientTemplate *template.Template

func handleTransient(log *lib.Logger, st *state, w http.ResponseWriter, r *http.Request) {
	handlePage(
		log, w, r,
		func(query url.Values) (*template.Template, *pageData, int) {
			pd := pageData{
				Title: "transient",
				Body:  nil,
			}
			return transientTemplate, &pd, http.StatusOK
		},
	)
}

func sendJson(w http.ResponseWriter, out []byte) (io.ReadCloser, int64, int) {
	w.Header().Set("Content-Type", "application/json")
	return ioutil.NopCloser(bytes.NewReader(out)), int64(len(out)), http.StatusOK
}

func sendJsonErr(w http.ResponseWriter, err any, status int) (io.ReadCloser, int64, int) {
	w.Header().Set("Content-Type", "application/json")
	j := map[string]any{"err": fmt.Sprintf("%v", err)}
	eggsErr, ok := err.(msgs.ErrCode)
	if ok {
		j["errCode"] = uint8(eggsErr)
	}
	out, err := json.Marshal(j)
	w.Header().Set("Content-Type", "application/json")
	return ioutil.NopCloser(bytes.NewReader(out)), int64(len(out)), status
}

func handleApi(log *lib.Logger, st *state, w http.ResponseWriter, r *http.Request) {
	methodAllowed := http.MethodPost
	handleWithRecover(
		log, w, r, &methodAllowed,
		func(log *lib.Logger, query url.Values) (io.ReadCloser, int64, int) {
			segments := strings.Split(r.URL.Path, "/")[1:]
			if segments[0] != "api" {
				panic(fmt.Errorf("bad path %v", r.URL.Path))
			}
			badUrl := func(what string) (io.ReadCloser, int64, int) {
				return sendJsonErr(w, fmt.Errorf("expected /api/(shuckle|cdc|shard)/<req>, got %v (%v)", r.URL.Path, what), http.StatusBadRequest)
			}
			sendResponse := func(resp bincode.Packable) (io.ReadCloser, int64, int) {
				accept := r.Header.Get("Accept")
				if accept == "application/octet-stream" {
					buf := bytes.NewBuffer([]byte{})
					if err := resp.Pack(buf); err != nil {
						panic(err)
					}
					w.Header().Set("Content-Type", "application/octet-stream")
					out := buf.Bytes()
					return ioutil.NopCloser(bytes.NewReader(out)), int64(len(out)), http.StatusOK
				} else {
					out, err := json.Marshal(map[string]any{"resp": resp})
					if err != nil {
						return sendJsonErr(w, fmt.Errorf("could not marshal request: %v", err), http.StatusInternalServerError)
					}
					return sendJson(w, out)
				}
			}
			switch segments[1] {
			case "shuckle":
				if len(segments) != 3 {
					return badUrl("bad segments len")
				}
				req, _, err := msgs.MkShuckleMessage(segments[2])
				if err != nil {
					return badUrl("bad ")
				}
				if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
					return sendJsonErr(w, fmt.Errorf("could not decode request: %v", err), http.StatusBadRequest)
				}
				resp, err := handleRequestParsed(log, st, req)
				if err != nil {
					return sendJsonErr(w, err, http.StatusInternalServerError)
				}
				return sendResponse(resp)
			case "cdc":
				if len(segments) != 3 {
					return badUrl("bad segments len")
				}
				req, resp, err := msgs.MkCDCMessage(segments[2])
				if err != nil {
					return badUrl("bad req type")
				}
				if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
					return sendJsonErr(w, fmt.Errorf("could not decode request: %v", err), http.StatusBadRequest)
				}
				client := st.client
				if err := client.CDCRequest(log, req, resp); err != nil {
					return sendJsonErr(w, err, http.StatusInternalServerError)
				}
				return sendResponse(resp)
			case "shard":
				if len(segments) != 4 {
					return badUrl("bad segments len")
				}
				req, resp, err := msgs.MkShardMessage(segments[3])
				if err != nil {
					return badUrl("bad req type")
				}
				shidU, err := strconv.ParseUint(segments[2], 10, 8)
				if err != nil {
					return badUrl("bad shard id")
				}
				shid := msgs.ShardId(shidU)
				if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
					return sendJsonErr(w, fmt.Errorf("could not decode request: %v", err), http.StatusBadRequest)
				}
				client := st.client
				if err := client.ShardRequest(log, shid, req, resp); err != nil {
					return sendJsonErr(w, err, http.StatusInternalServerError)
				}
				return sendResponse(resp)
			default:
				return badUrl("bad api type")
			}
		},
	)
}

//go:embed preact-10.18.1.module.js
var preactJsStr []byte

//go:embed preact-hooks-10.18.1.module.js
var preactHooksJsStr []byte

//go:embed scripts.js
var scriptsJs []byte

func setupRouting(log *lib.Logger, st *state, scriptsJsFile string) {
	errorTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "error", body: errorTemplateStr},
	)

	setupPage := func(path string, handle func(ll *lib.Logger, state *state, w http.ResponseWriter, r *http.Request)) {
		http.HandleFunc(
			path,
			func(w http.ResponseWriter, r *http.Request) { handle(log, st, w, r) },
		)
	}

	// Static assets
	http.HandleFunc(
		"/static/bootstrap.5.0.2.min.css",
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "text/css; charset=utf-8")
			w.Header().Set("Cache-Control", "max-age=31536000")
			w.Write(bootstrapCssStr)
		},
	)
	http.HandleFunc(
		"/static/shuckle-face.png",
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "image/png")
			w.Header().Set("Cache-Control", "max-age=31536000")
			w.Write(shuckleFacePngStr)
		},
	)
	http.HandleFunc(
		"/static/chart-4.3.0.js",
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "text/javascript")
			w.Header().Set("Cache-Control", "max-age=31536000")
			w.Write(chartsJsStr)
		},
	)
	http.HandleFunc(
		"/static/preact-10.18.1.module.js",
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "text/javascript")
			w.Header().Set("Cache-Control", "max-age=31536000")
			w.Write(preactJsStr)
		},
	)
	http.HandleFunc(
		"/static/preact-hooks-10.18.1.module.js",
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "text/javascript")
			w.Header().Set("Cache-Control", "max-age=31536000")
			w.Write(preactHooksJsStr)
		},
	)

	http.HandleFunc(
		"/static/scripts.js",
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "text/javascript")
			w.Header().Set("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
			w.Header().Set("Pragma", "no-cache")
			w.Header().Set("Expires", "0")
			if scriptsJsFile == "" {
				w.Write(scriptsJs)
			} else {
				file, err := os.Open(scriptsJsFile)
				if err != nil {
					panic(err)
				}
				defer file.Close()
				if _, err := io.Copy(w, file); err != nil {
					panic(err)
				}
			}
		},
	)

	// blocks serving
	http.HandleFunc(
		"/blocks/",
		func(w http.ResponseWriter, r *http.Request) { handleBlock(log, st, w, r) },
	)

	// file serving
	http.HandleFunc(
		"/files/",
		func(w http.ResponseWriter, r *http.Request) { handleFile(log, st, w, r) },
	)

	http.HandleFunc(
		"/api/",
		func(w http.ResponseWriter, r *http.Request) { handleApi(log, st, w, r) },
	)

	// pages
	indexTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "shuckle", body: indexTemplateStr},
	)
	setupPage("/", handleIndex)

	fileTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "file", body: fileTemplateStr},
	)
	directoryTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "directory", body: directoryTemplateStr},
	)
	setupPage("/browse/", handleInode)

	statsTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "file", body: statsTemplateStr},
	)
	setupPage("/stats", handleStats)

	transientTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "transient", body: transientTemplateStr},
	)
	setupPage("/transient", handleTransient)
}

// Writes stats to influx db.
func sendMetrics(log *lib.Logger, st *state) error {
	metrics := lib.MetricsBuilder{}
	rand := wyhash.New(rand.Uint64())
	alert := log.NewNCAlert(10 * time.Second)
	for {
		log.Info("sending metrics")
		metrics.Reset()
		now := time.Now()
		for _, req := range msgs.AllShuckleMessageKind {
			t := st.counters[req]
			metrics.Measurement("eggsfs_shuckle_requests")
			metrics.Tag("kind", req.String())
			metrics.FieldU64("count", t.Count())
			metrics.Timestamp(now)
		}
		err := lib.SendMetrics(metrics.Payload())
		if err == nil {
			log.ClearNC(alert)
			sleepFor := time.Minute + time.Duration(rand.Uint64() & ^(uint64(1)<<63))%time.Minute
			log.Info("metrics sent, sleeping for %v", sleepFor)
			time.Sleep(sleepFor)
		} else {
			log.RaiseNC(alert, "failed to send metrics, will try again in a second: %v", err)
			time.Sleep(time.Second)
		}
	}
}

func deleteOldStats(ll *lib.Logger, st *state) error {
	ll.Info("clearing out old stats")
	n := sql.Named

	st.mutex.Lock()
	defer st.mutex.Unlock()

	_, err := st.db.Exec(
		"DELETE FROM stats WHERE time < :time",
		n("time", time.Now().Add(-time.Hour*24*7).UnixNano()),
	)
	return err
}

func writeStats(ll *lib.Logger, st *state) {
	stats := lib.TimingsToStats("shuckle", st.counters)
	st.resetTimings()
	ll.Info("writing %v stats to database", len(stats))
	if _, err := handleInsertStats(ll, st, &msgs.InsertStatsReq{Stats: stats}); err != nil {
		panic(err)
	}
	if err := deleteOldStats(ll, st); err != nil {
		panic(err)
	}
}

func statsWriter(ll *lib.Logger, st *state) {
	for {
		writeStats(ll, st)
		time.Sleep(time.Hour)
	}
}

func serviceMonitor(ll *lib.Logger, st *state, staleDelta time.Duration) error {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()

	staleBlockServicesAlerts := make(map[msgs.BlockServiceId]*lib.XmonNCAlert)
	staleShardsAlerts := make(map[msgs.ShardId]*lib.XmonNCAlert)
	staleCDCAlert := ll.NewNCAlert(0)
	staleCDCAlert.SetAppType(lib.XMON_DAYTIME)

	for {
		<-ticker.C

		now := msgs.Now()
		thresh := uint64(now) - uint64(staleDelta.Nanoseconds())

		formatLastSeen := func(t msgs.EggsTime) string {
			return formatNanos(uint64(now) - uint64(t))
		}

		n := sql.Named

		st.mutex.Lock()
		_, err := st.db.Exec(
			"UPDATE block_services SET flags = ((flags & ~:flag) | :flag) WHERE last_seen < :thresh",
			n("flag", msgs.EGGSFS_BLOCK_SERVICE_STALE),
			n("thresh", thresh),
		)
		st.mutex.Unlock()
		if err != nil {
			ll.RaiseAlert("error setting block services stale: %s", err)
		}

		// Wrap the following select statements in func() to make defer work properly.
		func() {
			rows, err := st.db.Query(
				"SELECT id, failure_domain, last_seen FROM block_services WHERE last_seen < :thresh AND (flags & :flag) == 0",
				n("thresh", thresh),
				// we already know that decommissioned block services are gone
				n("flag", msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
			)
			if err != nil {
				ll.RaiseAlert("error selecting blockServices: %s", err)
				return
			}
			defer rows.Close()

			badBlockServices := make(map[msgs.BlockServiceId]struct{})
			for rows.Next() {
				var fd []byte
				var id uint64
				var ts msgs.EggsTime
				err = rows.Scan(&id, &fd, &ts)
				if err != nil {
					ll.RaiseAlert("error decoding blockService row: %s", err)
					return
				}
				bsId := msgs.BlockServiceId(id)
				badBlockServices[bsId] = struct{}{}
				alert, found := staleBlockServicesAlerts[bsId]
				if !found {
					alert = ll.NewNCAlert(0)
					alert.SetAppType(lib.XMON_DAYTIME)
					staleBlockServicesAlerts[bsId] = alert
				}
				ll.RaiseNC(alert, "stale blockservice %v, fd %s (seen %s ago)", id, fd, formatLastSeen(ts))
			}
			for bsId, alert := range staleBlockServicesAlerts {
				if _, found := badBlockServices[bsId]; !found {
					ll.ClearNC(alert)
					delete(staleBlockServicesAlerts, bsId)
				}
			}
		}()

		func() {
			rows, err := st.db.Query("SELECT id, last_seen FROM shards WHERE is_leader = true AND last_seen < :thresh", n("thresh", thresh))
			if err != nil {
				ll.RaiseAlert("error selecting shards: %s", err)
				return
			}
			defer rows.Close()

			badShards := make(map[msgs.ShardId]struct{})
			for rows.Next() {
				var id uint64
				var ts msgs.EggsTime
				err = rows.Scan(&id, &ts)
				if err != nil {
					ll.RaiseAlert("error decoding shard row: %s", err)
					return
				}
				shId := msgs.ShardId(id)
				badShards[shId] = struct{}{}
				alert, found := staleShardsAlerts[shId]
				if !found {
					alert = ll.NewNCAlert(0)
					alert.SetAppType(lib.XMON_DAYTIME)
					staleShardsAlerts[shId] = alert
				}
				ll.RaiseNC(alert, "stale shard %v (seen %s ago)", shId, formatLastSeen(ts))
			}
			for shardId, alert := range staleShardsAlerts {
				if _, found := badShards[shardId]; !found {
					ll.ClearNC(alert)
					delete(staleShardsAlerts, shardId)
				}
			}
		}()

		func() {
			rows, err := st.db.Query("SELECT last_seen FROM cdc WHERE is_leader = true AND last_seen < :thresh", n("thresh", thresh))
			if err != nil {
				ll.RaiseAlert("error selecting blockServices: %s", err)
				return
			}
			defer rows.Close()

			stale := false
			for rows.Next() {
				stale = true
				var ts msgs.EggsTime
				err = rows.Scan(&ts)
				if err != nil {
					ll.RaiseAlert("error decoding cdc row: %s", err)
					return
				}
				ll.RaiseNC(staleCDCAlert, "stale cdc (last seen %s ago)", formatLastSeen(ts))
			}
			if !stale {
				ll.ClearNC(staleCDCAlert)
			}
		}()
	}
}

func blockServiceAlerts(log *lib.Logger, s *state) {
	replaceDecommedAlert := log.NewNCAlert(0)
	replaceDecommedAlert.SetAppType(lib.XMON_NEVER)
	migrateDecommedAlert := log.NewNCAlert(0)
	migrateDecommedAlert.SetAppType(lib.XMON_NEVER)
	for {
		blockServices, err := s.selectBlockServices(nil, 0)
		if err != nil {
			log.RaiseNC(replaceDecommedAlert, "error reading block services, will try again in one second: %s", err)
			time.Sleep(time.Second)
			continue
		}
		// collect non-decommissioned block services
		// failure domain -> path
		activeBlockServices := make(map[string]map[string]struct{})
		for _, bs := range blockServices {
			if bs.Info.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) {
				continue
			}
			fd := bs.Info.FailureDomain.String()
			if _, found := activeBlockServices[fd]; !found {
				activeBlockServices[fd] = make(map[string]struct{})
			}
			if _, alreadyPresent := activeBlockServices[fd][bs.Info.Path]; alreadyPresent {
				log.RaiseAlert("found duplicate block service at path %q for failure domain %q", bs.Info.Path, fd)
				continue
			}
			activeBlockServices[fd][bs.Info.Path] = struct{}{}
		}
		// collect non-replaced decommissioned block services
		missingBlockServices := make(map[string]struct{})
		decommedWithFiles := make(map[string]struct{})
		for _, bs := range blockServices {
			if !bs.Info.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) {
				continue
			}
			fd := bs.Info.FailureDomain.String()
			if _, found := activeBlockServices[fd]; !found {
				// this alert can go once we start decommissioning entire servers, leaving it in
				// now for safety
				log.RaiseAlert("did not find any active block service for block service %v in failure domain %q", bs.Info.Id, fd)
				continue
			}
			if bs.HasFiles {
				decommedWithFiles[fmt.Sprintf("%v,%q,%q", bs.Info.Id, fd, bs.Info.Path)] = struct{}{}
			}
			if _, found := activeBlockServices[fd][bs.Info.Path]; !found {
				missingBlockServices[fmt.Sprintf("%q,%q", fd, bs.Info.Path)] = struct{}{}
			}
		}
		if len(missingBlockServices) == 0 {
			log.ClearNC(replaceDecommedAlert)
		} else {
			bss := make([]string, 0, len(missingBlockServices))
			for bs := range missingBlockServices {
				bss = append(bss, bs)
			}
			sort.Strings(bss)
			log.RaiseNC(replaceDecommedAlert, "decommissioned block services have to be replaced: %s", strings.Join(bss, " "))
		}

		if len(decommedWithFiles) == 0 {
			log.ClearNC(migrateDecommedAlert)
		} else {
			bss := make([]string, 0, len(decommedWithFiles))
			for bs := range decommedWithFiles {
				bss = append(bss, bs)
			}
			sort.Strings(bss)
			log.RaiseNC(migrateDecommedAlert, "decommissioned block services still have files (including transient): %s", strings.Join(bss, " "))
		}
		log.Info("checked block services, sleeping for a minute")
		time.Sleep(time.Minute)
	}
}

func initDb(dbFile string) (*sql.DB, error) {
	db, err := sql.Open("sqlite3", fmt.Sprintf("file:%s?_journal=WAL", dbFile))
	if err != nil {
		return nil, err
	}

	err = initCDCTable(db)

	if err != nil {
		return nil, err
	}

	err = initAndPopulateShardsTable(db)

	if err != nil {
		return nil, err
	}

	err = initBlockServicesTable(db)
	if err != nil {
		return nil, err
	}
	_, err = db.Exec(`CREATE TABLE IF NOT EXISTS stats(
		name TEXT NOT NULL,
		time INT NOT NULL,
		value BLOB NOT NULL,
		PRIMARY KEY (name, time)
	)`)
	if err != nil {
		return nil, err
	}
	_, err = db.Exec("CREATE INDEX IF NOT EXISTS stats_by_time on stats (time, name)")
	if err != nil {
		return nil, err
	}

	return db, nil
}

func initBlockServicesTable(db *sql.DB) error {
	blockServicesDefinition := `(
		id INT NOT NULL PRIMARY KEY,
		ip1 BLOB NOT NULL,
		port1 INT NOT NULL,
		ip2 BLOB NOT NULL,
		port2 INT NOT NULL,
		storage_class INT NOT NULL,
		failure_domain BLOB NOT NULL,
		secret_key BLOB NOT NULL,
		flags INT NOT NULL,
		capacity_bytes INT NOT NULL,
		available_bytes INT NOT NULL,
		blocks INT NOT NULL,
		path TEXT NOT NULL,
		last_seen INT NOT NULL,
		has_files INT NOT NULL
	)`
	_, err := db.Exec(`CREATE TABLE IF NOT EXISTS block_services` + blockServicesDefinition)
	if err != nil {
		return err
	}

	// detect and convert old table format
	row := db.QueryRow(`
		SELECT IIF(sql LIKE '%has_files%', 1, 0)
		FROM sqlite_schema
		WHERE name = 'block_services'
	`)
	var hasNewFormat bool
	err = row.Scan(&hasNewFormat)
	if err != nil {
		return err
	}
	if !hasNewFormat {
		_, err = db.Exec(`CREATE TABLE IF NOT EXISTS block_services_` + blockServicesDefinition)
		if err != nil {
			return err
		}

		_, err = db.Exec(`
			INSERT INTO block_services_ (id, ip1, port1, ip2, port2, storage_class, failure_domain, secret_key, flags, capacity_bytes, available_bytes, blocks, path, last_seen, has_files)
			SELECT id, ip1, port1, ip2, port2, storage_class, failure_domain, secret_key, flags, capacity_bytes, available_bytes, blocks, path, last_seen, 0 FROM block_services
		`)
		if err != nil {
			return err
		}

		_, err = db.Exec(`DROP TABLE block_services`)
		if err != nil {
			return err
		}

		_, err = db.Exec(`ALTER TABLE block_services_ RENAME TO block_services`)
		if err != nil {
			return err
		}
	}

	_, err = db.Exec("CREATE INDEX IF NOT EXISTS last_seen_idx_b on block_services (last_seen)")
	return err
}

func initCDCTable(db *sql.DB) error {
	cdcDefinition := `(
		replica_id INT PRIMARY KEY,
		is_leader BOOL NOT NULL,
		ip1 BLOB NOT NULL,
		port1 INT NOT NULL,
		ip2 BLOB NOT NULL,
		port2 INT NOT NULL,
		last_seen INT NOT NULL
	)`

	_, err := db.Exec(`CREATE TABLE IF NOT EXISTS cdc` + cdcDefinition)
	if err != nil {
		return err
	}

	// detect and convert old cdc table format
	row := db.QueryRow(`
		SELECT IIF(sql LIKE '%replica_id%', 1, 0)
		FROM sqlite_schema
		WHERE name = 'cdc'
	`)
	var hasNewCDCFormat bool
	err = row.Scan(&hasNewCDCFormat)
	if err != nil {
		return err
	}
	if !hasNewCDCFormat {
		_, err = db.Exec(`CREATE TABLE IF NOT EXISTS cdc_` + cdcDefinition)
		if err != nil {
			return err
		}

		_, err = db.Exec(`
			INSERT INTO cdc_
			SELECT 0, false, ifnull(ip1,` + zeroIPString + `), ifnull(port1,0), ifnull(ip2,` + zeroIPString + `), ifnull(port2,0), ifnull(last_seen,0)
			FROM cdc
		`)
		if err != nil {
			return err
		}

		_, err = db.Exec(`DROP TABLE cdc`)
		if err != nil {
			return err
		}

		_, err = db.Exec(`ALTER TABLE cdc_ RENAME TO cdc`)
		if err != nil {
			return err
		}
	}

	// Prepopulate cdc rows to simplify register operation checking if ip has changed
	rows, err := db.Query("SELECT replica_id FROM cdc")
	if err != nil {
		return err
	}
	usedReplicaIds := [5]bool{}
	for rows.Next() {
		var replicaId int
		err = rows.Scan(&replicaId)
		if err != nil {
			return err
		}
		usedReplicaIds[replicaId] = true
	}
	err = rows.Close()
	if err != nil {
		return err
	}

	for replicaId := 0; replicaId < 5; replicaId++ {
		if usedReplicaIds[replicaId] {
			continue
		}
		n := sql.Named
		_, err = db.Exec("INSERT INTO cdc VALUES(:replica_id, :is_leader, "+zeroIPString+", 0, "+zeroIPString+", 0, 0)", n("replica_id", replicaId), n("is_leader", replicaId == 0))
		if err != nil {
			return err
		}
	}

	return nil
}

func initAndPopulateShardsTable(db *sql.DB) error {
	shardsDefinition := `(
		id INT NOT NULL,
		replica_id INT NOT NULL,
		is_leader BOOL NOT NULL,
		ip1 BLOB NOT NULL,
		port1 INT NOT NULL,
		ip2 BLOB NOT NULL,
		port2 INT NOT NULL,
		last_seen INT NOT NULL,
		PRIMARY KEY (id, replica_id)
	)`
	_, err := db.Exec(`CREATE TABLE IF NOT EXISTS shards` + shardsDefinition)
	if err != nil {
		return err
	}

	// detect and convert old shards table format
	row := db.QueryRow(`
		SELECT IIF(sql LIKE '%replica_id%', 1, 0)
		FROM sqlite_schema
		WHERE name = 'shards'
	`)
	var hasNewShardsFormat bool
	err = row.Scan(&hasNewShardsFormat)
	if err != nil {
		return err
	}
	if !hasNewShardsFormat {
		_, err = db.Exec(`CREATE TABLE IF NOT EXISTS shards_` + shardsDefinition)
		if err != nil {
			return err
		}

		_, err = db.Exec(`
			INSERT INTO shards_
			SELECT id, 0, false, ifnull(ip1,` + zeroIPString + `), ifnull(port1,0), ifnull(ip2,` + zeroIPString + `), ifnull(port2,0), ifnull(last_seen,0)
			FROM shards
		`)
		if err != nil {
			return err
		}

		_, err = db.Exec(`DROP TABLE shards`)
		if err != nil {
			return err
		}

		_, err = db.Exec(`ALTER TABLE shards_ RENAME TO shards`)
		if err != nil {
			return err
		}
	}

	_, err = db.Exec("CREATE INDEX IF NOT EXISTS last_seen_idx_s ON shards (last_seen)")
	if err != nil {
		return err
	}

	// Prepopulate shard rows to simplify register operation checking if ip has changed
	rows, err := db.Query("SELECT id, replica_id FROM shards")
	if err != nil {
		return err
	}
	usedShrids := map[msgs.ShardReplicaId]any{}
	for rows.Next() {
		var id, replicaId int
		err = rows.Scan(&id, &replicaId)
		if err != nil {
			return err
		}
		shrid := msgs.MakeShardReplicaId(msgs.ShardId(id), msgs.ReplicaId(replicaId))
		usedShrids[shrid] = nil
	}
	err = rows.Close()
	if err != nil {
		return err
	}

	for id := msgs.MakeShardReplicaId(0, 0); id < msgs.MakeShardReplicaId(0, 5); id++ {
		if _, ok := usedShrids[id]; ok {
			continue
		}
		n := sql.Named
		_, err = db.Exec("INSERT INTO shards VALUES(:id, :replica_id, :is_leader, "+zeroIPString+", 0, "+zeroIPString+", 0, 0)", n("id", id.Shard()), n("replica_id", id.Replica()), n("is_leader", id.Replica() == 0))
		if err != nil {
			return err
		}
	}

	return nil
}

func main() {
	httpPort := flag.Uint("http-port", 10000, "Port on which to run the HTTP server")
	addr1 := flag.String("addr-1", "", "First address to bind bincode server on.")
	addr2 := flag.String("addr-2", "", "Second address to bind bincode server on (optional).")
	logFile := flag.String("log-file", "", "File in which to write logs (or stdout)")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	syslog := flag.Bool("syslog", false, "")
	metrics := flag.Bool("metrics", false, "")
	dataDir := flag.String("data-dir", "", "Where to store the shuckle files")
	// See internal-repo/issues/69 for more info on the default value below.
	bsMinBytes := flag.Uint64("bs-min-bytes", 300<<(10*3), "Minimum free space before marking blockservice NO_WRITES")
	mtu := flag.Uint64("mtu", 0, "")
	stale := flag.Duration("stale", 3*time.Minute, "")
	minDecomInterval := flag.Duration("min-decom-interval", 2*time.Hour, "Minimum interval between calls to decommission blockservices by automation")
	maxDecommedWithFiles := flag.Int("max-decommed-with-files", 3, "Maximum number of migrating decommissioned blockservices before rejecting further automated decommissions")
	scriptsJs := flag.String("scripts-js", "", "")

	flag.Parse()
	noRunawayArgs()

	if *addr1 == "" {
		fmt.Fprintf(os.Stderr, "-addr-1 must be provided.\n")
		os.Exit(2)
	}

	ownIp1, ownPort1, err := lib.ParseIPV4Addr(*addr1)
	if err != nil {
		panic(err)
	}

	var ownIp2 [4]byte
	var ownPort2 uint16
	if *addr2 != "" {
		ownIp2, ownPort2, err = lib.ParseIPV4Addr(*addr2)
		if err != nil {
			panic(err)
		}
	}

	logOut := os.Stdout
	if *logFile != "" {
		var err error
		logOut, err = os.OpenFile(*logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			log.Fatalf("could not open log file %v: %v", *logFile, err)
		}
	}

	level := lib.INFO
	if *verbose {
		level = lib.DEBUG
	}
	if *trace {
		level = lib.TRACE
	}
	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppInstance: "eggsshuckle", AppType: "restech_eggsfs.critical"})

	if *dataDir == "" {
		fmt.Fprintf(os.Stderr, "You need to specify a -data-dir\n")
		os.Exit(2)
	}

	log.Info("Running shuckle with options:")
	log.Info("  addr1 = %s", *addr1)
	log.Info("  addr2 = %s", *addr2)
	log.Info("  httpPort = %v", *httpPort)
	log.Info("  logFile = '%v'", *logFile)
	log.Info("  logLevel = %v", level)
	log.Info("  mtu = %v", *mtu)
	log.Info("  stale = '%v'", *stale)
	log.Info("  dataDir = %s", *dataDir)
	log.Info("  minDecomInterval = '%v'", *minDecomInterval)
	log.Info("  maxDecommedWithFiles = %v", *maxDecommedWithFiles)

	if *mtu != 0 {
		client.SetMTU(*mtu)
	}

	if err := os.Mkdir(*dataDir, 0777); err != nil && !os.IsExist(err) {
		panic(err)
	}
	dbFile := path.Join(*dataDir, "shuckle.db")
	db, err := initDb(dbFile)
	if err != nil {
		panic(err)
	}
	defer db.Close()

	readSpanBufPool = lib.NewBufPool()

	bincodeListener1, err := net.Listen("tcp", fmt.Sprintf("%v:%v", net.IP(ownIp1[:]), ownPort1))
	if err != nil {
		panic(err)
	}
	defer bincodeListener1.Close()

	var bincodeListener2 net.Listener
	if *addr2 != "" {
		var err error
		bincodeListener2, err = net.Listen("tcp", fmt.Sprintf("%v:%v", net.IP(ownIp2[:]), ownPort2))
		if err != nil {
			panic(err)
		}
		defer bincodeListener2.Close()
	}

	httpListener, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%v", *httpPort))
	if err != nil {
		panic(err)
	}
	defer httpListener.Close()

	if bincodeListener2 == nil {
		log.Info("running on %v (HTTP) and %v (bincode)", httpListener.Addr(), bincodeListener1.Addr())
	} else {
		log.Info("running on %v (HTTP) and %v,%v (bincode)", httpListener.Addr(), bincodeListener1.Addr(), bincodeListener2.Addr())
	}

	config := &shuckleConfig{
		ip1:                      ownIp1,
		port1:                    ownPort1,
		ip2:                      ownIp2,
		port2:                    ownPort2,
		blockServiceMinFreeBytes: *bsMinBytes,
		minAutoDecomInterval:     *minDecomInterval,
		maxDecommedWithFiles:     *maxDecommedWithFiles,
	}
	state, err := newState(log, db, config)
	if err != nil {
		panic(err)
	}

	statsWrittenBeforeQuitting := int32(0)
	writeStatsBeforeQuitting := func() {
		if atomic.CompareAndSwapInt32(&statsWrittenBeforeQuitting, 0, 1) {
			writeStats(log, state)
		}
	}
	defer writeStatsBeforeQuitting()
	signalChan := make(chan os.Signal, 1)
	signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
	go func() {
		sig := <-signalChan
		signal.Stop(signalChan)
		writeStatsBeforeQuitting()
		syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
	}()

	setupRouting(log, state, *scriptsJs)

	terminateChan := make(chan any)

	startBincodeHandler := func(listener net.Listener) {
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				conn, err := listener.Accept()
				if err != nil {
					terminateChan <- err
					return
				}
				go func() {
					defer func() { lib.HandleRecoverPanic(log, recover()) }()
					handleRequest(log, state, conn.(*net.TCPConn))
				}()
			}
		}()
	}
	startBincodeHandler(bincodeListener1)
	if bincodeListener2 != nil {
		startBincodeHandler(bincodeListener2)
	}

	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		terminateChan <- http.Serve(httpListener, nil)
	}()

	go func() {
		defer func() { lib.HandleRecoverPanic(log, recover()) }()
		err := serviceMonitor(log, state, *stale)
		log.RaiseAlert("serviceMonitor ended with error %s", err)
	}()

	go func() {
		defer func() { lib.HandleRecoverPanic(log, recover()) }()
		statsWriter(log, state)
	}()

	go func() {
		defer func() { lib.HandleRecoverPanic(log, recover()) }()
		blockServiceAlerts(log, state)
	}()

	go func() {
		defer func() { lib.HandleRecoverPanic(log, recover()) }()
		checkBlockServiceFilePresence(log, state)
	}()

	if *metrics {
		go func() {
			defer func() { lib.HandleRecoverPanic(log, recover()) }()
			sendMetrics(log, state)
		}()
	}

	panic(<-terminateChan)
}
