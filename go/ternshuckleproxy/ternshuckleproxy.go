package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"log"
	"math/rand"
	"net"
	"os"
	"os/signal"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/ternfs/bincode"
	"xtx/ternfs/client"
	"xtx/ternfs/lib"
	"xtx/ternfs/msgs"
	"xtx/ternfs/wyhash"
)

type state struct {
	counters  map[msgs.ShuckleMessageKind]*lib.Timings
	config    *shuckleProxyConfig
	shuckleConn    *client.ShuckleConn
}

type shuckleProxyConfig struct {
	addrs    msgs.AddrsInfo
	location msgs.Location
	shuckleAddress string
	numHandlers uint
}

func newState(
	log *lib.Logger,
	conf *shuckleProxyConfig,
) *state {
	st := &state{
		config: conf,
		shuckleConn: client.MakeShuckleConn(log, nil, conf.shuckleAddress, conf.numHandlers),
	}

	st.counters = make(map[msgs.ShuckleMessageKind]*lib.Timings)
	for _, k := range msgs.AllShuckleMessageKind {
		st.counters[k] = lib.NewTimings(40, 10*time.Microsecond, 1.5)
	}

	return st
}

func handleLocalChangedBlockServices(ll *lib.Logger, s *state, req *msgs.LocalChangedBlockServicesReq) (*msgs.LocalChangedBlockServicesResp, error) {
	reqAtLocation := &msgs.ChangedBlockServicesAtLocationReq{s.config.location, req.ChangedSince}
	resp, err := handleProxyRequest(ll, s, reqAtLocation)
	if err != nil {
		return nil, err
	}
	respAtLocation := resp.(*msgs.ChangedBlockServicesAtLocationResp)
	return &msgs.LocalChangedBlockServicesResp{respAtLocation.LastChange, respAtLocation.BlockServices}, nil
}

func handleLocalShards(ll *lib.Logger, s *state, _ *msgs.LocalShardsReq) (*msgs.LocalShardsResp, error) {
	reqAtLocation := &msgs.ShardsAtLocationReq{s.config.location}
	resp, err := handleProxyRequest(ll, s, reqAtLocation)
	if err != nil {
		return nil, err
	}

	respAtLocation := resp.(*msgs.ShardsAtLocationResp)
	return &msgs.LocalShardsResp{respAtLocation.Shards}, nil
}

func handleLocalCdc(log *lib.Logger, s *state, req *msgs.LocalCdcReq) (msgs.ShuckleResponse, error) {
	reqAtLocation := &msgs.CdcAtLocationReq{LocationId: s.config.location}
	resp, err := handleProxyRequest(log, s, reqAtLocation)
	if err != nil {
		return nil, err
	}

	respAtLocation := resp.(*msgs.CdcAtLocationResp)
	return &msgs.LocalCdcResp{respAtLocation.Addrs, respAtLocation.LastSeen}, nil
}

func handleProxyRequest(log *lib.Logger, s *state, req msgs.ShuckleRequest) (msgs.ShuckleResponse, error) {
	return s.shuckleConn.Request(req)
}

