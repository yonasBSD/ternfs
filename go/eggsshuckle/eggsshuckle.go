package main

import (
	"bytes"
	"container/heap"
	"context"
	"crypto/aes"
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
	"slices"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/certificate"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"

	_ "github.com/mattn/go-sqlite3"
	"golang.org/x/sync/semaphore"
)

const zeroIPString = "x'00000000'"
const DEFAULT_LOCATION = 0

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
	addrs    msgs.AddrsInfo
	lastSeen msgs.EggsTime
}

type locStorageClass struct {
	locationId   msgs.Location
	storageClass msgs.StorageClass
}

type state struct {
	// we need the mutex since sqlite doesn't like concurrent writes
	mutex     sync.Mutex
	semaphore *semaphore.Weighted
	db        *sql.DB
	counters  map[msgs.ShuckleMessageKind]*lib.Timings
	config    *shuckleConfig
	client    *client.Client

	fsInfoMutex      sync.RWMutex
	fsInfo           msgs.InfoResp
	fsInfoLastUpdate time.Time
	// separate mutex to manage `lastAutoDecom`
	decomMutex    sync.Mutex
	lastAutoDecom time.Time
	// speed up generating proof for decommed block deletion
	decommedBlockServices   map[msgs.BlockServiceId]msgs.BlockServiceInfo
	decommedBlockServicesMu sync.RWMutex
	// periodically calculated evenly spread writable block services across all shards
	currentShardBlockServices   map[msgs.ShardId][]msgs.BlockServiceInfoShort
	currentShardBlockServicesMu sync.RWMutex
	hotSpottingAlerts           map[locStorageClass]*lib.XmonNCAlert
	highLoadAlerts              map[locStorageClass]*lib.XmonNCAlert
	lowCapacityAlerts           map[locStorageClass]*lib.XmonNCAlert
}

func (s *state) selectCDC(locationId msgs.Location) (*cdcState, error) {
	s.semaphore.Acquire(context.Background(), 1)
	defer s.semaphore.Release(1)
	var cdc cdcState
	rows, err := s.db.Query("SELECT ip1, port1, ip2, port2, last_seen FROM cdc WHERE is_leader = true AND location_id = ?", locationId)
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
		err = rows.Scan(&ip1, &cdc.addrs.Addr1.Port, &ip2, &cdc.addrs.Addr2.Port, &cdc.lastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding blockService row: %s", err)
		}
		copy(cdc.addrs.Addr1.Addrs[:], ip1)
		copy(cdc.addrs.Addr2.Addrs[:], ip2)
		i += 1
	}
	return &cdc, nil
}

func (s *state) selectShards(locationId msgs.Location) (*[256]msgs.ShardInfo, error) {
	s.semaphore.Acquire(context.Background(), 1)
	defer s.semaphore.Release(1)
	var ret [256]msgs.ShardInfo

	rows, err := s.db.Query("SELECT * FROM shards WHERE is_leader = true AND location_id = ?", locationId)
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
		var loc msgs.Location
		err = rows.Scan(&id, &replicaId, &loc, &isLeader, &ip1, &si.Addrs.Addr1.Port, &ip2, &si.Addrs.Addr2.Port, &si.LastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding shard row: %s", err)
		}
		copy(si.Addrs.Addr1.Addrs[:], ip1)
		copy(si.Addrs.Addr2.Addrs[:], ip2)
		ret[id] = si
		i += 1
	}

	return &ret, nil
}

type BlockServiceInfoWithLocation struct {
	msgs.BlockServiceInfo
	LocationId msgs.Location
}

// if flagsFilter&flags is non-zero, things won't be returned
func (s *state) selectBlockServices(locationId *msgs.Location, id *msgs.BlockServiceId, flagsFilter msgs.BlockServiceFlags, flagsChangedSince msgs.EggsTime, flagsChangedBefore msgs.EggsTime, filterWithSpaceAvailable bool) (map[msgs.BlockServiceId]BlockServiceInfoWithLocation, error) {
	s.semaphore.Acquire(context.Background(), 1)
	defer s.semaphore.Release(1)
	n := sql.Named

	ret := make(map[msgs.BlockServiceId]BlockServiceInfoWithLocation)
	q := "SELECT * FROM block_services WHERE (flags & :flags) = 0 AND flags_last_changed > :changed_since AND flags_last_changed <= :changed_before"
	if filterWithSpaceAvailable {
		q += " AND available_bytes > 0"
	}
	args := []any{n("flags", flagsFilter), n("changed_since", uint64(flagsChangedSince)), n("changed_before", uint64(flagsChangedBefore))}
	if locationId != nil {
		q += " AND location_id = :location_id"
		args = append(args, n("location_id", locationId))
	}
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
		bs := BlockServiceInfoWithLocation{}
		var ip1, ip2, fd, sk []byte
		err = rows.Scan(&bs.Id, &bs.LocationId, &ip1, &bs.Addrs.Addr1.Port, &ip2, &bs.Addrs.Addr2.Port, &bs.StorageClass, &fd, &sk, &bs.Flags, &bs.CapacityBytes, &bs.AvailableBytes, &bs.Blocks, &bs.Path, &bs.LastSeen, &bs.HasFiles, &bs.FlagsLastChanged)
		if err != nil {
			return nil, fmt.Errorf("error decoding blockService row: %s", err)
		}

		copy(bs.Addrs.Addr1.Addrs[:], ip1)
		copy(bs.Addrs.Addr2.Addrs[:], ip2)
		copy(bs.FailureDomain.Name[:], fd)
		copy(bs.SecretKey[:], sk)
		ret[bs.Id] = bs
	}
	return ret, nil
}

func (s *state) selectLocations() ([]msgs.LocationInfo, error) {
	var res []msgs.LocationInfo
	rows, err := s.db.Query("SELECT id, name FROM location")
	if err != nil {
		return nil, nil
	}
	defer rows.Close()
	for rows.Next() {
		loc := msgs.LocationInfo{}
		err = rows.Scan(&loc.Id, &loc.Name)
		if err != nil {
			return nil, nil
		}
		res = append(res, loc)
	}
	return res, nil
}

func (s *state) selectNonReadableFailureDomains(locationId msgs.Location) ([]string, error) {

	bs, err := s.selectBlockServices(&locationId, nil, 0, 0, msgs.Now(), false)
	if err != nil {
		return nil, err
	}
	ret := map[string]msgs.BlockServiceFlags{}
	for _, v := range bs {
		if !v.HasFiles {
			continue
		}
		var flags msgs.BlockServiceFlags
		switch {
		case v.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED == msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED:
			flags = msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED
		case v.Flags&msgs.EGGSFS_BLOCK_SERVICE_NO_READ == msgs.EGGSFS_BLOCK_SERVICE_NO_READ:
			flags = msgs.EGGSFS_BLOCK_SERVICE_NO_READ
		case v.Flags&msgs.EGGSFS_BLOCK_SERVICE_STALE == msgs.EGGSFS_BLOCK_SERVICE_STALE:
			flags = msgs.EGGSFS_BLOCK_SERVICE_STALE
		}
		if flags != 0 {
			ret[string(v.FailureDomain.Name[:])] |= flags
		}
	}
	var retList []string
	for k, v := range ret {
		retList = append(retList, fmt.Sprintf("%s:(%v)", strings.TrimSpace(k), v.String()))
	}

	if retList != nil {
		sort.Strings(retList)
	}

	return retList, nil
}

type shuckleConfig struct {
	addrs                                   msgs.AddrsInfo
	minAutoDecomInterval                    time.Duration
	maxNonReadableFailureDomainsPerLocation int
	maxFailureDomainsPerShard               int
	currentBlockServiceUpdateInterval       time.Duration
	blockServiceDelay                       time.Duration
}

func newState(
	log *lib.Logger,
	db *sql.DB,
	conf *shuckleConfig,
) (*state, error) {
	st := &state{
		db:                          db,
		mutex:                       sync.Mutex{},
		semaphore:                   semaphore.NewWeighted(1000),
		fsInfoMutex:                 sync.RWMutex{},
		fsInfoLastUpdate:            time.Time{},
		fsInfo:                      msgs.InfoResp{},
		config:                      conf,
		decommedBlockServices:       make(map[msgs.BlockServiceId]msgs.BlockServiceInfo),
		decommedBlockServicesMu:     sync.RWMutex{},
		currentShardBlockServices:   make(map[msgs.ShardId][]msgs.BlockServiceInfoShort),
		currentShardBlockServicesMu: sync.RWMutex{},
		hotSpottingAlerts:           make(map[locStorageClass]*lib.XmonNCAlert),
		highLoadAlerts:              make(map[locStorageClass]*lib.XmonNCAlert),
		lowCapacityAlerts:           make(map[locStorageClass]*lib.XmonNCAlert),
	}

	blockServices, err := st.selectBlockServices(nil, nil, 0, 0, msgs.Now(), false)
	if err != nil {
		return nil, err
	}
	for id, bs := range blockServices {
		if bs.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED == 0 {
			continue
		}
		st.decommedBlockServices[id] = bs.BlockServiceInfo
	}
	st.counters = make(map[msgs.ShuckleMessageKind]*lib.Timings)
	for _, k := range msgs.AllShuckleMessageKind {
		st.counters[k] = lib.NewTimings(40, 10*time.Microsecond, 1.5)
	}

	st.client, err = client.NewClientDirectNoAddrs(log, msgs.AddrsInfo{})
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
	if _, err := handleInfoReq(log, st, &msgs.InfoReq{}); err != nil {
		return nil, err
	}
	return st, err
}

func (st *state) resetTimings() {
	for _, t := range st.counters {
		t.Reset()
	}
}

type shardFailureDomain struct {
	shard  msgs.ShardId
	domain msgs.FailureDomain
}

type shardBlockServiceCount struct {
	shard msgs.ShardId
	count int
}

