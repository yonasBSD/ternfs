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
	"net/url"
	"os"
	"os/signal"
	"path"
	"runtime/debug"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"

	_ "github.com/mattn/go-sqlite3"
)

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
	mutex                    sync.Mutex
	db                       *sql.DB
	counters                 map[msgs.ShuckleMessageKind]*lib.Timings
	blockServiceMinFreeBytes uint64
}

func (s *state) selectCDC() (*cdcState, error) {
	var cdc cdcState
	rows, err := s.db.Query("SELECT id, ip1, port1, ip2, port2, last_seen FROM cdc")
	if err != nil {
		return nil, fmt.Errorf("error selecting cdc: %s", err)
	}
	defer rows.Close()

	i := 0
	for rows.Next() {
		if i > 0 {
			return nil, fmt.Errorf("more than 1 cdc row returned from db")
		}
		var id int
		var ip1, ip2 []byte
		err = rows.Scan(&id, &ip1, &cdc.port1, &ip2, &cdc.port2, &cdc.lastSeen)
		if id != 0 {
			return nil, fmt.Errorf("unexpected id %v for cdc (expected 0)", id)
		}
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

	rows, err := s.db.Query("SELECT * FROM shards")
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
		err = rows.Scan(&id, &ip1, &si.Port1, &ip2, &si.Port2, &si.LastSeen)
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

	rows, err := s.db.Query("SELECT * FROM shards WHERE id = :id", n("id", shid))
	if err != nil {
		return nil, fmt.Errorf("error selecting shards: %s", err)
	}
	defer rows.Close()

	for rows.Next() {
		si := msgs.ShardInfo{}
		var ip1, ip2 []byte
		var id int
		err = rows.Scan(&id, &ip1, &si.Port1, &ip2, &si.Port2, &si.LastSeen)
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

func (s *state) selectBlockServices(id *msgs.BlockServiceId) (map[msgs.BlockServiceId]msgs.BlockServiceInfo, error) {
	n := sql.Named

	ret := make(map[msgs.BlockServiceId]msgs.BlockServiceInfo)
	q := "SELECT * FROM block_services"
	args := []any{}
	if id != nil {
		q += " WHERE id = :id"
		args = append(args, n("id", *id))
	}
	rows, err := s.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("error selecting blockServices: %s", err)
	}
	defer rows.Close()

	for rows.Next() {
		bs := msgs.BlockServiceInfo{}
		var ip1, ip2, fd, sk []byte
		err = rows.Scan(&bs.Id, &ip1, &bs.Port1, &ip2, &bs.Port2, &bs.StorageClass, &fd, &sk, &bs.Flags, &bs.CapacityBytes, &bs.AvailableBytes, &bs.Blocks, &bs.Path, &bs.LastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding blockService row: %s", err)
		}

		copy(bs.Ip1[:], ip1)
		copy(bs.Ip2[:], ip2)
		copy(bs.FailureDomain.Name[:], fd)
		copy(bs.SecretKey[:], sk)
		ret[bs.Id] = bs
	}
	return ret, nil
}

func newState(db *sql.DB, minBytes uint64) *state {
	st := &state{
		db:                       db,
		mutex:                    sync.Mutex{},
		blockServiceMinFreeBytes: minBytes,
	}
	st.counters = make(map[msgs.ShuckleMessageKind]*lib.Timings)
	for _, k := range msgs.AllShuckleMessageKind {
		st.counters[k] = lib.NewTimings(40, 10*time.Microsecond, 1.5)
	}
	return st
}

func (st *state) resetTimings() {
	for _, t := range st.counters {
		t.Reset()
	}
}

func handleAllBlockServices(ll *lib.Logger, s *state, req *msgs.AllBlockServicesReq) (*msgs.AllBlockServicesResp, error) {
	resp := msgs.AllBlockServicesResp{}
	blockServices, err := s.selectBlockServices(nil)
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
	blockServices, err := s.selectBlockServices(&req.Id)
	if err != nil {
		return nil, err
	}
	if len(blockServices) > 1 {
		panic(fmt.Errorf("impossible: %v results for id blocks selection", len(blockServices)))
	}
	for _, bs := range blockServices {
		return &msgs.BlockServiceResp{Info: bs}, nil
	}
	return nil, msgs.BLOCK_SERVICE_NOT_FOUND
}

func handleRegisterBlockServices(ll *lib.Logger, s *state, req *msgs.RegisterBlockServicesReq) (*msgs.RegisterBlockServicesResp, error) {
	if len(req.BlockServices) == 0 {
		return &msgs.RegisterBlockServicesResp{}, nil
	}

	now := msgs.Now()
	var fmtBuilder strings.Builder
	fmtBuilder.Write([]byte(`
		INSERT INTO block_services
			(id, ip1, port1, ip2, port2, storage_class, failure_domain, secret_key, flags, capacity_bytes, available_bytes, blocks, path, last_seen)
		VALUES
	`))
	fmtValues := []byte("(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
	values := make([]any, len(req.BlockServices)*14) // 14: number of columns
	values = values[:0]
	for i := range req.BlockServices {
		bs := req.BlockServices[i]
		if bs.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED != 0 {
			return nil, msgs.CANNOT_REGISTER_DECOMMISSIONED
		}
		flags := bs.Flags
		if bs.AvailableBytes < s.blockServiceMinFreeBytes {
			flags = flags | msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE
		}
		// ll.Info("block service %s %v key: %s", string(bs.FailureDomain[:]), bs.Id, hex.EncodeToString(bs.SecretKey[:]))
		values = append(
			values,
			bs.Id, bs.Ip1[:], bs.Port1, bs.Ip2[:], bs.Port2,
			bs.StorageClass, bs.FailureDomain.Name[:],
			bs.SecretKey[:], flags,
			bs.CapacityBytes, bs.AvailableBytes, bs.Blocks,
			bs.Path, now,
		)
		if i > 0 {
			fmtBuilder.Write([]byte(", "))
		}
		fmtBuilder.Write(fmtValues)
	}
	// Never change the storage_class/failure_domain/secret_key/ip/path,
	// see <internal-repo/issues/89>, we might
	// relax the IP restriction in the future but let's be cautious now
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
		ll.RaiseAlert("error settings flags for blockservice %d: %s", req.Id, err)
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

func handleShard(ll *lib.Logger, s *state, req *msgs.ShardReq) (*msgs.ShardResp, error) {
	info, err := s.selectShard(req.Id)
	if err != nil {
		return nil, err
	}
	return &msgs.ShardResp{Info: *info}, nil
}

func handleRegisterShard(ll *lib.Logger, s *state, req *msgs.RegisterShardReq) (*msgs.RegisterShardResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	_, err := s.db.Exec(
		"REPLACE INTO shards(id, ip1, port1, ip2, port2, last_seen) VALUES (:id, :ip1, :port1, :ip2, :port2, :last_seen)",
		n("id", req.Id), n("ip1", req.Info.Ip1[:]), n("port1", req.Info.Port1), n("ip2", req.Info.Ip2[:]), n("port2", req.Info.Port2), n("last_seen", msgs.Now()),
	)
	if err != nil {
		ll.RaiseAlert("error registering shard %d: %s", req.Id, err)
		return nil, err
	}

	return &msgs.RegisterShardResp{}, err
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

func handleRegisterCdc(log *lib.Logger, s *state, req *msgs.RegisterCdcReq) (*msgs.RegisterCdcResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	_, err := s.db.Exec(
		"REPLACE INTO cdc(id, ip1, port1, ip2, port2, queued_txns, current_txn_kind, current_txn_step, last_seen) VALUES (0, :ip1, :port1, :ip2, :port2, :queued_txns, :current_txn_kind, :current_txn_step, :last_seen)",
		n("ip1", req.Ip1[:]), n("port1", req.Port1),
		n("ip2", req.Ip2[:]), n("port2", req.Port2),
		n("queued_txns", 0), n("current_txn_kind", 0), n("current_txn_step", 0), // unused now TODO remove
		n("last_seen", msgs.Now()),
	)
	if err != nil {
		log.RaiseAlert("error registering cdc: %s", err)
		return nil, err
	}

	return &msgs.RegisterCdcResp{}, nil
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
	case *msgs.ShardsReq:
		resp, err = handleShards(log, s, whichReq)
	case *msgs.RegisterShardReq:
		resp, err = handleRegisterShard(log, s, whichReq)
	case *msgs.AllBlockServicesReq:
		resp, err = handleAllBlockServices(log, s, whichReq)
	case *msgs.CdcReq:
		resp, err = handleCdc(log, s, whichReq)
	case *msgs.RegisterCdcReq:
		resp, err = handleRegisterCdc(log, s, whichReq)
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
	log.RaiseAlertStack(1, "got unexpected error %v from %v", err, conn.RemoteAddr())

	// attempt to say goodbye, ignore errors
	if eggsErr, isEggsErr := err.(msgs.ErrCode); isEggsErr {
		lib.WriteShuckleResponseError(log, conn, eggsErr)
		return false
	} else {
		lib.WriteShuckleResponseError(log, conn, msgs.INTERNAL_ERROR)
		return true
	}
}

func handleRequest(log *lib.Logger, s *state, conn *net.TCPConn) {
	defer conn.Close()

	for {
		req, err := lib.ReadShuckleRequest(log, conn)
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
			if err := lib.WriteShuckleResponse(log, conn, resp); err != nil {
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

type indexBlockService struct {
	Id             msgs.BlockServiceId
	Addr1          string
	Addr2          string
	StorageClass   msgs.StorageClass
	Flags          string
	FailureDomain  string
	CapacityBytes  string
	AvailableBytes string
	Path           string
	Blocks         uint64
	LastSeen       string
}

type indexShard struct {
	Addr1    string
	Addr2    string
	LastSeen string
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

			blockServices, err := state.selectBlockServices(nil)
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
	TransientNote string // only if transient
	DownloadLink  string
	AllInline     bool
	Spans         []fileSpan
	PathSegments  []pathSegment
}

//go:embed directory.html
var directoryTemplateStr string

type directoryEdge struct {
	Current      bool
	TargetId     string
	Owned        bool
	NameHash     string
	Name         string
	CreationTime string
	Type         string
	Locked       bool
}

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
	Edges        []directoryEdge
	Info         []directoryInfoEntry
}

func newClient(log *lib.Logger, state *state) (*lib.Client, error) {
	shards, err := state.selectShards()
	if err != nil {
		return nil, fmt.Errorf("error reading shards: %s", err)
	}
	cdc, err := state.selectCDC()
	if err != nil {
		return nil, fmt.Errorf("error reading cdc: %s", err)
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
	client, err := lib.NewClientDirect(log, &cdcIps, &cdcPorts, &shardIps, &shardPorts)
	if err != nil {
		return nil, fmt.Errorf("error creating client: %s", err)
	}
	return client, nil
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

func lookup(log *lib.Logger, client *lib.Client, path string) *msgs.InodeId {
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
			if id != msgs.NULL_INODE_ID && id != msgs.ROOT_DIR_INODE_ID && path == "/" {
				path = "" // path not provided
			}
			if id != msgs.NULL_INODE_ID && id != msgs.ROOT_DIR_INODE_ID && path != "" {
				return errorPage(http.StatusBadRequest, "cannot specify both id and path")
			}
			if id == msgs.ROOT_DIR_INODE_ID && path != "/" && path != "" {
				return errorPage(http.StatusBadRequest, "bad root inode id")
			}
			client, err := newClient(log, state)
			if err != nil {
				panic(err)
			}
			defer client.Close()
			if id == msgs.NULL_INODE_ID {
				mbId := lookup(log, client, path)
				if mbId == nil {
					return errorPage(http.StatusNotFound, fmt.Sprintf("path '%v' not found", path))
				}
				id = *mbId
			}
			if id.Type() == msgs.DIRECTORY {
				data := directoryData{
					Id:   fmt.Sprintf("%v", id),
					Path: "/" + path + "/",
				}
				title := fmt.Sprintf("Directory %v", data.Id)
				{
					resp := msgs.StatDirectoryResp{}
					if err := client.ShardRequest(log, id.Shard(), &msgs.StatDirectoryReq{Id: id}, &resp); err != nil {
						panic(err)
					}
					data.Owner = resp.Owner.String()
					data.Mtime = resp.Mtime.String()
				}
				data.PathSegments = pathSegments(path)
				{
					_, full := query["full"]
					if full {

					} else {
						req := msgs.FullReadDirReq{DirId: id}
						resp := msgs.FullReadDirResp{}
						edges := []msgs.Edge{}
						for {
							if err := client.ShardRequest(log, id.Shard(), &req, &resp); err != nil {
								panic(err)
							}
							edges = append(edges, resp.Results...)
							if resp.Next.StartName == "" {
								break
							}
							req.Flags = 0
							if resp.Next.Current {
								req.Flags = msgs.FULL_READ_DIR_CURRENT
							}
							req.StartName = resp.Next.StartName
							req.StartTime = resp.Next.StartTime
						}
						sort.Slice(
							edges,
							func(i, j int) bool {
								a := edges[i]
								b := edges[j]
								if a.Name != b.Name {
									return a.Name < b.Name
								}
								if a.NameHash != b.NameHash {
									return a.NameHash < b.NameHash
								}
								return a.CreationTime < b.CreationTime
							},
						)
						for _, edge := range edges {
							dataEdge := directoryEdge{
								Current:      edge.Current,
								TargetId:     fmt.Sprintf("%v", edge.TargetId.Id()),
								Owned:        edge.Current || edge.TargetId.Extra(),
								NameHash:     fmt.Sprintf("%016x", edge.NameHash),
								Name:         edge.Name,
								CreationTime: edge.CreationTime.String(),
								Locked:       edge.Current && edge.TargetId.Extra(),
							}
							if edge.TargetId.Id() != msgs.NULL_INODE_ID {
								dataEdge.Type = edge.TargetId.Id().Type().String()
								if edge.TargetId.Id().Type() == msgs.DIRECTORY {
									dataEdge.Name = dataEdge.Name + "/"
								}
							}
							data.Edges = append(data.Edges, dataEdge)
						}
					}
				}
				{
					data.Info = []directoryInfoEntry{}
					dirInfoCache := lib.NewDirInfoCache()
					populateInfo := func(info msgs.IsDirectoryInfoEntry) {
						inheritedFrom, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, id, info)
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
				if len(data.PathSegments) > 0 {
					data.PathSegments = pathSegments(path)
				}
				title := fmt.Sprintf("File %v", data.Id)
				{
					resp := msgs.StatFileResp{}
					err := client.ShardRequest(log, id.Shard(), &msgs.StatFileReq{Id: id}, &resp)
					if err == msgs.FILE_NOT_FOUND {
						transResp := msgs.StatTransientFileResp{}
						transErr := client.ShardRequest(log, id.Shard(), &msgs.StatTransientFileReq{Id: id}, &transResp)
						if transErr == nil {
							data.Mtime = transResp.Mtime.String()
							data.Size = fmt.Sprintf("%v (%v bytes)", formatSize(transResp.Size), transResp.Size)
							data.TransientNote = transResp.Note
						} else {
							panic(err)
						}
					} else if err == nil {
						data.Mtime = resp.Mtime.String()
						data.Size = fmt.Sprintf("%v (%v bytes)", formatSize(resp.Size), resp.Size)
					} else {
						panic(err)
					}
				}
				if len(data.PathSegments) > 0 {
					data.DownloadLink = fmt.Sprintf("/files/%v?name=%v", id, data.PathSegments[len(data.PathSegments)-1].Segment)
				} else {
					data.DownloadLink = fmt.Sprintf("/files/%v", id)
				}
				{
					req := msgs.FileSpansReq{FileId: id}
					resp := msgs.FileSpansResp{}
					for {
						if err := client.ShardRequest(log, id.Shard(), &req, &resp); err != nil {
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
									fb := fileBlock{
										Id:           block.BlockId.String(),
										BlockService: blockService.Id.String(),
										Crc:          block.Crc.String(),
										Link:         fmt.Sprintf("/blocks/%v/%v?size=%v", blockService.Id, block.BlockId, blockSize),
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

				conn, err = lib.BlockServiceConnection(log, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2)
				if err != nil {
					panic(err)
				}
			}
			if err := lib.FetchBlock(log, conn, &blockService, blockId, 0, uint32(size)); err != nil {
				panic(err)
			}
			w.Header().Set("Content-Type", "application/x-binary")
			w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=\"%016x\"", uint64(blockId)))
			log.Info("serving block of size %v", size)
			return conn, int64(size), http.StatusOK
		},
	)
}

type clientSpanReader struct {
	client     *lib.Client
	spanReader io.ReadCloser
}

func (r *clientSpanReader) Read(p []byte) (n int, err error) {
	return r.spanReader.Read(p)
}

func (r *clientSpanReader) Close() (err error) {
	err = r.spanReader.Close()
	r.client.Close()
	return err
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

			client, err := newClient(log, st)
			if err != nil {
				panic(err)
			}

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

			return &clientSpanReader{client: client, spanReader: r}, int64(statResp.Size), http.StatusOK
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

type transientFile struct {
	Shard    int
	Id       string
	Deadline string
	Cookie   string
}

type transientData struct {
	Files []transientFile
}

func handleTransient(log *lib.Logger, st *state, w http.ResponseWriter, r *http.Request) {
	handlePage(
		log, w, r,
		func(query url.Values) (*template.Template, *pageData, int) {
			data := &transientData{Files: []transientFile{}}
			client, err := newClient(log, st)
			if err != nil {
				panic(err)
			}
			for i := 0; i < 256; i++ {
				req := msgs.VisitTransientFilesReq{}
				resp := msgs.VisitTransientFilesResp{}
				for {
					if err := client.ShardRequest(log, msgs.ShardId(i), &req, &resp); err != nil {
						panic(err)
					}
					for _, f := range resp.Files {
						data.Files = append(data.Files, transientFile{
							Shard:    i,
							Id:       f.Id.String(),
							Deadline: f.DeadlineTime.String(),
							Cookie:   fmt.Sprintf("0x%016x", binary.LittleEndian.Uint64(f.Cookie[:])),
						})
					}
					if resp.NextId == msgs.NULL_INODE_ID {
						break
					}
					req.BeginId = resp.NextId
				}
			}
			pd := pageData{
				Title: "transient",
				Body:  data,
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
				client, err := newClient(log, st)
				if err != nil {
					return sendJsonErr(w, err, http.StatusInternalServerError)
				}
				if err := client.CDCRequest(log, req, resp); err != nil {
					return sendJsonErr(w, err, http.StatusInternalServerError)
				}
				return sendResponse(resp)
			case "shard":
				if len(segments) != 4 {
					return badUrl("bad segments len")
				}
				req, resp, err := msgs.MkShardMessage(segments[2])
				if err != nil {
					return badUrl("bad req type")
				}
				shidU, err := strconv.ParseUint(segments[3], 10, 8)
				if err != nil {
					return badUrl("bad shard id")
				}
				shid := msgs.ShardId(shidU)
				if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
					return sendJsonErr(w, fmt.Errorf("could not decode request: %v", err), http.StatusBadRequest)
				}
				client, err := newClient(log, st)
				if err != nil {
					return sendJsonErr(w, err, http.StatusInternalServerError)
				}
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
	staleAlert := ll.NewNCAlert(0)

	for {
		<-ticker.C

		alerts := []string{}
		now := msgs.Now()
		thresh := uint64(now) - uint64(staleDelta.Nanoseconds())

		formatLastSeen := func(t msgs.EggsTime) string {
			return formatNanos(uint64(now) - uint64(t))
		}

		n := sql.Named
		var id uint64
		var ts msgs.EggsTime

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
				"SELECT id, failure_domain, last_seen FROM block_services WHERE last_seen < :thresh AND (flags & ~:flag) == 0",
				n("thresh", thresh),
				// we already know that decommissioned block services are gone
				n("flag", msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
			)
			if err != nil {
				ll.RaiseAlert("error selecting blockServices: %s", err)
				return
			}
			defer rows.Close()

			for rows.Next() {
				var fd []byte
				err = rows.Scan(&id, &fd, &ts)
				if err != nil {
					ll.RaiseAlert("error decoding blockService row: %s", err)
					return
				}
				alerts = append(alerts, fmt.Sprintf("stale blockservice %v, fd %s (seen %s ago)", id, fd, formatLastSeen(ts)))
			}
		}()

		func() {
			rows, err := st.db.Query("SELECT id, last_seen FROM shards where last_seen < :thresh", n("thresh", thresh))
			if err != nil {
				ll.RaiseAlert("error selecting blockServices: %s", err)
				return
			}
			defer rows.Close()

			for rows.Next() {
				err = rows.Scan(&id, &ts)
				if err != nil {
					ll.RaiseAlert("error decoding shard row: %s", err)
					return
				}
				alerts = append(alerts, fmt.Sprintf("stale shard %d (last seen %s ago)", id, formatLastSeen(ts)))
			}
		}()

		func() {
			rows, err := st.db.Query("SELECT last_seen FROM cdc where last_seen < :thresh", n("thresh", thresh))
			if err != nil {
				ll.RaiseAlert("error selecting blockServices: %s", err)
				return
			}
			defer rows.Close()

			for rows.Next() {
				err = rows.Scan(&ts)
				if err != nil {
					ll.RaiseAlert("error decoding cdc row: %s", err)
					return
				}
				alerts = append(alerts, fmt.Sprintf("stale cdc (last seen %s ago)", formatLastSeen(ts)))
			}
		}()

		if len(alerts) == 0 {
			ll.ClearNC(staleAlert)
			continue
		}
		msg := strings.Join(alerts, "\n")
		ll.RaiseNC(staleAlert, msg)
	}
}

func main() {
	httpPort := flag.Uint("http-port", 10000, "Port on which to run the HTTP server")
	bincodePort := flag.Uint("bincode-port", 10001, "Port on which to run the bincode server.")
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
	scriptsJs := flag.String("scripts-js", "", "")
	flag.Parse()
	noRunawayArgs()

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
	ll := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppName: "shuckle", AppType: "restech.critical"})

	if *dataDir == "" {
		fmt.Fprintf(os.Stderr, "You need to specify a -data-dir\n")
		os.Exit(2)
	}

	ll.Info("Running shuckle with options:")
	ll.Info("  bincodePort = %v", *bincodePort)
	ll.Info("  httpPort = %v", *httpPort)
	ll.Info("  logFile = '%v'", *logFile)
	ll.Info("  logLevel = %v", level)
	ll.Info("  mtu = %v", *mtu)
	ll.Info("  stale = '%v'", *stale)
	ll.Info("  dataDir = %s", *dataDir)

	if *mtu != 0 {
		lib.SetMTU(*mtu)
	}

	if err := os.Mkdir(*dataDir, 0777); err != nil && !os.IsExist(err) {
		panic(err)
	}
	dbFile := path.Join(*dataDir, "shuckle.db")
	db, err := sql.Open("sqlite3", fmt.Sprintf("file:%s?_journal=WAL", dbFile))
	if err != nil {
		panic(err)
	}
	defer db.Close()

	_, err = db.Exec(`CREATE TABLE IF NOT EXISTS cdc(
		id INT NOT NULL PRIMARY KEY,
		ip1 BLOB,
		port1 INT,
		ip2 BLOB,
		port2 INT,
		queued_txns INT,
		current_txn_kind INT,
		current_txn_step INT,
		last_seen INT
	)`)
	if err != nil {
		panic(err)
	}
	_, err = db.Exec(`CREATE TABLE IF NOT EXISTS shards(
		id INT NOT NULL PRIMARY KEY,
		ip1 BLOB,
		port1 INT,
		ip2 BLOB,
		port2 INT,
		last_seen INT
	)`)
	if err != nil {
		panic(err)
	}
	_, err = db.Exec("CREATE INDEX IF NOT EXISTS last_seen_idx_s on shards (last_seen)")
	if err != nil {
		panic(err)
	}
	_, err = db.Exec(`CREATE TABLE IF NOT EXISTS block_services(
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
		last_seen INT
	)`)
	if err != nil {
		panic(err)
	}
	_, err = db.Exec("CREATE INDEX IF NOT EXISTS last_seen_idx_b on block_services (last_seen)")
	if err != nil {
		panic(err)
	}

	_, err = db.Exec(`CREATE TABLE IF NOT EXISTS stats(
		name TEXT NOT NULL,
		time INT NOT NULL,
		value BLOB NOT NULL,
		PRIMARY KEY (name, time)
	)`)
	if err != nil {
		panic(err)
	}
	_, err = db.Exec("CREATE INDEX IF NOT EXISTS stats_by_time on stats (time, name)")
	if err != nil {
		panic(err)
	}

	readSpanBufPool = lib.NewBufPool()

	bincodeListener, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%v", *bincodePort))
	if err != nil {
		panic(err)
	}
	defer bincodeListener.Close()

	httpListener, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%v", *httpPort))
	if err != nil {
		panic(err)
	}
	defer httpListener.Close()

	ll.Info("running on %v (HTTP) and %v (bincode)", httpListener.Addr(), bincodeListener.Addr())

	state := newState(db, *bsMinBytes)

	statsWrittenBeforeQuitting := int32(0)
	writeStatsBeforeQuitting := func() {
		if atomic.CompareAndSwapInt32(&statsWrittenBeforeQuitting, 0, 1) {
			writeStats(ll, state)
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

	setupRouting(ll, state, *scriptsJs)

	terminateChan := make(chan any)

	go func() {
		defer func() { lib.HandleRecoverChan(ll, terminateChan, recover()) }()
		for {
			conn, err := bincodeListener.Accept()
			if err != nil {
				terminateChan <- err
				return
			}
			go func() {
				defer func() { lib.HandleRecoverPanic(ll, recover()) }()
				handleRequest(ll, state, conn.(*net.TCPConn))
			}()
		}
	}()

	go func() {
		defer func() { lib.HandleRecoverChan(ll, terminateChan, recover()) }()
		terminateChan <- http.Serve(httpListener, nil)
	}()

	go func() {
		defer func() { lib.HandleRecoverPanic(ll, recover()) }()
		err := serviceMonitor(ll, state, *stale)
		ll.RaiseAlert("serviceMonitor ended with error %s", err)
	}()

	go func() {
		defer func() { lib.HandleRecoverPanic(ll, recover()) }()

	}()

	go func() {
		defer func() { lib.HandleRecoverPanic(ll, recover()) }()
		statsWriter(ll, state)
	}()

	if *metrics {
		go func() {
			defer func() { lib.HandleRecoverPanic(ll, recover()) }()
			sendMetrics(ll, state)
		}()
	}

	panic(<-terminateChan)
}