func handleShuckle(log *lib.Logger, s *state) (msgs.ShuckleResponse, error) {
	return &msgs.ShuckleResp{s.config.addrs}, nil
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
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.DecommissionBlockServiceReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.LocalShardsReq:
		resp, err = handleLocalShards(log, s, whichReq)
	case *msgs.AllShardsReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.RegisterShardReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.AllBlockServicesDeprecatedReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.LocalChangedBlockServicesReq:
		resp, err = handleLocalChangedBlockServices(log, s, whichReq)
	case *msgs.LocalCdcReq:
		resp, err = handleLocalCdc(log, s, whichReq)
	case *msgs.AllCdcReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.CdcReplicasDEPRECATEDReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.RegisterCdcReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.InfoReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.ShuckleReq:
		resp, err = handleShuckle(log, s)
	case *msgs.ShardBlockServicesDEPRECATEDReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.MoveShardLeaderReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.ClearShardInfoReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.ClearCdcInfoReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.EraseDecommissionedBlockReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.MoveCdcLeaderReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.CreateLocationReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.RenameLocationReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.LocationsReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.RegisterBlockServicesReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.CdcAtLocationReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.ChangedBlockServicesAtLocationReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.ShardsAtLocationReq:
		resp, err = handleProxyRequest(log, s, req)
	case *msgs.ShardBlockServicesReq:
		resp, err = handleProxyRequest(log, s, req)
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

func writeShuckleResponseError(log *lib.Logger, w io.Writer, err msgs.TernError) error {
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

	// attempt to say goodbye, ignore errors
	if ternErr, isTernErr := err.(msgs.TernError); isTernErr {
		writeShuckleResponseError(log, conn, ternErr)
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
		now := time.Now()
		reqDeadline := now.Add(client.DefaultShuckleTimeout.RequestTimeout)
		conn.SetReadDeadline(now.Add(client.DefaultShuckleTimeout.ReconnectTimeout.Overall))
		req, err := readShuckleRequest(log, conn)
		conn.SetReadDeadline(time.Time{})

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
			conn.SetWriteDeadline(reqDeadline)
			if err := writeShuckleResponse(log, conn, resp); err != nil {
				if handleError(log, conn, err) {
					return
				}
			}
			conn.SetWriteDeadline(time.Time{})
		}
	}
}

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
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
			metrics.Measurement("eggsfs_shuckle_proxy_requests")
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

func main() {

	var addresses lib.StringArrayFlags
	flag.Var(&addresses, "addr", "Addresses (up to two) to bind bincode server on.")
	logFile := flag.String("log-file", "", "File in which to write logs (or stdout)")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	syslog := flag.Bool("syslog", false, "")
	metrics := flag.Bool("metrics", false, "")
	shuckleAddress := flag.String("shuckle-address", "", "Shuckle address to connect to.")
	location := flag.Uint("location", 0, "Location id for this shuckle proxy.")
	numHandlers := flag.Uint("num-handlers", 100, "Number of shuckle connections to open.")
	maxConnections := flag.Uint("max-connections", 4000, "Maximum number of connections to accept.")
	mtu := flag.Uint64("mtu", 0, "")

	flag.Parse()
	noRunawayArgs()

	if len(addresses) == 0 || len(addresses) > 2 {
		fmt.Fprintf(os.Stderr, "at least one -addr and no more than two needs to be provided\n")
		os.Exit(2)
	}

	if *shuckleAddress == "" {
		fmt.Fprintf(os.Stderr, "no shuckle address provided\n")
		os.Exit(2)
	}

	if *location > 255 {
		fmt.Fprintf(os.Stderr, "location id 0..255 is supported\n")
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
	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppInstance: "eggsshuckleproxy", AppType: "restech_eggsfs.critical"})

	log.Info("Running shuckle proxy with options:")
	log.Info("  addr = %v", addresses)
	log.Info("	shuckle-address = %v", *shuckleAddress)
	log.Info("  location = %d", *location)
	log.Info("  logFile = '%v'", *logFile)
	log.Info("  logLevel = %v", level)
	log.Info("  maxConnections = %d", *maxConnections)
	log.Info("  mtu = %v", *mtu)


	if *mtu != 0 {
		client.SetMTU(*mtu)
	}


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


	if bincodeListener2 == nil {
		log.Info("running on  %v (bincode)", bincodeListener1.Addr())
	} else {
		log.Info("running on %v,%v (bincode)", bincodeListener1.Addr(), bincodeListener2.Addr())
	}

	config := &shuckleProxyConfig{
		addrs:  msgs.AddrsInfo{msgs.IpPort{ownIp1, ownPort1}, msgs.IpPort{ownIp2, ownPort2}},
		location: msgs.Location(*location),
		numHandlers: *numHandlers,
		shuckleAddress: *shuckleAddress,
	}
	state := newState(log, config)

	signalChan := make(chan os.Signal, 1)
	signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
	go func() {
		sig := <-signalChan
		signal.Stop(signalChan)
		syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
	}()

	terminateChan := make(chan any)

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

	if *metrics {
		go func() {
			defer func() { lib.HandleRecoverPanic(log, recover()) }()
			sendMetrics(log, state)
		}()
	}

	panic(<-terminateChan)
}
