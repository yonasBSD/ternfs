package main

import (
	"bytes"
	_ "embed"
	"encoding/hex"
	"flag"
	"fmt"
	"html/template"
	"io"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"runtime/debug"
	"sort"
	"strconv"
	"strings"
	"sync"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
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

type state struct {
	mutex         sync.RWMutex
	blockServices map[msgs.BlockServiceId]*msgs.BlockServiceInfo
	shards        [256]msgs.ShardInfo
	cdcIp         [4]byte
	cdcPort       uint16
}

func newState() *state {
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

	template.ParseFiles()
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

//go:embed shuckle.png
var shucklePngStr []byte

//go:embed shuckleface.png
var shuckleFacePngStr []byte

//go:embed bootstrap.min.css
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
	log eggs.LogLevels,
	w http.ResponseWriter,
	r *http.Request,
	handle func(log eggs.LogLevels, query url.Values) (io.ReadCloser, int64, int),
) {
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
		if r.Method != http.MethodGet {
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
	w.WriteHeader(*statusPtr)
	if written, err := io.CopyN(w, content, *sizePtr); err != nil {
		log.RaiseAlert(fmt.Errorf("Could not send full response of size %v, %v written: %w", *sizePtr, written, err))
	}
}

func handlePage(
	log eggs.LogLevels,
	w http.ResponseWriter,
	r *http.Request,
	// third result is status code
	page func(query url.Values) (*template.Template, *pageData, int),
) {
	handleWithRecover(
		log, w, r,
		func(log eggs.LogLevels, query url.Values) (io.ReadCloser, int64, int) {
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
	Addr           string
	StorageClass   msgs.StorageClass
	FailureDomain  string
	CapacityBytes  string
	AvailableBytes string
	Path           string
	Blocks         uint64
}

type indexData struct {
	NumBlockServices    int
	NumFailureDomains   int
	TotalCapacity       string
	TotalUsed           string
	TotalUsedPercentage string
	CDCAddr             string
	BlockServices       []indexBlockService
	ShardsAddrs         []string
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

var shuckleTemplate *template.Template

func handleIndex(ll eggs.LogLevels, state *state, w http.ResponseWriter, r *http.Request) {
	handlePage(
		ll, w, r,
		func(_ url.Values) (*template.Template, *pageData, int) {
			state.mutex.RLock()
			defer state.mutex.RUnlock()

			data := indexData{
				NumBlockServices: len(state.blockServices),
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
			return shuckleTemplate, &pageData{Title: "Shuckle", Body: &data}, http.StatusOK
		},
	)
}

//go:embed file.html
var fileTemplateStr string

type fileBlock struct {
	Id           string
	Crc32        string
	BlockService string
	Link         string
}

type fileSpan struct {
	Offset       string
	Size         string
	Crc32        string
	StorageClass string
	BlockSize    string
	BodyBlocks   []fileBlock
	BodyBytes    string
	ParityBlocks int
	DataBlocks   int
}

type fileData struct {
	Id    string
	Path  string // might be empty
	Size  string
	Mtime string
	Spans []fileSpan
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

type directoryData struct {
	Id    string
	Path  string // might be empty
	Mtime string
	Owner string
	Edges []directoryEdge
}

func newClient(log eggs.LogLevels, state *state) *eggs.Client {
	state.mutex.RLock()
	defer state.mutex.RUnlock()

	var shardIps [256][4]byte
	var shardPorts [256]uint16
	for i, si := range state.shards {
		shardIps[i] = si.Ip
		shardPorts[i] = si.Port
	}
	client, err := eggs.NewClientDirect(log, nil, nil, nil, state.cdcIp, state.cdcPort, &shardIps, &shardPorts)
	if err != nil {
		panic(err)
	}
	return client
}

func lookup(log eggs.LogLevels, client *eggs.Client, path string) *msgs.InodeId {
	segments := strings.Split(path, "/")[1:] // starts with /
	id := msgs.ROOT_DIR_INODE_ID
	for _, segment := range segments {
		if segment == "" {
			continue
		}
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

var fileTemplate *template.Template
var directoryTemplate *template.Template

func handleInode(
	log eggs.LogLevels,
	state *state,
	w http.ResponseWriter,
	r *http.Request,
) {
	handlePage(
		log, w, r,
		func(query url.Values) (*template.Template, *pageData, int) {
			path := query.Get("path")
			if path != "" {
				if len(path) == 0 || path[0] != '/' {
					return errorPage(http.StatusBadRequest, "Path must start with /")
				}
			}
			var id msgs.InodeId
			idStr := query.Get("id")
			if idStr != "" {
				i, err := strconv.ParseUint(idStr, 0, 63)
				if err != nil {
					return errorPage(http.StatusBadRequest, fmt.Sprintf("cannot parse id %v: %v", idStr, err))
				}
				id = msgs.InodeId(i)
			}
			if id != msgs.NULL_INODE_ID && path != "" {
				return errorPage(http.StatusBadRequest, "cannot specify both id and path")
			}
			if id == msgs.NULL_INODE_ID && path == "" {
				path = "/"
			}
			client := newClient(log, state)
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
					Path: path,
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
				{
					_, full := query["full"]
					if full {

					} else {
						req := msgs.FullReadDirReq{DirId: id}
						resp := msgs.FullReadDirResp{}
						for {
							if err := client.ShardRequest(log, id.Shard(), &req, &resp); err != nil {
								panic(err)
							}
							for _, edge := range resp.Results {
								dataEdge := directoryEdge{
									Current:      edge.Current,
									TargetId:     fmt.Sprintf("%v", edge.TargetId.Id()),
									Owned:        edge.Current || edge.TargetId.Extra(),
									NameHash:     fmt.Sprintf("%08x", edge.NameHash),
									Name:         edge.Name,
									CreationTime: edge.CreationTime.String(),
									Locked:       edge.Current && edge.TargetId.Extra(),
								}
								if edge.TargetId.Id() != msgs.NULL_INODE_ID {
									dataEdge.Type = edge.TargetId.Id().Type().String()
								}
								data.Edges = append(data.Edges, dataEdge)
							}
							req.Cursor = resp.Next
							if req.Cursor == (msgs.FullReadDirCursor{}) {
								break
							}
						}
					}
				}
				sort.Slice(
					data.Edges,
					func(i, j int) bool {
						a := data.Edges[i]
						b := data.Edges[j]
						if a.Name != b.Name {
							return a.Name < b.Name
						}
						if a.NameHash != b.NameHash {
							return a.NameHash < b.NameHash
						}
						return a.CreationTime < b.CreationTime
					},
				)
				return directoryTemplate, &pageData{Title: title, Body: &data}, http.StatusOK
			} else {
				data := fileData{
					Id:   fmt.Sprintf("%v", id),
					Path: path,
				}
				title := fmt.Sprintf("File %v", data.Id)
				{
					resp := msgs.StatFileResp{}
					if err := client.ShardRequest(log, id.Shard(), &msgs.StatFileReq{Id: id}, &resp); err != nil {
						panic(err)
					}
					data.Mtime = resp.Mtime.String()
					data.Size = fmt.Sprintf("%v (%v bytes)", formatSize(resp.Size), resp.Size)
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
								Offset:       formatPreciseSize(span.ByteOffset),
								Size:         formatPreciseSize(span.Size),
								Crc32:        hex.EncodeToString(span.Crc32[:]),
								StorageClass: span.StorageClass.String(),
								BlockSize:    formatPreciseSize(span.BlockSize),
								BodyBytes:    hex.EncodeToString(span.BodyBytes),
								DataBlocks:   span.Parity.DataBlocks(),
								ParityBlocks: span.Parity.ParityBlocks(),
							}
							for _, block := range span.BodyBlocks {
								blockService := resp.BlockServices[block.BlockServiceIx]
								crcStr := hex.EncodeToString(block.Crc32[:])
								fb := fileBlock{
									Id:           block.BlockId.String(),
									BlockService: blockService.Id.String(),
									Crc32:        crcStr,
									Link:         fmt.Sprintf("/blocks/%v/%v?size=%v&crc=%v", blockService.Id, block.BlockId, span.BlockSize, crcStr),
								}
								fs.BodyBlocks = append(fs.BodyBlocks, fb)
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

func handleBlock(log eggs.LogLevels, st *state, w http.ResponseWriter, r *http.Request) {
	handleWithRecover(
		log, w, r,
		func(log eggs.LogLevels, query url.Values) (io.ReadCloser, int64, int) {
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
			crcBytes, err := hex.DecodeString(query.Get("crc"))
			if err != nil || len(crcBytes) != 4 {
				return sendPage(errorPage(http.StatusBadRequest, fmt.Sprintf("Bad crc '%v'", query.Get("crc"))))
			}
			var crc [4]byte
			copy(crc[:], crcBytes)
			var blockService msgs.BlockService
			var conn *net.TCPConn
			{
				st.mutex.RLock()
				blockServiceInfo, found := st.blockServices[blockServiceId]
				st.mutex.RUnlock()
				if !found {
					return sendPage(errorPage(http.StatusNotFound, fmt.Sprintf("Unknown block service id %v", blockServiceId)))
				}
				blockService.Id = blockServiceInfo.Id
				blockService.Ip = blockServiceInfo.Ip
				blockService.Port = blockServiceInfo.Port
				var err error
				conn, err = eggs.BlockServiceConnection(blockServiceId, blockService.Ip[:], blockService.Port)
				if err != nil {
					panic(err)
				}
			}
			if err := eggs.FetchBlock(log, conn, &blockService, blockId, crc, 0, uint32(size)); err != nil {
				panic(err)
			}
			w.Header().Set("Content-Type", "application/x-binary")
			log.Info("serving block of size %v", size)
			return conn, int64(size), http.StatusOK
		},
	)
}

func setupRouting(log eggs.LogLevels, st *state) {
	errorTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "error", body: errorTemplateStr},
	)

	setupPage := func(path string, handle func(ll eggs.LogLevels, state *state, w http.ResponseWriter, r *http.Request)) {
		http.HandleFunc(
			path,
			func(w http.ResponseWriter, r *http.Request) { handle(log, st, w, r) },
		)
	}

	// Static assets
	http.HandleFunc(
		"/bootstrap.min.css",
		func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "text/css; charset=utf-8")
			w.Write(bootstrapCssStr)
		},
	)
	http.HandleFunc(
		"/shuckle-face.png",
		func(w http.ResponseWriter, r *http.Request) { w.Write(shuckleFacePngStr) },
	)
	http.HandleFunc(
		"/shuckle.png",
		func(w http.ResponseWriter, r *http.Request) { w.Write(shucklePngStr) },
	)

	// blocks serving
	http.HandleFunc(
		"/blocks/",
		func(w http.ResponseWriter, r *http.Request) { handleBlock(log, st, w, r) },
	)

	// pages
	shuckleTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "shuckle", body: indexTemplateStr},
	)
	setupPage("/shuckle", handleIndex)

	fileTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "file", body: fileTemplateStr},
	)
	directoryTemplate = parseTemplates(
		namedTemplate{name: "base", body: baseTemplateStr},
		namedTemplate{name: "directory", body: directoryTemplateStr},
	)
	setupPage("/inode", handleInode)
}

func main() {
	bincodePort := flag.Uint("bincode-port", 10000, "Port on which to run the bincode server.")
	httpPort := flag.Uint("http-port", 10001, "Port on which to run the HTTP server")
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

	state := newState()

	setupRouting(ll, state)

	terminateChan := make(chan error)

	go func() {
		for {
			conn, err := bincodeListener.Accept()
			if err != nil {
				terminateChan <- err
				return
			}
			go func() { handleRequest(ll, state, conn.(*net.TCPConn)) }()
		}
	}()

	go func() {
		terminateChan <- http.Serve(httpListener, nil)
	}()

	panic(<-terminateChan)
}