type currentBlockServicesStats struct {
	blockServicesConsidered               int
	maxBlockServicesPerShard              int
	minBlockServicesPerShard              int
	duplicateBlockServicesAssigned        int
	maxTimesBlockServicePicked            int
	minTimesBlockServicePicked            int
	maxLowBlockCapacityBlockServicePicked int
	minLowBlockCapacityBlockServicePicked int
}

type failureDomainInfo struct {
	id            msgs.FailureDomain
	bsIdx         int
	blockServices []BlockServiceInfoWithLocation
	shuffleHash   uint64
}

type failureDomainPQ []*failureDomainInfo

func (pq failureDomainPQ) Len() int { return len(pq) }

// We first try to minimize the number of times a block service ends up being picked (number of cycles)
// Then we try to first assign block services with the most available bytes
// Then we try to assign block services in a random order (shuffleHash) to avoid assigning the same block service to the same shards every time
func (pq failureDomainPQ) Less(i, j int) bool {
	pqIcyclesCompleted := pq[i].bsIdx / len(pq[i].blockServices)
	pqJcyclesCompleted := pq[j].bsIdx / len(pq[j].blockServices)
	if pqIcyclesCompleted != pqJcyclesCompleted {
		return pqIcyclesCompleted < pqJcyclesCompleted
	}

	pqIbytesAvailable := pq[i].blockServices[pq[i].bsIdx%len(pq[i].blockServices)].AvailableBytes
	pqJbytesAvailable := pq[j].blockServices[pq[j].bsIdx%len(pq[j].blockServices)].AvailableBytes
	if pqIbytesAvailable != pqJbytesAvailable {
		return pqJbytesAvailable < pqIbytesAvailable
	}
	return pq[i].shuffleHash < pq[j].shuffleHash
}

func (pq *failureDomainPQ) Swap(i, j int) {
	(*pq)[i], (*pq)[j] = (*pq)[j], (*pq)[i]
}

func (pq *failureDomainPQ) Push(x any) {
	*pq = append(*pq, x.(*failureDomainInfo))
}

func (pq *failureDomainPQ) Pop() any {
	old := *pq
	n := len(old)
	x := old[n-1]
	*pq = old[0 : n-1]
	return x
}

func assignWritableBlockServicesToShards(log *lib.Logger, s *state) {
	updatesSkipped := 0
	lastBlockServicesUsed := map[msgs.BlockServiceId]struct{}{}
	for {
		newBlockServicesUsed := map[msgs.BlockServiceId]struct{}{}
		blockServicesMap, err := s.selectBlockServices(nil, nil, msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE|msgs.EGGSFS_BLOCK_SERVICE_STALE|msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED, 0, msgs.MakeEggsTime(time.Now().Add(-s.config.blockServiceDelay)), true)
		if err != nil {
			log.RaiseAlert("failed to select block services: %v", err)
		}
		needsRecalculate := false
		for bsId, _ := range blockServicesMap {
			newBlockServicesUsed[bsId] = struct{}{}
			if needsRecalculate {
				continue
			}
			if _, ok := lastBlockServicesUsed[bsId]; !ok {
				needsRecalculate = true
			}
		}
		if !needsRecalculate {
			for bsId, _ := range lastBlockServicesUsed {
				if _, ok := blockServicesMap[bsId]; !ok {
					needsRecalculate = true
					break
				}
			}
		}
		// if nothing changed since we last calculated shard block services, nothing to do
		if !needsRecalculate && 10*time.Second*time.Duration(updatesSkipped) < s.config.currentBlockServiceUpdateInterval {
			updatesSkipped++
			time.Sleep(10 * time.Second)
			continue
		}
		lastBlockServicesUsed = newBlockServicesUsed
		updatesSkipped = 0

		currentShardBlockServices := map[msgs.ShardId][]msgs.BlockServiceInfoShort{}

		locations, err := s.selectLocations()
		if err != nil {
			log.RaiseAlert("failed to select locations: %v", err)
			continue
		}
		for _, location := range locations {
			for _, storageClass := range []msgs.StorageClass{msgs.FLASH_STORAGE, msgs.HDD_STORAGE} {
				// divide them by failure domain
				blockServicesByFailureDomain := make(map[msgs.FailureDomain][]BlockServiceInfoWithLocation)
				failureDomains := []msgs.FailureDomain{}
				stats := currentBlockServicesStats{
					minBlockServicesPerShard:              len(blockServicesMap),
					minTimesBlockServicePicked:            256,
					maxTimesBlockServicePicked:            1,
					maxLowBlockCapacityBlockServicePicked: 0,
					minLowBlockCapacityBlockServicePicked: len(blockServicesMap),
				}
				pq := failureDomainPQ{}
				for _, bs := range blockServicesMap {
					if bs.StorageClass != storageClass || bs.LocationId != location.Id {
						continue
					}
					blockServices, ok := blockServicesByFailureDomain[bs.FailureDomain]
					if !ok {
						failureDomains = append(failureDomains, bs.FailureDomain)
					}
					blockServicesByFailureDomain[bs.FailureDomain] = append(blockServices, bs)
					stats.blockServicesConsidered++
				}

				// check minimum number of failure domainsavailable.  we try to get at least 28 different if available
				// we hard fail if there are less than 14
				minFailureDomains := 28
				if minFailureDomains > len(blockServicesByFailureDomain) {
					minFailureDomains = len(blockServicesByFailureDomain)
				}

				if len(blockServicesByFailureDomain) < 14 {
					if location.Id == DEFAULT_LOCATION {
						log.RaiseAlert("could not pick block services for storage class %v. there are only %d failure domains and we need at least 14", storageClass, len(blockServicesByFailureDomain))
					} else {
						log.Info("could not pick block services for location %v and storage class %v. there are only %d failure domains and we need at least 14", location.Name, storageClass, len(blockServicesByFailureDomain))
					}
					continue
				}

				for _, failureDomain := range failureDomains {
					blockServices := blockServicesByFailureDomain[failureDomain]
					// sort them by bytes available in descending order to prefer higher available capacity when assigning
					slices.SortFunc[[]BlockServiceInfoWithLocation](blockServices, func(a, b BlockServiceInfoWithLocation) int {
						return int(b.AvailableBytes) - int(a.AvailableBytes)
					})
					pq = failureDomainPQ(append([]*failureDomainInfo(pq), &failureDomainInfo{failureDomain, 0, blockServices, rand.Uint64()}))
				}
				heap.Init(&pq)
				perStorageClassShardBlockServices := map[msgs.ShardId][]msgs.BlockServiceInfoShort{}
				shardFailureDomainSet := map[shardFailureDomain]struct{}{}
				blockServiceSet := map[msgs.BlockServiceId]int{}
				lowCapacityBlockServicePerShardCount := make([]int, 256)

				// as we want to assign capacity fairly iterate over shards minFailureDomain times
				for failureDomainCount := 0; failureDomainCount < minFailureDomains; failureDomainCount++ {
					for idx := 0; idx < 256; idx++ {
						var usedBlockServices []*failureDomainInfo
						shardId := msgs.ShardId(idx)

						failureDomain := heap.Pop(&pq).(*failureDomainInfo)
						usedBlockServices = append(usedBlockServices, failureDomain)
						// we might have used this failure domain, find one we have not
						for _, ok := shardFailureDomainSet[shardFailureDomain{shardId, failureDomain.id}]; ok; _, ok = shardFailureDomainSet[shardFailureDomain{shardId, failureDomain.id}] {
							failureDomain = heap.Pop(&pq).(*failureDomainInfo)
							usedBlockServices = append(usedBlockServices, failureDomain)
						}
						shardFailureDomainSet[shardFailureDomain{shardId, failureDomain.id}] = struct{}{}

						blockServiceInfo := failureDomain.blockServices[failureDomain.bsIdx%len(failureDomain.blockServices)]
						failureDomain.bsIdx++
						perStorageClassShardBlockServices[shardId] = append(perStorageClassShardBlockServices[shardId], msgs.BlockServiceInfoShort{blockServiceInfo.LocationId, blockServiceInfo.FailureDomain, blockServiceInfo.Id, blockServiceInfo.StorageClass})
						// log distribution of services with lesss than 500GB available
						if blockServiceInfo.AvailableBytes < 500000000000 {
							lowCapacityBlockServicePerShardCount[shardId]++
						}

						if _, ok := blockServiceSet[blockServiceInfo.Id]; !ok {
							blockServiceSet[blockServiceInfo.Id] = 1
						} else {
							blockServiceSet[blockServiceInfo.Id] += 1
							stats.maxTimesBlockServicePicked = max(blockServiceSet[blockServiceInfo.Id], stats.maxTimesBlockServicePicked)
							stats.duplicateBlockServicesAssigned++
						}
						if usedBlockServices != nil {
							pq = failureDomainPQ(append([]*failureDomainInfo(pq), usedBlockServices...))
							heap.Init(&pq)
						}
					}
				}

				// assign any remaining block services
				for _, failureDomain := range pq {
					blockServices := failureDomain.blockServices
					candidateBsIdx := failureDomain.bsIdx
					if candidateBsIdx >= len(blockServices) {
						continue
					}
					candidateShards := []shardBlockServiceCount{}
					for i := 0; i < 256; i++ {
						if _, ok := shardFailureDomainSet[shardFailureDomain{shard: msgs.ShardId(i), domain: failureDomain.id}]; !ok && len(perStorageClassShardBlockServices[msgs.ShardId(i)]) < s.config.maxFailureDomainsPerShard {
							candidateShards = append(candidateShards, shardBlockServiceCount{shard: msgs.ShardId(i), count: len(perStorageClassShardBlockServices[msgs.ShardId(i)])})
						}
					}
					sort.Slice(candidateShards, func(i, j int) bool { return candidateShards[i].count < candidateShards[j].count })

					for _, shard := range candidateShards {
						if candidateBsIdx >= len(blockServices) {
							break
						}
						blockServiceInfo := blockServices[candidateBsIdx]
						if _, ok := blockServiceSet[blockServiceInfo.Id]; !ok {
							blockServiceSet[blockServiceInfo.Id] = 1
						} else {
							panic(fmt.Sprintf("duplicate block service %s, this should not happen in this part of assigment", blockServiceInfo.Id))
						}
						perStorageClassShardBlockServices[shard.shard] = append(perStorageClassShardBlockServices[shard.shard], msgs.BlockServiceInfoShort{blockServiceInfo.LocationId, blockServiceInfo.FailureDomain, blockServiceInfo.Id, blockServiceInfo.StorageClass})
						candidateBsIdx++
						shardFailureDomainSet[shardFailureDomain{shard: shard.shard, domain: failureDomain.id}] = struct{}{}
					}
				}
				for shard, blockServices := range perStorageClassShardBlockServices {
					if len(blockServices) > stats.maxBlockServicesPerShard {
						stats.maxBlockServicesPerShard = len(blockServices)
					}
					if len(blockServices) < stats.minBlockServicesPerShard {
						stats.minBlockServicesPerShard = len(blockServices)
					}
					currentShardBlockServices[shard] = append(currentShardBlockServices[shard], blockServices...)
				}

				for _, count := range blockServiceSet {
					stats.minTimesBlockServicePicked = min(count, stats.minTimesBlockServicePicked)
				}
				for _, count := range lowCapacityBlockServicePerShardCount {
					stats.maxLowBlockCapacityBlockServicePicked = max(count, stats.maxLowBlockCapacityBlockServicePicked)
					stats.minLowBlockCapacityBlockServicePicked = min(count, stats.minLowBlockCapacityBlockServicePicked)
				}
				if stats.minTimesBlockServicePicked*5 < stats.maxTimesBlockServicePicked {
					alert, ok := s.hotSpottingAlerts[locStorageClass{location.Id, storageClass}]
					if !ok {
						alert = log.NewNCAlert(0)
						alert.SetAppType(lib.XMON_NEVER)
						s.hotSpottingAlerts[locStorageClass{location.Id, storageClass}] = alert
					}
					log.RaiseNC(alert, "hot spotting block services in location %v, storage class %v. Min times picked: %d, max times picked: %d", location.Name, storageClass, stats.minTimesBlockServicePicked, stats.maxTimesBlockServicePicked)
				} else {
					if alert, ok := s.hotSpottingAlerts[locStorageClass{location.Id, storageClass}]; ok {
						log.ClearNC(alert)
					}
				}
				if stats.minTimesBlockServicePicked > 10 {
					alert, ok := s.highLoadAlerts[locStorageClass{location.Id, storageClass}]
					if !ok {
						alert = log.NewNCAlert(0)
						alert.SetAppType(lib.XMON_NEVER)
						s.highLoadAlerts[locStorageClass{location.Id, storageClass}] = alert
					}
					log.RaiseNC(alert, "high load on block services in location %v, storage class %v. Min times picked: %d", location.Name, storageClass, stats.minTimesBlockServicePicked)
				} else {
					if alert, ok := s.highLoadAlerts[locStorageClass{location.Id, storageClass}]; ok {
						log.ClearNC(alert)
					}
				}
				if stats.maxLowBlockCapacityBlockServicePicked > 3 {
					alert, ok := s.lowCapacityAlerts[locStorageClass{location.Id, storageClass}]
					if !ok {
						alert = log.NewNCAlert(0)
						alert.SetAppType(lib.XMON_NEVER)
						s.lowCapacityAlerts[locStorageClass{location.Id, storageClass}] = alert
					}
					log.RaiseNC(alert, "high number of low capacity block services per shard in location %v, storage class %v. Max times picked: %d", location.Name, storageClass, stats.maxLowBlockCapacityBlockServicePicked)
				} else {
					if alert, ok := s.lowCapacityAlerts[locStorageClass{location.Id, storageClass}]; ok {
						log.ClearNC(alert)
					}
				}

				log.Info("finished calculating current block services for location %v, storage class %v: block service stats {considered: %d, assigned: %d, duplicate: %d, minShard: %d, maxShard: %d, minTimesPicked: %d, maxTimesPicked: %d, minLowCpacityBlockService: %d, maxLowCapacityBlockService: %d}",
					location.Name, storageClass, stats.blockServicesConsidered, len(blockServiceSet), stats.duplicateBlockServicesAssigned, stats.minBlockServicesPerShard, stats.maxBlockServicesPerShard, stats.minTimesBlockServicePicked, stats.maxTimesBlockServicePicked, stats.minLowBlockCapacityBlockServicePicked, stats.maxLowBlockCapacityBlockServicePicked)
			}
		}
		s.currentShardBlockServicesMu.Lock()
		s.currentShardBlockServices = currentShardBlockServices
		s.currentShardBlockServicesMu.Unlock()
	}
}

