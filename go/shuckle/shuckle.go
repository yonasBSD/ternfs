package main

import (
	"bytes"
	"flag"
	"fmt"
	"html/template"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"sync"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

type state struct {
	mutex         sync.RWMutex
	blockServices map[msgs.BlockServiceId]*msgs.BlockServiceInfo
	shards        [256]msgs.ShardInfo
	cdcIp         [4]byte
	cdcPort       uint16
}

func newBlockServices() *state {
	return &state{
		mutex:         sync.RWMutex{},
		blockServices: make(map[msgs.BlockServiceId]*msgs.BlockServiceInfo),
	}
}

func handleBlockServicesForShard(ll eggs.LogLevels, s *state, w io.Writer, req *msgs.BlockServicesForShardReq) *msgs.BlockServicesForShardResp {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	resp := msgs.BlockServicesForShardResp{}
	resp.BlockServices = make([]msgs.BlockServiceInfo, len(s.blockServices))

	i := 0
	for _, bs := range s.blockServices {
		resp.BlockServices[i] = *bs
		i++
	}

	return &resp
}

func handleAllBlockServicesReq(ll eggs.LogLevels, s *state, w io.Writer, req *msgs.AllBlockServicesReq) *msgs.AllBlockServicesResp {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	resp := msgs.AllBlockServicesResp{}
	resp.BlockServices = make([]msgs.BlockServiceInfo, len(s.blockServices))

	i := 0
	for _, bs := range s.blockServices {
		resp.BlockServices[i] = *bs
		i++
	}

	return &resp
}

func handleRegisterBlockService(ll eggs.LogLevels, s *state, w io.Writer, req *msgs.RegisterBlockServiceReq) *msgs.RegisterBlockServiceResp {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	s.blockServices[req.BlockService.Id] = &req.BlockService

	return &msgs.RegisterBlockServiceResp{}
}

func handleShards(ll eggs.LogLevels, s *state, w io.Writer, req *msgs.ShardsReq) *msgs.ShardsResp {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	resp := msgs.ShardsResp{}
	resp.Shards = s.shards[:]

	return &resp
}

func handleRegisterShard(ll eggs.LogLevels, s *state, w io.Writer, req *msgs.RegisterShardReq) *msgs.RegisterShardResp {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	s.shards[req.Id] = req.Info

	return &msgs.RegisterShardResp{}
}

func handleCdcReq(log eggs.LogLevels, s *state, w io.Writer, req *msgs.CdcReq) *msgs.CdcResp {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	resp := msgs.CdcResp{}
	resp.Ip = s.cdcIp
	resp.Port = s.cdcPort

	return &resp
}

func handleRegisterCdcReq(log eggs.LogLevels, s *state, w io.Writer, req *msgs.RegisterCdcReq) *msgs.RegisterCdcResp {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	s.cdcIp = req.Ip
	s.cdcPort = req.Port

	return &msgs.RegisterCdcResp{}
}

func handleRequest(log eggs.LogLevels, s *state, conn *net.TCPConn) {
	conn.SetLinger(0) // poor man error handling for now
	defer conn.Close()

	req, err := eggs.ReadShuckleRequest(log, conn)
	if err != nil {
		log.RaiseAlert(fmt.Errorf("could not decode request: %w", err))
		return
	}
	log.Debug("handling request %T %+v", req, req)
	var resp msgs.ShuckleResponse

	switch whichReq := req.(type) {
	case *msgs.BlockServicesForShardReq:
		resp = handleBlockServicesForShard(log, s, conn, whichReq)
	case *msgs.RegisterBlockServiceReq:
		resp = handleRegisterBlockService(log, s, conn, whichReq)
	case *msgs.ShardsReq:
		resp = handleShards(log, s, conn, whichReq)
	case *msgs.RegisterShardReq:
		resp = handleRegisterShard(log, s, conn, whichReq)
	case *msgs.AllBlockServicesReq:
		resp = handleAllBlockServicesReq(log, s, conn, whichReq)
	case *msgs.CdcReq:
		resp = handleCdcReq(log, s, conn, whichReq)
	case *msgs.RegisterCdcReq:
		resp = handleRegisterCdcReq(log, s, conn, whichReq)
	default:
		log.RaiseAlert(fmt.Errorf("bad req type %T", req))
	}
	log.Debug("sending back response %T", resp)
	if err := eggs.WriteShuckleResponse(log, conn, resp); err != nil {
		log.RaiseAlert(fmt.Errorf("could not send response: %w", err))
	}
}

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

type indexBlockService struct {
	Id            msgs.BlockServiceId
	Addr          string
	StorageClass  uint8
	FailureDomain string
}

type indexData struct {
	BlockServices []indexBlockService
	ShardsAddrs   []string
}

const indexTemplateStr string = `
<!DOCTYPE html>
<html lang="en">
	<head>
		<meta charset="UTF-8">
		<title>Shuckle</title>
	</head>
	<body>
		<h1>Shuckle</h1>
		<h2>Block services</h2>
		<table>
			<thead>
				<tr>
					<th>Id</th>
					<th>Address</th>
					<th>StorageClass</th>
					<th>FailureDomain</th>
				</tr>
			<tbody>
				{{ range .BlockServices }}
					<tr>
						<td><tt>{{.Id}}</tt></td>
						<td><tt>{{.Addr}}</tt></td>
						<td>{{.StorageClass}}</td>
						<td>{{.FailureDomain}}</td>
					</tr>
				{{end}}
			</tbody>
		</table>
		<h2>Shards</h2>
		<table>
			<thead>
				<tr>
					<th>Id</th>
					<th>Address</th>
				</tr>
			<tbody>
				{{ range $id, $addr := .ShardsAddrs }}
					<tr>
						<td><tt>{{printf "%03v" $id}}</tt></td>
						<td><tt>{{$addr}}</tt></td>
					</tr>
				{{end}}
			</tbody>
		</table>
	</body>
</html>
`

func handleIndex(ll eggs.LogLevels, template *template.Template, state *state, w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	state.mutex.RLock()
	defer state.mutex.RUnlock()
	data := indexData{}
	for _, bs := range state.blockServices {
		data.BlockServices = append(data.BlockServices, indexBlockService{
			Id:            bs.Id,
			Addr:          fmt.Sprintf("%v:%v", net.IP(bs.Ip[:]), bs.Port),
			StorageClass:  bs.StorageClass,
			FailureDomain: string(bs.FailureDomain[:bytes.Index(bs.FailureDomain[:], []byte{0})]),
		})
	}
	for _, shard := range state.shards {
		data.ShardsAddrs = append(data.ShardsAddrs, fmt.Sprintf("%v:%v", net.IP(shard.Ip[:]), shard.Port))
	}
	if err := template.Execute(w, &data); err != nil {
		ll.RaiseAlert(fmt.Errorf("could not execute template: %w", err))
		http.Error(w, "internal error", http.StatusInternalServerError)
	}
}

func main() {
	httpPort := flag.Uint("http-port", 30000, "Port on which to run the HTTP server")
	bincodePort := flag.Uint("bincode-port", 30001, "Port on which to run the bincode server.")
	logFile := flag.String("log-file", "", "File in which to write logs (or stdout)")
	verbose := flag.Bool("verbose", false, "")
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

	ll := &eggs.LogLogger{
		Verbose: *verbose,
		Logger:  eggs.NewLogger(logOut),
	}

	indexTemplate, err := template.New("index").Option("missingkey=error").Parse(indexTemplateStr)
	if err != nil {
		panic(err)
	}

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

	blockServices := newBlockServices()

	http.HandleFunc(
		"/",
		func(w http.ResponseWriter, r *http.Request) { handleIndex(ll, indexTemplate, blockServices, w, r) },
	)

	terminateChan := make(chan error)

	go func() {
		for {
			conn, err := bincodeListener.Accept()
			if err != nil {
				terminateChan <- err
				return
			}
			go func() { handleRequest(ll, blockServices, conn.(*net.TCPConn)) }()
		}
	}()

	go func() {
		terminateChan <- http.Serve(httpListener, nil)
	}()

	panic(<-terminateChan)
}
