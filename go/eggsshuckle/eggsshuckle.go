package main

import (
	"bytes"
	_ "embed"
	"encoding/base64"
	"flag"
	"fmt"
	"html/template"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"sort"
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

func handleRegisterBlockServices(ll eggs.LogLevels, s *state, w io.Writer, req *msgs.RegisterBlockServicesReq) *msgs.RegisterBlockServicesResp {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	for _, bs := range req.BlockServices {
		s.blockServices[bs.Id] = &bs
	}

	return &msgs.RegisterBlockServicesResp{}
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
	case *msgs.RegisterBlockServicesReq:
		resp = handleRegisterBlockServices(log, s, conn, whichReq)
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
	Id             msgs.BlockServiceId
	Addr           string
	StorageClass   msgs.StorageClass
	FailureDomain  string
	CapacityBytes  string
	AvailableBytes string
	Path           string
	Blocks         uint64
}

type indexData struct {
	ShuckleFacePngBase64 string
	ShucklePngBase64     string
	NumBlockServices     int
	NumFailureDomains    int
	TotalCapacity        string
	TotalUsed            string
	TotalUsedPercentage  string
	CDCAddr              string
	BlockServices        []indexBlockService
	ShardsAddrs          []string
	Blocks               uint64
}

//go:embed shuckle.png
var shucklePng []byte
var shucklePngBase64 string

//go:embed shuckleface.png
var shuckleFacePng []byte
var shuckleFacePngBase64 string

const indexTemplateStr string = `
<!DOCTYPE html>
<html lang="en">
	<head>
		<meta charset="UTF-8">
		<title>Shuckle</title>
		<link rel="icon" type="image/png" href="data:image/png;base64,{{.ShuckleFacePngBase64}}" />
	</head>
	<body>
		<h1>Shuckle</h1>
		<img src="data:image/png;base64,{{.ShucklePngBase64}}" alt="Mr. Shuckle"/>
		<h2>Overview</h2>
		<ul>
			<li>{{.NumBlockServices}} block services on {{.NumFailureDomains}} failure domains</li>
			<li>Total capacity: {{.TotalCapacity}}</li>
			<li>{{.TotalUsed}} used over {{.Blocks}} blocks ({{.TotalUsedPercentage}})</li>
		</ul>
		<h2>CDC</h2>
		<table>
			<thead>
				<tr>
					<th>Address</th>
				</tr>
			<tbody>
				<tr>
					<td><tt>{{.CDCAddr}}</tt></td>
				</tr>
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
		<h2>Block services</h2>
		<table>
			<thead>
				<tr>
					<th>FailureDomain</th>
					<th>Address</th>
					<th>Path</th>
					<th>Id</th>
					<th>StorageClass</th>
					<th>Blocks</th>
					<th>Capacity</th>
					<th>Available</th>
				</tr>
			<tbody>
				{{ range .BlockServices }}
					<tr>
						<td>{{.FailureDomain}}</td>
						<td><tt>{{.Addr}}</tt></td>
						<td><tt>{{.Path}}</tt></td>
						<td><tt>{{.Id}}</tt></td>
						<td>{{.StorageClass}}</td>
						<td>{{.Blocks}}</td>
						<td>{{.CapacityBytes}}</td>
						<td>{{.AvailableBytes}}</td>
					</tr>
				{{end}}
			</tbody>
		</table>
	</body>
</html>
`

func formatSize(bytes uint64) string {
	bytesf := float64(bytes)
	if bytes == 0 {
		return "0"
	}
	if bytesf < 1e6 {
		return fmt.Sprintf("%.2fKB", bytesf/1e3)
	}
	if bytesf < 1e9 {
		return fmt.Sprintf("%.2fMB", bytesf/1e6)
	}
	if bytesf < 1e12 {
		return fmt.Sprintf("%.2fGB", bytesf/1e9)
	}
	if bytesf < 1e15 {
		return fmt.Sprintf("%.2fTB", bytesf/1e12)
	}
	return fmt.Sprintf("%.2fPB", bytesf/1e15)
}

func handleIndex(ll eggs.LogLevels, template *template.Template, state *state, w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	state.mutex.RLock()
	defer state.mutex.RUnlock()

	data := indexData{
		ShucklePngBase64:     shucklePngBase64,
		ShuckleFacePngBase64: shuckleFacePngBase64,
		NumBlockServices:     len(state.blockServices),
	}
	data.CDCAddr = fmt.Sprintf("%v:%v", net.IP(state.cdcIp[:]), state.cdcPort)
	totalCapacityBytes := uint64(0)
	totalAvailableBytes := uint64(0)
	failureDomainsBytes := make(map[string]struct{})
	for _, bs := range state.blockServices {
		data.BlockServices = append(data.BlockServices, indexBlockService{
			Id:             bs.Id,
			Addr:           fmt.Sprintf("%v:%v", net.IP(bs.Ip[:]), bs.Port),
			StorageClass:   bs.StorageClass,
			FailureDomain:  string(bs.FailureDomain[:bytes.Index(bs.FailureDomain[:], []byte{0})]),
			CapacityBytes:  formatSize(bs.CapacityBytes),
			AvailableBytes: formatSize(bs.AvailableBytes),
			Path:           bs.Path,
			Blocks:         bs.Blocks,
		})
		failureDomainsBytes[string(bs.FailureDomain[:])] = struct{}{}
		totalAvailableBytes += bs.AvailableBytes
		totalCapacityBytes += bs.CapacityBytes
		data.Blocks += bs.Blocks
	}
	sort.Slice(
		data.BlockServices,
		func(i, j int) bool {
			a := data.BlockServices[i]
			b := data.BlockServices[j]
			if len(a.FailureDomain) != len(b.FailureDomain) {
				return len(a.FailureDomain) < len(b.FailureDomain)
			}
			if a.FailureDomain != b.FailureDomain {
				return a.FailureDomain < b.FailureDomain
			}
			if a.Path != b.Path {
				return a.Path < b.Path
			}
			return a.Id < b.Id
		},
	)
	for _, shard := range state.shards {
		data.ShardsAddrs = append(data.ShardsAddrs, fmt.Sprintf("%v:%v", net.IP(shard.Ip[:]), shard.Port))
	}
	data.TotalUsed = formatSize(totalCapacityBytes - totalAvailableBytes)
	data.TotalCapacity = formatSize(totalAvailableBytes)
	data.NumFailureDomains = len(failureDomainsBytes)
	if totalAvailableBytes == 0 {
		data.TotalUsedPercentage = "0%"
	} else {
		data.TotalUsedPercentage = fmt.Sprintf("%0.2f%%", 100.0*float64(totalAvailableBytes)/float64(totalCapacityBytes))
	}
	if err := template.Execute(w, &data); err != nil {
		ll.RaiseAlert(fmt.Errorf("could not execute template: %w", err))
		http.Error(w, "internal error", http.StatusInternalServerError)
	}
}

func main() {
	bincodePort := flag.Uint("bincode-port", 10000, "Port on which to run the bincode server.")
	httpPort := flag.Uint("http-port", 10001, "Port on which to run the HTTP server")
	logFile := flag.String("log-file", "", "File in which to write logs (or stdout)")
	verbose := flag.Bool("verbose", false, "")
	flag.Parse()
	noRunawayArgs()

	indexTemplate, err := template.New("index").Option("missingkey=error").Parse(indexTemplateStr)
	if err != nil {
		panic(err)
	}

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

	shucklePngBase64 = base64.StdEncoding.EncodeToString(shucklePng)
	shuckleFacePngBase64 = base64.StdEncoding.EncodeToString(shuckleFacePng)

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