func handleAllBlockServices(ll *lib.Logger, s *state, req *msgs.AllBlockServicesDeprecatedReq) (*msgs.AllBlockServicesDeprecatedResp, error) {
	resp := msgs.AllBlockServicesDeprecatedResp{}
	blockServices, err := s.selectBlockServices(nil, nil, 0, 0, msgs.Now(), false)
	if err != nil {
		ll.RaiseAlert("error reading block services: %s", err)
		return nil, err
	}

	resp.BlockServices = make([]msgs.BlockServiceInfo, len(blockServices))
	i := 0
	for _, bs := range blockServices {
		resp.BlockServices[i] = bs.BlockServiceInfo
		i++
	}

	return &resp, nil
}

func handleLocalChangedBlockServices(ll *lib.Logger, s *state, req *msgs.LocalChangedBlockServicesReq) (*msgs.LocalChangedBlockServicesResp, error) {
	reqAtLocation := &msgs.ChangedBlockServicesAtLocationReq{DEFAULT_LOCATION, req.ChangedSince}
	respAtLocation, err := handleChangedBlockServicesAtLocation(ll, s, reqAtLocation)
	if err != nil {
		return nil, err
	}
	return &msgs.LocalChangedBlockServicesResp{respAtLocation.LastChange, respAtLocation.BlockServices}, nil
}

func handleChangedBlockServicesAtLocation(ll *lib.Logger, s *state, req *msgs.ChangedBlockServicesAtLocationReq) (*msgs.ChangedBlockServicesAtLocationResp, error) {
	resp := msgs.ChangedBlockServicesAtLocationResp{}

	blockServices, err := s.selectBlockServices(&req.LocationId, nil, 0, req.ChangedSince, msgs.Now(), false)
	if err != nil {
		ll.RaiseAlert("error reading block services: %s", err)
		return nil, err
	}

	resp.BlockServices = make([]msgs.BlockService, len(blockServices))
	// 10 minutes ago to allow shard propagation path to pick up the change and not override it
	resp.LastChange = msgs.MakeEggsTime(time.Now().Add(-10 * time.Minute))
	i := 0
	for _, bs := range blockServices {
		resp.BlockServices[i].Id = bs.Id
		resp.BlockServices[i].Addrs = bs.Addrs
		resp.BlockServices[i].Flags = bs.Flags
		i++
	}
	return &resp, nil
}

func handleRegisterBlockServices(ll *lib.Logger, s *state, req *msgs.RegisterBlockServicesReq) (*msgs.RegisterBlockServicesResp, error) {
	if len(req.BlockServices) == 0 {
		return &msgs.RegisterBlockServicesResp{}, nil
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()

	tx, err := s.db.Begin()
	if err != nil {
		return nil, err
	}
	defer tx.Rollback()

	now := msgs.Now()
	has_files := false

	for i := range req.BlockServices {
		bs := req.BlockServices[i]
		if bs.Id == 0 {
			ll.RaiseAlert("cannot register block service zero, dropping request")
			return nil, msgs.INTERNAL_ERROR
		}
		// DECOMMISSIONED|STALE are always managed externally, not by eggsblocks.
		if msgs.BlockServiceFlags(bs.FlagsMask).HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED | msgs.EGGSFS_BLOCK_SERVICE_STALE) {
			return nil, msgs.CANNOT_REGISTER_DECOMMISSIONED_OR_STALE
		}

		res, err := tx.Exec(`
				INSERT INTO block_services
					(id, location_id, ip1, port1, ip2, port2, storage_class, failure_domain, secret_key, flags, capacity_bytes, available_bytes, blocks, path, last_seen, has_files, flags_last_changed)
				VALUES
					(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
				ON CONFLICT DO UPDATE SET
					ip1 = excluded.ip1,
					port1 = excluded.port1,
					ip2 = excluded.ip2,
					port2 = excluded.port2,
					flags =
						IIF(
							(flags & ?) <> 0,
							flags,
							((flags & ~?) | (excluded.flags & ?))
						),
					capacity_bytes = excluded.capacity_bytes,
					available_bytes = excluded.available_bytes,
					blocks = excluded.blocks,
					last_seen = excluded.last_seen,
					path = excluded.path,
					flags_last_changed =
						IIF(
							(
								((flags & ?) <> 0) OR
								(((flags & ~?) | (excluded.flags & ?)) = flags) OR
								(ip1 <> excluded.ip1 OR ip2 <> excluded.ip2)
							),
							flags_last_changed,
							?
						)
				WHERE`+
			// we allow update if one of the new addresses matches old addresses
			`
					(
						(ip1 = excluded.ip1 AND port1 = excluded.port1) OR (ip1 = excluded.ip2 AND port1 = excluded.port2) OR
						(port2 <> 0 AND (ip2 = excluded.ip1 AND port2 = excluded.port1) OR ( ip2 = excluded.ip2 AND port2 = excluded.port2))
					) AND
					storage_class = excluded.storage_class AND
					failure_domain = excluded.failure_domain AND
					secret_key = excluded.secret_key
			`,
			bs.Id, bs.LocationId, bs.Addrs.Addr1.Addrs[:], bs.Addrs.Addr1.Port, bs.Addrs.Addr2.Addrs[:], bs.Addrs.Addr2.Port,
			bs.StorageClass, bs.FailureDomain.Name[:],
			bs.SecretKey[:], bs.Flags,
			bs.CapacityBytes, bs.AvailableBytes, bs.Blocks,
			bs.Path, now, has_files, now,
			uint8(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
			bs.FlagsMask|uint8(msgs.EGGSFS_BLOCK_SERVICE_STALE), // remove staleness -- we've just updated the BS
			bs.FlagsMask,
			uint8(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
			bs.FlagsMask|uint8(msgs.EGGSFS_BLOCK_SERVICE_STALE),
			bs.FlagsMask,
			now,
		)
		if err != nil {
			return nil, err
		}
		rowsAffected, err := res.RowsAffected()
		if err != nil {
			panic(err)
		}
		if int(rowsAffected) != 1 {
			ll.RaiseAlert("could not update block service %v (expected 1 row, updated %v), it probably has inconsistent info, check logs", bs.Id, rowsAffected)
			return nil, msgs.INCONSISTENT_BLOCK_SERVICE_REGISTRATION
		}
	}

	if err := tx.Commit(); err != nil {
		return nil, err
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
		blockServices, err := s.selectBlockServices(nil, nil, 0, 0, msgs.Now(), false)
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
			bsId := bs.Id
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

func handleSetBlockserviceDecommissioned(ll *lib.Logger, s *state, req *msgs.DecommissionBlockServiceReq) (*msgs.DecommissionBlockServiceResp, error) {
	// This lock effectively prevents decom requests running in parallel
	// we don't want to take any chances here.
	s.decomMutex.Lock()
	defer s.decomMutex.Unlock()
	ld := time.Since(s.lastAutoDecom)
	if ld < s.config.minAutoDecomInterval {
		return nil, msgs.AUTO_DECOMMISSION_FORBIDDEN
	}

	blockServiceInfo, err := s.selectBlockServices(nil, &req.Id, 0, 0, msgs.Now(), false)
	if err != nil {
		return nil, err
	}

	if len(blockServiceInfo) != 1 {
		return nil, msgs.BLOCK_SERVICE_NOT_FOUND
	}

	bsInfo := blockServiceInfo[req.Id]
	if bsInfo.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) {
		return &msgs.DecommissionBlockServiceResp{}, nil
	}

	nonReadableFailureDomains, err := s.selectNonReadableFailureDomains(bsInfo.LocationId)
	if err != nil {
		return nil, err
	}
	nonReadableFailureDomainsCount := len(nonReadableFailureDomains)
	newFailureDomain := true
	for _, fd := range nonReadableFailureDomains {
		if fd == bsInfo.FailureDomain.String() {
			newFailureDomain = false
			break
		}
	}
	if newFailureDomain {
		nonReadableFailureDomainsCount++
	}

	if nonReadableFailureDomainsCount >= s.config.maxNonReadableFailureDomainsPerLocation {
		return nil, msgs.AUTO_DECOMMISSION_FORBIDDEN
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
	return &msgs.DecommissionBlockServiceResp{}, nil
}

func handleSetBlockServiceFlags(ll *lib.Logger, s *state, req *msgs.SetBlockServiceFlagsReq) (*msgs.SetBlockServiceFlagsResp, error) {
	// Special case: the DECOMMISSIONED flag can never be unset, we assume that in
	// a couple of cases (e.g. when fetching block services in shuckle)
	if (req.FlagsMask&uint8(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED)) != 0 && (req.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) == 0 {
		return nil, msgs.CANNOT_UNSET_DECOMMISSIONED
	}
	if req.FlagsMask&uint8(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) != 0 {
		req.Flags = msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED
		req.FlagsMask = uint8(msgs.EGGSFS_BLOCK_SERVICE_MASK_ALL)
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	res, err := s.db.Exec(
		"UPDATE block_services SET flags = ((flags & ~:mask) | (:flags & :mask)), flags_last_changed = :flags_last_changed WHERE id = :id",
		n("flags", req.Flags), n("mask", req.FlagsMask), n("id", req.Id), n("flags_last_changed", uint64(msgs.Now())),
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
	if (req.FlagsMask&uint8(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED)) != 0 && (req.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) != 0 {
		s.decommedBlockServicesMu.Lock()
		defer s.decommedBlockServicesMu.Unlock()
		bs, err := s.selectBlockServices(nil, &req.Id, 0, 0, msgs.Now(), false)
		if err != nil {
			ll.RaiseAlert("couldnt select block service after decommissioning")
		} else {
			s.decommedBlockServices[req.Id] = bs[req.Id].BlockServiceInfo
		}
	}
	return &msgs.SetBlockServiceFlagsResp{}, nil
}

func handleLocalShards(ll *lib.Logger, s *state, _ *msgs.LocalShardsReq) (*msgs.LocalShardsResp, error) {
	reqAtLocation := &msgs.ShardsAtLocationReq{DEFAULT_LOCATION}
	resp, err := handleShardsAtLocation(ll, s, reqAtLocation)
	if err != nil {
		return nil, err
	}
	return &msgs.LocalShardsResp{resp.Shards}, nil
}

func handleShardsAtLocation(ll *lib.Logger, s *state, req *msgs.ShardsAtLocationReq) (*msgs.ShardsAtLocationResp, error) {
	resp := msgs.ShardsAtLocationResp{}

	shards, err := s.selectShards(req.LocationId)
	if err != nil {
		ll.RaiseAlert("error reading shards: %s", err)
		return nil, err
	}

	resp.Shards = shards[:]
	return &resp, nil
}

func handleAllShards(ll *lib.Logger, s *state, _ *msgs.AllShardsReq) (*msgs.AllShardsResp, error) {
	s.semaphore.Acquire(context.Background(), 1)
	defer s.semaphore.Release(1)
	ret := []msgs.FullShardInfo{}

	rows, err := s.db.Query("SELECT * FROM shards")
	if err != nil {
		ll.RaiseAlert("error reading shards: %s", err)
		return nil, fmt.Errorf("error selecting shards: %s", err)
	}
	defer rows.Close()

	for rows.Next() {
		si := msgs.FullShardInfo{}
		var ip1, ip2 []byte
		var id int
		var replicaId int
		err = rows.Scan(&id, &replicaId, &si.LocationId, &si.IsLeader, &ip1, &si.Addrs.Addr1.Port, &ip2, &si.Addrs.Addr2.Port, &si.LastSeen)
		if err != nil {
			return nil, fmt.Errorf("error decoding shard row: %s", err)
		}
		si.Id = msgs.MakeShardReplicaId(msgs.ShardId(id), msgs.ReplicaId(replicaId))
		copy(si.Addrs.Addr1.Addrs[:], ip1)
		copy(si.Addrs.Addr2.Addrs[:], ip2)
		ret = append(ret, si)
	}

	return &msgs.AllShardsResp{Shards: ret}, nil
}

func handleRegisterShard(ll *lib.Logger, s *state, req *msgs.RegisterShardReq) (*msgs.RegisterShardResp, error) {
	var err error = nil
	defer func() {
		if err != nil {
			ll.RaiseAlert("error registering shard %d: %s", req.Shrid, err)
		}
	}()
	if req.Shrid.Replica() > 4 {
		err = msgs.INVALID_REPLICA
		return nil, err
	}
	if req.Addrs.Addr1.Port == 0 {
		err = msgs.MALFORMED_REQUEST
		return nil, err
	}
	if req.Addrs.Addr1 == req.Addrs.Addr2 && req.Addrs.Addr1.Port == req.Addrs.Addr2.Port {
		err = msgs.MALFORMED_REQUEST
		return nil, err
	}
	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := sql.Named
	res, err := s.db.Exec(`
		UPDATE shards
		SET ip1 = :ip1, port1 = :port1, ip2 = :ip2, port2 = :port2, last_seen = :last_seen
		WHERE
		id = :id AND replica_id = :replica_id AND is_leader = :is_leader AND location_id = :location_id AND`+
		// we allow update if ip1 was never set (first registration)
		// or one of the new addresses matches old addresses
		`
		(
			(ip1 = `+zeroIPString+` AND port1 = 0) OR
			(ip1 = :ip1 AND port1 = :port1) OR (ip1 = :ip2 AND port1 = :port2) OR
			(port2 <> 0 AND (ip2 = :ip1 AND port2 = :port1) OR ( ip2 = :ip2 AND port2 = :port2))
		)
		`, n("id", req.Shrid.Shard()), n("replica_id", req.Shrid.Replica()), n("is_leader", req.IsLeader), n("location_id", req.Location), n("ip1", req.Addrs.Addr1.Addrs[:]), n("port1", req.Addrs.Addr1.Port), n("ip2", req.Addrs.Addr2.Addrs[:]), n("port2", req.Addrs.Addr2.Port), n("last_seen", msgs.Now()),
	)
	if err != nil {
		return nil, err
	}
	rowsAffected, err := res.RowsAffected()
	if err != nil {
		return nil, err
	}
	if rowsAffected > 1 {
		panic(fmt.Errorf("more than one row in shards with shardReplicaId %s", req.Shrid))
	}
	if rowsAffected == 0 {
		err = msgs.DIFFERENT_ADDRS_INFO
		return nil, err
	}
	err = refreshClient(ll, s)
	if err != nil {
		return nil, err
	}
	return &msgs.RegisterShardResp{}, nil
}

func handleCdcAtLocation(log *lib.Logger, s *state, req *msgs.CdcAtLocationReq) (*msgs.CdcAtLocationResp, error) {
	resp := msgs.CdcAtLocationResp{}
	cdc, err := s.selectCDC(req.LocationId)
	if err != nil {
		log.RaiseAlert("error reading cdc: %s", err)
		return nil, err
	}
	resp.Addrs = cdc.addrs
	resp.LastSeen = cdc.lastSeen

	return &resp, nil
}

func handleLocalCdc(log *lib.Logger, s *state, req *msgs.LocalCdcReq) (*msgs.LocalCdcResp, error) {
	reqAtLocation := &msgs.CdcAtLocationReq{LocationId: 0}
	respAtLocation, err := handleCdcAtLocation(log, s, reqAtLocation)
	if err != nil {
		return nil, err
	}

	return &msgs.LocalCdcResp{respAtLocation.Addrs, respAtLocation.LastSeen}, nil
}

func handleAllCdc(log *lib.Logger, s *state, req *msgs.AllCdcReq) (*msgs.AllCdcResp, error) {
	s.semaphore.Acquire(context.Background(), 1)
	defer s.semaphore.Release(1)
	var ret []msgs.CdcInfo
	rows, err := s.db.Query("SELECT replica_id, ip1, port1, ip2, port2, last_seen, is_leader, location_id FROM cdc")
	if err != nil {
		return nil, fmt.Errorf("error selecting cdc replicas: %s", err)
	}
	defer rows.Close()

	for rows.Next() {
		si := msgs.CdcInfo{}
		var ip1, ip2 []byte
		err = rows.Scan(&si.ReplicaId, &ip1, &si.Addrs.Addr1.Port, &ip2, &si.Addrs.Addr2.Port, &si.LastSeen, &si.IsLeader, &si.LocationId)
		if err != nil {
			return nil, fmt.Errorf("error decoding cdc row: %s", err)
		}
		copy(si.Addrs.Addr1.Addrs[:], ip1)
		copy(si.Addrs.Addr2.Addrs[:], ip2)
		ret = append(ret, si)
	}

	return &msgs.AllCdcResp{Replicas: ret}, nil
}

func handleCdcReplicas(log *lib.Logger, s *state, req *msgs.CdcReplicasDEPRECATEDReq) (*msgs.CdcReplicasDEPRECATEDResp, error) {
	s.semaphore.Acquire(context.Background(), 1)
	defer s.semaphore.Release(1)
	var ret [5]msgs.AddrsInfo
	rows, err := s.db.Query("SELECT replica_id, ip1, port1, ip2, port2 FROM cdc WHERE location_id = 0")
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
		err = rows.Scan(&replicaId, &ip1, &si.Addr1.Port, &ip2, &si.Addr2.Port)
		if err != nil {
			return nil, fmt.Errorf("error decoding cdc row: %s", err)
		}
		copy(si.Addr1.Addrs[:], ip1)
		copy(si.Addr2.Addrs[:], ip2)
		ret[replicaId] = si
		i += 1
	}

	return &msgs.CdcReplicasDEPRECATEDResp{Replicas: ret[:]}, nil
}

func handleRegisterCDC(log *lib.Logger, s *state, req *msgs.RegisterCdcReq) (resp *msgs.RegisterCdcResp, err error) {
	defer func() {
		if err != nil {
			log.RaiseAlert("error registering cdc: %s", err)
		}
	}()
	if req.Replica > 4 {
		err = msgs.INVALID_REPLICA
	} else if req.Addrs.Addr1.Port == 0 {
		err = msgs.MALFORMED_REQUEST
	} else if req.Addrs.Addr1 == req.Addrs.Addr2 && req.Addrs.Addr1.Port == req.Addrs.Addr2.Port {
		err = msgs.MALFORMED_REQUEST

	} else {
		s.mutex.Lock()
		defer s.mutex.Unlock()

		n := sql.Named
		res, err := s.db.Exec(`
			UPDATE cdc
			SET ip1 = :ip1, port1 = :port1, ip2 = :ip2, port2 = :port2, last_seen = :last_seen
			WHERE replica_id = :replica_id AND is_leader = :is_leader AND location_id = :location_id AND`+
			// we allow update if ip1 was never set (first registration)
			// or one of the new addresses matches old addresses
			`
			(
				(ip1 = `+zeroIPString+` AND port1 = 0) OR
				(ip1 = :ip1 AND port1 = :port1) OR (ip1 = :ip2 AND port1 = :port2) OR
				(port2 <> 0 AND (ip2 = :ip1 AND port2 = :port1) OR ( ip2 = :ip2 AND port2 = :port2))
			)`,
			n("replica_id", req.Replica),
			n("location_id", req.Location),
			n("is_leader", req.IsLeader),
			n("ip1", req.Addrs.Addr1.Addrs[:]), n("port1", req.Addrs.Addr1.Port),
			n("ip2", req.Addrs.Addr2.Addrs[:]), n("port2", req.Addrs.Addr2.Port),
			n("last_seen", msgs.Now()),
		)
		if err == nil {
			rowsAffected, err := res.RowsAffected()
			if err == nil {
				if rowsAffected > 1 {
					panic(fmt.Errorf("more than one row in cdc with replicaId %v", req.Replica))
				}
				if rowsAffected == 0 {
					err = msgs.DIFFERENT_ADDRS_INFO
				} else {
					err = refreshClient(log, s)
				}
			}
		}
	}
	if err == nil {
		resp = &msgs.RegisterCdcResp{}
	}
	return
}

func handleInfoReq(log *lib.Logger, s *state, req *msgs.InfoReq) (*msgs.InfoResp, error) {
	var resp *msgs.InfoResp
	func() {
		s.fsInfoMutex.RLock()
		defer s.fsInfoMutex.RUnlock()
		if time.Since(s.fsInfoLastUpdate) < time.Minute {
			resp = &s.fsInfo
		}
	}()
	if resp != nil {
		return resp, nil
	}

	err := func() error {
		s.fsInfoMutex.Lock()
		defer s.fsInfoMutex.Unlock()
		if time.Since(s.fsInfoLastUpdate) < time.Minute {
			resp = &s.fsInfo
			return nil
		}
		s.fsInfo = msgs.InfoResp{}
		rows, err := s.db.Query(
			`
			SELECT
				count(*),
				count(distinct failure_domain),
				ifnull(sum(CASE WHEN (flags&:decommissioned) = 0 THEN capacity_bytes ELSE 0 END), 0),
				ifnull(sum(CASE WHEN (flags&:decommissioned) = 0 THEN available_bytes ELSE 0 END), 0),
				ifnull(sum(CASE WHEN (flags&:decommissioned) = 0 THEN blocks ELSE 0 END),0)
			FROM block_services
			`,
			sql.Named("decommissioned", msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
		)
		if err != nil {
			log.RaiseAlert("error getting info: %s", err)
			return err
		}
		defer rows.Close()
		if !rows.Next() {
			err = fmt.Errorf("no row found in info query")
			return err
		}
		if err = rows.Scan(&s.fsInfo.NumBlockServices, &s.fsInfo.NumFailureDomains, &s.fsInfo.Capacity, &s.fsInfo.Available, &s.fsInfo.Blocks); err != nil {
			log.RaiseAlert("error scanning info: %s", err)
			return err
		}
		s.fsInfoLastUpdate = time.Now()
		resp = &s.fsInfo
		return nil

	}()
	if err != nil {
		return nil, err
	}

	return resp, nil
}

func handleShardBlockServicesDEPRECATED(log *lib.Logger, s *state, req *msgs.ShardBlockServicesDEPRECATEDReq) (*msgs.ShardBlockServicesDEPRECATEDResp, error) {
	resp, err := handleShardBlockServices(log, s, &msgs.ShardBlockServicesReq{req.ShardId})
	if err != nil {
		return nil, err
	}
	deprecatedResp := &msgs.ShardBlockServicesDEPRECATEDResp{}
	for _, bs := range resp.BlockServices {
		if bs.LocationId != 0 {
			continue
		}
		deprecatedResp.BlockServices = append(deprecatedResp.BlockServices, bs.Id)
	}
	return deprecatedResp, nil
}

func handleShardBlockServices(log *lib.Logger, s *state, req *msgs.ShardBlockServicesReq) (*msgs.ShardBlockServicesResp, error) {
	s.currentShardBlockServicesMu.RLock()
	defer s.currentShardBlockServicesMu.RUnlock()
	currentBlockServices, ok := s.currentShardBlockServices[req.ShardId]
	if !ok {
		return &msgs.ShardBlockServicesResp{}, nil
	}
	return &msgs.ShardBlockServicesResp{currentBlockServices}, nil
}

func handleMoveShardLeader(log *lib.Logger, s *state, req *msgs.MoveShardLeaderReq) (*msgs.MoveShardLeaderResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	n := sql.Named
	res, err := s.db.Exec(
		"UPDATE shards SET is_leader = (replica_id = :replica_id) WHERE id = :id AND location_id = :location_id",
		n("id", req.Shrid.Shard()), n("replica_id", req.Shrid.Replica()), n("location_id", req.Location),
	)
	if err != nil {
		panic(err)
	}
	rowsAffected, err := res.RowsAffected()
	if err != nil {
		panic(err)
	}
	if rowsAffected != 5 {
		panic(fmt.Errorf("unusual number of rows affected (%d) when changing leader to %s", rowsAffected, req.Shrid))
	}

	return &msgs.MoveShardLeaderResp{}, nil
}

func handleMoveCDCLeader(log *lib.Logger, s *state, req *msgs.MoveCdcLeaderReq) (*msgs.MoveCdcLeaderResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	n := sql.Named
	res, err := s.db.Exec(
		"UPDATE cdc SET is_leader = (replica_id = :replica_id) WHERE location_id = :location_id",
		n("replica_id", req.Replica), n("location_id", req.Location),
	)
	if err != nil {
		panic(err)
	}
	rowsAffected, err := res.RowsAffected()
	if err != nil {
		panic(err)
	}
	if rowsAffected != 5 {
		panic(fmt.Errorf("unusual number of rows affected (%d) when changing CDC leader to %v", rowsAffected, req.Replica))
	}

	return &msgs.MoveCdcLeaderResp{}, nil
}

func handleCreateLocation(log *lib.Logger, s *state, req *msgs.CreateLocationReq) (*msgs.CreateLocationResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	if _, err := s.db.Exec("INSERT INTO location (id, name) VALUES (?, ?)", req.Id, req.Name); err != nil {
		return nil, err
	}
	if err := populateCDCTableForLocation(s.db, req.Id); err != nil {
		return nil, err
	}
	if err := populateShardsTableForLocation(s.db, req.Id); err != nil {
		return nil, err
	}
	log.Info("created location id: %v, name: %v", req.Id, req.Name)
	return &msgs.CreateLocationResp{}, nil
}

func handleRenameLocation(log *lib.Logger, s *state, req *msgs.RenameLocationReq) (*msgs.RenameLocationResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	res, err := s.db.Exec("UPDATE location SET name = ? WHERE id = ?", req.Name, req.Id)
	if err != nil {
		return nil, err
	}
	rowsAffected, err := res.RowsAffected()
	if err != nil {
		return nil, err
	}
	if rowsAffected == 0 {
		return nil, fmt.Errorf("no such location: %d", req.Id)
	}
	log.Info("renamed location %d to %s", req.Id, req.Name)
	return &msgs.RenameLocationResp{}, nil
}

func handleLocations(log *lib.Logger, s *state, req *msgs.LocationsReq) (*msgs.LocationsResp, error) {
	resp := &msgs.LocationsResp{}
	rows, err := s.db.Query("SELECT id, name FROM location")
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	for rows.Next() {
		loc := msgs.LocationInfo{}
		err = rows.Scan(&loc.Id, &loc.Name)
		if err != nil {
			return nil, fmt.Errorf("error decoding location row: %s", err)
		}
		resp.Locations = append(resp.Locations, loc)
	}
	return resp, nil

}

func handleClearShardInfo(log *lib.Logger, s *state, req *msgs.ClearShardInfoReq) (*msgs.ClearShardInfoResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	n := sql.Named
	res, err := s.db.Exec(`
		UPDATE shards
		SET ip1 = `+zeroIPString+`, port1 = 0, ip2 = `+zeroIPString+`, port2 = 0, last_seen = 0
		WHERE id = :id AND replica_id = :replica_id AND is_leader = false AND location_id = :location_id
		`,
		n("id", req.Shrid.Shard()), n("replica_id", req.Shrid.Replica()), n("location_id", req.Location),
	)
	if err != nil {
		panic(err)
	}
	rowsAffected, err := res.RowsAffected()
	if err != nil {
		panic(err)
	}
	if rowsAffected != 1 {
		panic(fmt.Errorf("unusual number of rows affected (%d) when executing ClearShardInfoReq for %s", rowsAffected, req.Shrid))
	}

	return &msgs.ClearShardInfoResp{}, nil
}

func handleClearCDCInfo(log *lib.Logger, s *state, req *msgs.ClearCdcInfoReq) (*msgs.ClearCdcInfoResp, error) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	n := sql.Named
	res, err := s.db.Exec(`
		UPDATE cdc
		SET ip1 = `+zeroIPString+`, port1 = 0, ip2 = `+zeroIPString+`, port2 = 0, last_seen = 0
		WHERE replica_id = :replica_id AND is_leader = false AND location_id = :location_id
		`,
		n("replica_id", req.Replica), n("location_id", req.Location),
	)
	if err != nil {
		panic(err)
	}
	rowsAffected, err := res.RowsAffected()
	if err != nil {
		panic(err)
	}
	if rowsAffected != 1 {
		panic(fmt.Errorf("unusual number of rows affected (%d) when executing ClearCDCInfoReq for %v", rowsAffected, req.Replica))
	}

	return &msgs.ClearCdcInfoResp{}, nil
}

func handleEraseDecomissionedBlockReq(log *lib.Logger, s *state, req *msgs.EraseDecommissionedBlockReq) (*msgs.EraseDecommissionedBlockResp, error) {
	s.decommedBlockServicesMu.RLock()
	defer s.decommedBlockServicesMu.RUnlock()

	blockService, ok := s.decommedBlockServices[req.BlockServiceId]
	if !ok || blockService.Flags&msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED == 0 {
		log.RaiseAlert("got an EraseDecommissionedBlockReq for a non-decommissioned block service %v", req.BlockServiceId)
		return nil, fmt.Errorf("block service not decommissioned %v", req.BlockServiceId)
	}
	key, err := aes.NewCipher(blockService.SecretKey[:])
	if err != nil {
		return nil, fmt.Errorf("failed creating cipher for block service %v", blockService.Id)
	}
	expectedMac, good := certificate.CheckBlockEraseCertificate(req.BlockServiceId, key, &msgs.EraseBlockReq{BlockId: req.BlockId, Certificate: req.Certificate})
	if !good {
		log.RaiseAlert("bad MAC, got %v, expected %v", req.Certificate, expectedMac)
		return nil, msgs.BAD_CERTIFICATE
	}
	return &msgs.EraseDecommissionedBlockResp{Proof: certificate.BlockEraseProof(req.BlockServiceId, req.BlockId, key)}, nil
}

func handleShuckle(log *lib.Logger, s *state) (*msgs.ShuckleResp, error) {
	resp := &msgs.ShuckleResp{
		Addrs: s.config.addrs,
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
	case *msgs.SetBlockServiceFlagsReq:
		resp, err = handleSetBlockServiceFlags(log, s, whichReq)
	case *msgs.DecommissionBlockServiceReq:
		resp, err = handleSetBlockserviceDecommissioned(log, s, whichReq)
	case *msgs.LocalShardsReq:
		resp, err = handleLocalShards(log, s, whichReq)
	case *msgs.AllShardsReq:
		resp, err = handleAllShards(log, s, whichReq)
	case *msgs.RegisterShardReq:
		resp, err = handleRegisterShard(log, s, whichReq)
	case *msgs.AllBlockServicesDeprecatedReq:
		resp, err = handleAllBlockServices(log, s, whichReq)
	case *msgs.LocalChangedBlockServicesReq:
		resp, err = handleLocalChangedBlockServices(log, s, whichReq)
	case *msgs.LocalCdcReq:
		resp, err = handleLocalCdc(log, s, whichReq)
	case *msgs.AllCdcReq:
		resp, err = handleAllCdc(log, s, whichReq)
	case *msgs.CdcReplicasDEPRECATEDReq:
		resp, err = handleCdcReplicas(log, s, whichReq)
	case *msgs.RegisterCdcReq:
		resp, err = handleRegisterCDC(log, s, whichReq)
	case *msgs.InfoReq:
		resp, err = handleInfoReq(log, s, whichReq)
	case *msgs.ShuckleReq:
		resp, err = handleShuckle(log, s)
	case *msgs.ShardBlockServicesDEPRECATEDReq:
		resp, err = handleShardBlockServicesDEPRECATED(log, s, whichReq)
	case *msgs.MoveShardLeaderReq:
		resp, err = handleMoveShardLeader(log, s, whichReq)
	case *msgs.ClearShardInfoReq:
		resp, err = handleClearShardInfo(log, s, whichReq)
	case *msgs.ClearCdcInfoReq:
		resp, err = handleClearCDCInfo(log, s, whichReq)
	case *msgs.EraseDecommissionedBlockReq:
		resp, err = handleEraseDecomissionedBlockReq(log, s, whichReq)
	case *msgs.MoveCdcLeaderReq:
		resp, err = handleMoveCDCLeader(log, s, whichReq)
	case *msgs.CreateLocationReq:
		resp, err = handleCreateLocation(log, s, whichReq)
	case *msgs.RenameLocationReq:
		resp, err = handleRenameLocation(log, s, whichReq)
	case *msgs.LocationsReq:
		resp, err = handleLocations(log, s, whichReq)
	case *msgs.RegisterBlockServicesReq:
		resp, err = handleRegisterBlockServices(log, s, whichReq)
	case *msgs.CdcAtLocationReq:
		resp, err = handleCdcAtLocation(log, s, whichReq)
	case *msgs.ChangedBlockServicesAtLocationReq:
		resp, err = handleChangedBlockServicesAtLocation(log, s, whichReq)
	case *msgs.ShardsAtLocationReq:
		resp, err = handleShardsAtLocation(log, s, whichReq)
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

func writeShuckleResponseError(log *lib.Logger, w io.Writer, err msgs.EggsError) error {
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

	if err != msgs.AUTO_DECOMMISSION_FORBIDDEN {
		log.RaiseAlertStack("", 1, "got unexpected error %v from %v", err, conn.RemoteAddr())
	}

	// attempt to say goodbye, ignore errors
	if eggsErr, isEggsErr := err.(msgs.EggsError); isEggsErr {
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
	case msgs.LOCAL_SHARDS:
		req = &msgs.LocalShardsReq{}
	case msgs.REGISTER_SHARD:
		req = &msgs.RegisterShardReq{}
	case msgs.ALL_BLOCK_SERVICES_DEPRECATED:
		req = &msgs.AllBlockServicesDeprecatedReq{}
	case msgs.LOCAL_CHANGED_BLOCK_SERVICES:
		req = &msgs.LocalChangedBlockServicesReq{}
	case msgs.DECOMMISSION_BLOCK_SERVICE:
		req = &msgs.DecommissionBlockServiceReq{}
	case msgs.SET_BLOCK_SERVICE_FLAGS:
		req = &msgs.SetBlockServiceFlagsReq{}
	case msgs.REGISTER_CDC:
		req = &msgs.RegisterCdcReq{}
	case msgs.LOCAL_CDC:
		req = &msgs.LocalCdcReq{}
	case msgs.CDC_REPLICAS_DE_PR_EC_AT_ED:
		req = &msgs.CdcReplicasDEPRECATEDReq{}
	case msgs.INFO:
		req = &msgs.InfoReq{}
	case msgs.SHUCKLE:
		req = &msgs.ShuckleReq{}
	case msgs.SHARD_BLOCK_SERVICES_DE_PR_EC_AT_ED:
		req = &msgs.ShardBlockServicesDEPRECATEDReq{}
	case msgs.ERASE_DECOMMISSIONED_BLOCK:
		req = &msgs.EraseDecommissionedBlockReq{}
	case msgs.ALL_CDC:
		req = &msgs.AllCdcReq{}
	case msgs.CLEAR_SHARD_INFO:
		req = &msgs.ClearShardInfoReq{}
	case msgs.MOVE_SHARD_LEADER:
		req = &msgs.MoveShardLeaderReq{}
	case msgs.ALL_SHARDS:
		req = &msgs.AllShardsReq{}
	case msgs.MOVE_CDC_LEADER:
		req = &msgs.MoveCdcLeaderReq{}
	case msgs.CLEAR_CDC_INFO:
		req = &msgs.ClearCdcInfoReq{}
	case msgs.CREATE_LOCATION:
		req = &msgs.CreateLocationReq{}
	case msgs.RENAME_LOCATION:
		req = &msgs.RenameLocationReq{}
	case msgs.LOCATIONS:
		req = &msgs.LocationsReq{}
	case msgs.REGISTER_BLOCK_SERVICES:
		req = &msgs.RegisterBlockServicesReq{}
	case msgs.CDC_AT_LOCATION:
		req = &msgs.CdcAtLocationReq{}
	case msgs.CHANGED_BLOCK_SERVICES_AT_LOCATION:
		req = &msgs.ChangedBlockServicesAtLocationReq{}
	case msgs.SHARDS_AT_LOCATION:
		req = &msgs.ShardsAtLocationReq{}
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
			content, size, status = sendPage(errorPage(http.StatusMethodNotAllowed, fmt.Sprintf("Only %v allowed", *methodAllowed)))
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

			blockServices, err := state.selectBlockServices(nil, nil, 0, 0, msgs.Now(), false)
			if err != nil {
				ll.RaiseAlert("error reading block services: %s", err)
				return errorPage(http.StatusInternalServerError, fmt.Sprintf("error reading block services: %s", err))
			}

			data := indexData{
				NumBlockServices: len(blockServices),
			}
			totalCapacityBytes := uint64(0)
			totalAvailableBytes := uint64(0)
			failureDomainsBytes := make(map[string]struct{})

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
	shards, err := state.selectShards(0)
	if err != nil {
		return fmt.Errorf("error reading shards: %s", err)
	}
	cdc, err := state.selectCDC(0)
	if err != nil {
		return fmt.Errorf("error reading cdc: %s", err)
	}

	var shardAddrs [256]msgs.AddrsInfo

	for i, si := range shards {
		shardAddrs[i] = si.Addrs
	}
	state.client.SetAddrs(cdc.addrs, &shardAddrs)
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
			eggsErr, ok := err.(msgs.EggsError)
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
					req := msgs.LocalFileSpansReq{FileId: id}
					resp := msgs.LocalFileSpansResp{}
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
										names1, _ := net.LookupAddr(net.IP(blockService.Addrs.Addr1.Addrs[:]).String())
										names2 := []string{}
										if blockService.Addrs.Addr2.Addrs != [4]byte{0, 0, 0, 0} {
											names2, _ = net.LookupAddr(net.IP(blockService.Addrs.Addr2.Addrs[:]).String())
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
				if err := rows.Scan(&ip1, &blockService.Addrs.Addr1.Port, &ip2, &blockService.Addrs.Addr2.Port, &blockService.Flags); err != nil {
					return sendPage(errorPage(http.StatusInternalServerError, fmt.Sprintf("Error reading block service: %s", err)))
				}
				blockService.Id = blockServiceId
				copy(blockService.Addrs.Addr1.Addrs[:], ip1[:])
				copy(blockService.Addrs.Addr2.Addrs[:], ip2[:])

				conn, err = client.BlockServiceConnection(log, blockService.Addrs)
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
	eggsErr, ok := err.(msgs.EggsError)
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

func serviceMonitor(ll *lib.Logger, st *state, staleDelta time.Duration) error {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()

	staleBlockServicesAlerts := make(map[msgs.BlockServiceId]*lib.XmonNCAlert)
	staleShardsAlerts := make(map[msgs.ShardId]*lib.XmonNCAlert)
	staleCDCAlert := ll.NewNCAlert(0)
	staleCDCAlert.SetAppType(lib.XMON_DAYTIME)

	gracePeriodThreshold := uint64(msgs.Now())

	for {
		<-ticker.C

		now := msgs.Now()
		thresh := uint64(now) - uint64(staleDelta.Nanoseconds())

		if thresh < gracePeriodThreshold {
			continue
		}

		formatLastSeen := func(t msgs.EggsTime) string {
			return formatNanos(uint64(now) - uint64(t))
		}

		n := sql.Named
		func() {
			st.mutex.Lock()
			defer st.mutex.Unlock()
			_, err := st.db.Exec(
				`UPDATE
					block_services
				SET
					flags = ((flags & ~:flag) | :flag),
					flags_last_changed = :flags_last_changed
				WHERE
					last_seen < :thresh
					AND flags <> ((flags & ~:flag) | :flag)
					AND (flags & :decom_flag == 0)`,
				n("flag", msgs.EGGSFS_BLOCK_SERVICE_STALE),
				n("flags_last_changed", uint64(now)),
				n("thresh", thresh),
				n("decom_flag", msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
			)
			if err != nil {
				ll.RaiseAlert("error setting block services stale: %s", err)
			}
		}()

		// Wrap the following select statements in func() to make defer work properly.
		func() {
			st.semaphore.Acquire(context.Background(), 1)
			defer st.semaphore.Release(1)
			rows, err := st.db.Query(
				"SELECT id, failure_domain, last_seen FROM block_services WHERE last_seen < :thresh AND (flags & :d_flag) == 0 AND NOT ((flags & :nr_nw_flags) == :nr_nw_flags)",
				n("thresh", thresh),
				// we already know that decommissioned block services are gone
				n("d_flag", msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED),
				// if it was manually marked as non read/non write it's most likely being worked on. no point to alert
				n("nr_nw_flags", msgs.EGGSFS_BLOCK_SERVICE_NO_READ|msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE),
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
				ll.RaiseNC(alert, "stale blockservice %v, fd %s (seen %s ago)", bsId, fd, formatLastSeen(ts))
			}
			for bsId, alert := range staleBlockServicesAlerts {
				if _, found := badBlockServices[bsId]; !found {
					ll.ClearNC(alert)
					delete(staleBlockServicesAlerts, bsId)
				}
			}
		}()

		func() {
			st.semaphore.Acquire(context.Background(), 1)
			defer st.semaphore.Release(1)

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
			st.semaphore.Acquire(context.Background(), 1)
			defer st.semaphore.Release(1)
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
	nonReadableFailureDomainsAlert := log.NewNCAlert(0)
	appType := lib.XMON_NEVER
	nonReadableFailureDomainsAlert.SetAppType(appType)

	for {
		blockServices, err := s.selectBlockServices(nil, nil, 0, 0, msgs.Now(), false)
		if err != nil {
			log.RaiseNC(replaceDecommedAlert, "error reading block services, will try again in one second: %s", err)
			time.Sleep(time.Second)
			continue
		}
		// collect non-decommissioned block services
		// failure domain -> path
		activeBlockServices := make(map[string]map[string]struct{})
		for _, bs := range blockServices {
			if bs.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) {
				continue
			}
			fd := bs.FailureDomain.String()
			if _, found := activeBlockServices[fd]; !found {
				activeBlockServices[fd] = make(map[string]struct{})
			}
			if _, alreadyPresent := activeBlockServices[fd][bs.Path]; alreadyPresent {
				log.RaiseAlert("found duplicate block service at path %q for failure domain %q", bs.Path, fd)
				continue
			}
			activeBlockServices[fd][bs.Path] = struct{}{}
		}
		// collect non-replaced decommissioned block services
		missingBlockServices := make(map[string]struct{})
		decommedWithFiles := make(map[string]struct{})
		for _, bs := range blockServices {
			if !bs.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) {
				continue
			}
			fd := bs.FailureDomain.String()
			// transient files should go away within 36 hours of decommissioning
			if bs.HasFiles && bs.FlagsLastChanged < msgs.MakeEggsTime(time.Now().Add(-36*time.Hour)) {
				decommedWithFiles[fmt.Sprintf("%v,%q,%q", bs.Id, fd, bs.Path)] = struct{}{}
			}

			if _, found := activeBlockServices[fd]; !found {
				// don't alert for whole servers down
				continue
			}

			if _, found := activeBlockServices[fd][bs.Path]; !found {
				if _, found := activeBlockServices[fd][fmt.Sprintf("%s:%s",fd,bs.Path)]; !found {
					missingBlockServices[fmt.Sprintf("%q,%q", fd, bs.Path)] = struct{}{}
				}
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
			alertString := fmt.Sprintf("decommissioned block services have to be replaced: %s", strings.Join(bss, " "))
			if len(alertString) > 10000 {
				alertString = alertString[:10000] + "...."
			}
			log.RaiseNC(replaceDecommedAlert, alertString)
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

		locations, err := s.selectLocations()
		if err != nil {
			nonReadableFailureDomainsAlert.SetAppType(lib.XMON_CRITICAL)
			log.RaiseNC(nonReadableFailureDomainsAlert, "error reading locations when trying to detect non readable failure domains: %s", err)

		} else {
			nonReadableFailureDomainAlertText := ""
			newAppType := lib.XMON_NEVER

			for _, loc := range locations {
				nonReadableFailureDomains, err := s.selectNonReadableFailureDomains(loc.Id)
				if err != nil {
					newAppType = lib.XMON_DAYTIME
					nonReadableFailureDomainAlertText += fmt.Sprintf("\nerror reading non readable failure domains for location %s: %s", loc.Name, err)
					continue
				}
				if len(nonReadableFailureDomains) > 0 {
					if len(nonReadableFailureDomains) > s.config.maxNonReadableFailureDomainsPerLocation {
						newAppType = lib.XMON_DAYTIME
					}
					nonReadableFailureDomainAlertText += fmt.Sprintf("\nlocation %s : non-readable failure domains %v", loc.Name, nonReadableFailureDomains)
				}
			}
			if nonReadableFailureDomainAlertText != "" {
				if appType != newAppType {
					log.ClearNC(nonReadableFailureDomainsAlert)
					appType = newAppType
					nonReadableFailureDomainsAlert.SetAppType(appType)
				}
				log.RaiseNC(nonReadableFailureDomainsAlert, "detected non readable failure domains:%s", nonReadableFailureDomainAlertText)
			} else {
				log.ClearNC(nonReadableFailureDomainsAlert)
				appType = newAppType
				nonReadableFailureDomainsAlert.SetAppType(appType)
			}
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

	err = initLocationTable(db)
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

	return db, nil
}

func initLocationTable(db *sql.DB) error {
	_, err := db.Exec(`CREATE TABLE IF NOT EXISTS location(
		id INT NOT NULL PRIMARY KEY,
		name TEXT NOT NULL
	)`)
	if err != nil {
		return err
	}

	// populate default location if it does not exist
	_, err = db.Exec("INSERT OR IGNORE INTO location VALUES(0, 'DEFAULT')")
	if err != nil {
		return err
	}
	return nil
}

func initBlockServicesTable(db *sql.DB) error {
	_, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS block_services(
			id INT NOT NULL,
			location_id INT NOT NULL,
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
			has_files INT NOT NULL,
			flags_last_changed INT NOT NULL,
			PRIMARY KEY (id, location_id)
		)`)
	if err != nil {
		return err
	}

	_, err = db.Exec("CREATE INDEX IF NOT EXISTS last_seen_idx_b on block_services (last_seen)")
	if err != nil {
		return err
	}

	_, err = db.Exec("CREATE INDEX IF NOT EXISTS flags_last_changed_idx_b on block_services (flags_last_changed)")
	if err != nil {
		return err
	}

	n := sql.Named

	_, err = db.Exec("UPDATE block_services SET flags = :decom_flag WHERE (flags & :decom_flag) <> 0", n("decom_flag", msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED))
	return err
}

func populateCDCTableForLocation(db *sql.DB, location msgs.Location) error {
	// Prepopulate cdc rows to simplify register operation checking if ip has changed
	for replicaId := 0; replicaId < 5; replicaId++ {
		n := sql.Named
		_, err := db.Exec("INSERT OR IGNORE INTO cdc VALUES(:replica_id, :location_id, :is_leader, "+zeroIPString+", 0, "+zeroIPString+", 0, 0)", n("replica_id", replicaId), n("location_id", location), n("is_leader", replicaId == 0))
		if err != nil {
			return err
		}
	}
	return nil
}

func initCDCTable(db *sql.DB) error {
	_, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS cdc(
			replica_id INT,
			location_id INT NOT NULL,
			is_leader BOOL NOT NULL,
			ip1 BLOB NOT NULL,
			port1 INT NOT NULL,
			ip2 BLOB NOT NULL,
			port2 INT NOT NULL,
			last_seen INT NOT NULL,
			PRIMARY KEY (replica_id, location_id)
		)`)

	if err != nil {
		return err
	}

	// populate default location
	err = populateCDCTableForLocation(db, 0)
	if err != nil {
		return err
	}

	return nil
}

func populateShardsTableForLocation(db *sql.DB, location msgs.Location) error {
	// Prepopulate shard rows to simplify register operation checking if ip has changed
	for id := msgs.MakeShardReplicaId(0, 0); id < msgs.MakeShardReplicaId(0, 5); id++ {
		n := sql.Named
		_, err := db.Exec("INSERT OR IGNORE INTO shards VALUES(:id, :replica_id, :location_id, :is_leader, "+zeroIPString+", 0, "+zeroIPString+", 0, 0)", n("id", id.Shard()), n("replica_id", id.Replica()), n("location_id", location), n("is_leader", id.Replica() == 0))
		if err != nil {
			return err
		}
	}
	return nil
}

func initAndPopulateShardsTable(db *sql.DB) error {
	_, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS shards(
		id INT NOT NULL,
		replica_id INT NOT NULL,
		location_id INT NOT NULL,
		is_leader BOOL NOT NULL,
		ip1 BLOB NOT NULL,
		port1 INT NOT NULL,
		ip2 BLOB NOT NULL,
		port2 INT NOT NULL,
		last_seen INT NOT NULL,
		PRIMARY KEY (id, replica_id, location_id)
	)`)
	if err != nil {
		return err
	}

	_, err = db.Exec("CREATE INDEX IF NOT EXISTS last_seen_idx_s ON shards (last_seen)")
	if err != nil {
		return err
	}

	// populate default location
	err = populateShardsTableForLocation(db, 0)
	if err != nil {
		return err
	}

	return nil
}

func main() {
	httpPort := flag.Uint("http-port", 10000, "Port on which to run the HTTP server")
	var addresses lib.StringArrayFlags
	flag.Var(&addresses, "addr", "Addresses (up to two) to bind bincode server on.")
	logFile := flag.String("log-file", "", "File in which to write logs (or stdout)")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	syslog := flag.Bool("syslog", false, "")
	metrics := flag.Bool("metrics", false, "")
	dataDir := flag.String("data-dir", "", "Where to store the shuckle files")
	maxConnections := flag.Uint("max-connections", 4000, "Maximum number of connections to accept.")
	mtu := flag.Uint64("mtu", 0, "")
	stale := flag.Duration("stale", 3*time.Minute, "")
	blockServiceDelay := flag.Duration("block-service-delay", 0*time.Minute, "How long to wait before starting to use new block services")
	minDecomInterval := flag.Duration("min-decom-interval", 2*time.Hour, "Minimum interval between calls to decommission blockservices by automation")
	maxDecommedWithFiles := flag.Int("max-non-readable-failure-domains", 3, "Maximum number of non readable failure domains before alerting and rejecting further automated decommissions")
	maxFailureDomainsPerShard := flag.Int("max-failure-domains-per-shard", 28, "Maximum number of failure domains per storage class that shards use for assigning blocks at a single point in time")
	currentBlockServiceUpdateInterval := flag.Duration("current-block-service-update-interval", 30*time.Minute, "Maximum interval between re-calculating current block services")
	scriptsJs := flag.String("scripts-js", "", "")

	flag.Parse()
	noRunawayArgs()

	if len(addresses) == 0 || len(addresses) > 2 {
		fmt.Fprintf(os.Stderr, "at least one -addr and no more than two needs to be provided\n")
		os.Exit(2)
	}
	if *maxFailureDomainsPerShard < 28 {
		fmt.Fprintf(os.Stderr, "-max-failure-domains-per-shard should not be less than 28. (double the minimum)")
		os.Exit(2)
	}

	ownIp1, ownPort1, err := lib.ParseIPV4Addr(addresses[0])
	if err != nil {
		panic(err)
	}

	var ownIp2 [4]byte
	var ownPort2 uint16
	if len(addresses) == 2 {
		ownIp2, ownPort2, err = lib.ParseIPV4Addr(addresses[1])
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
	log.Info("  addr = %v", addresses)
	log.Info("  httpPort = %v", *httpPort)
	log.Info("  logFile = '%v'", *logFile)
	log.Info("  logLevel = %v", level)
	log.Info("  maxConnections = %d", *maxConnections)
	log.Info("  mtu = %v", *mtu)
	log.Info("  stale = '%v'", *stale)
	log.Info("  blockServiceDelay ='%v' ", *blockServiceDelay)
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
	if len(addresses) == 2 {
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
		addrs:                                   msgs.AddrsInfo{msgs.IpPort{ownIp1, ownPort1}, msgs.IpPort{ownIp2, ownPort2}},
		minAutoDecomInterval:                    *minDecomInterval,
		maxNonReadableFailureDomainsPerLocation: *maxDecommedWithFiles,
		maxFailureDomainsPerShard:               *maxFailureDomainsPerShard,
		currentBlockServiceUpdateInterval:       *currentBlockServiceUpdateInterval,
		blockServiceDelay:                       *blockServiceDelay,
	}
	state, err := newState(log, db, config)
	if err != nil {
		panic(err)
	}

	signalChan := make(chan os.Signal, 1)
	signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
	go func() {
		sig := <-signalChan
		signal.Stop(signalChan)
		syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
	}()

	setupRouting(log, state, *scriptsJs)

	terminateChan := make(chan any)

	go func() {
		defer func() { lib.HandleRecoverPanic(log, recover()) }()
		assignWritableBlockServicesToShards(log, state)
	}()

	var activeConnections int64
	startBincodeHandler := func(listener net.Listener) {
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				conn, err := listener.Accept()
				if err != nil {
					terminateChan <- err
					return
				}
				if (atomic.AddInt64(&activeConnections, 1) > int64(*maxConnections)) {
					conn.Close()
					atomic.AddInt64(&activeConnections, -1)
					continue
				}
				go func() {
					defer func() {
						atomic.AddInt64(&activeConnections, -1)
						lib.HandleRecoverPanic(log, recover())
					}()
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
