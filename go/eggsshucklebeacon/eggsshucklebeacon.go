package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

func handleRequestParsed(log *lib.Logger, shuckleResp *msgs.ShuckleResp, req msgs.ShuckleRequest) (msgs.ShuckleResponse, error) {
	var err error
	var resp msgs.ShuckleResponse
	switch req.(type) {
	case *msgs.ShuckleReq:
		resp = shuckleResp
	default:
		err = fmt.Errorf("bad req type %T", req)
	}

	return resp, err
}

func handleError(
	log *lib.Logger,
	conn *net.TCPConn,
	err error,
) {
	if err == io.EOF {
		log.Debug("got EOF, terminating")
		return
	}

	// we always raise an alert since this is almost always bad news in shuckle
	log.RaiseAlertStack(1, "got unexpected error %v from %v", err, conn.RemoteAddr())

	// attempt to say goodbye, ignore errors
	if eggsErr, isEggsErr := err.(msgs.ErrCode); isEggsErr {
		lib.WriteShuckleResponseError(log, conn, eggsErr)
	} else {
		lib.WriteShuckleResponseError(log, conn, msgs.INTERNAL_ERROR)
	}
}

func handleRequest(log *lib.Logger, shuckleResp *msgs.ShuckleResp, conn *net.TCPConn) {
	defer conn.Close()

	req, err := lib.ReadShuckleRequest(log, conn)
	if err != nil {
		handleError(log, conn, err)
		return
	}
	log.Debug("handling request %T from %s", req, conn.RemoteAddr())
	resp, err := handleRequestParsed(log, shuckleResp, req)
	if err != nil {
		handleError(log, conn, err)
		return
	}
	log.Debug("sending back response %T to %s", resp, conn.RemoteAddr())
	if err := lib.WriteShuckleResponse(log, conn, resp); err != nil {
		handleError(log, conn, err)
	}
}

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func main() {
	logFile := flag.String("log-file", "", "File in which to write logs (or stdout)")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	syslog := flag.Bool("syslog", false, "")
	addr1Str := flag.String("addr-1", "", "First address that we'll bind to.")
	addr2Str := flag.String("addr-2", "", "Second address that we'll bind to. If it is not provided, we will only bind to the first IP.")
	shuckleAddr1 := flag.String("shuckle-1", "", "First shuckle address to advertise")
	shuckleAddr2 := flag.String("shuckle-2", "", "Second shuckle address to advertise")

	flag.Parse()
	noRunawayArgs()

	if *addr1Str == "" {
		fmt.Fprintf(os.Stderr, "-addr-1 must be provided.\n")
		os.Exit(2)
	}
	ownIp1, port1, err := lib.ParseIPV4Addr(*addr1Str)
	if err != nil {
		panic(err)
	}

	var ownIp2 [4]byte
	var port2 uint16
	if *addr2Str != "" {
		ownIp2, port2, err = lib.ParseIPV4Addr(*addr2Str)
		if err != nil {
			panic(err)
		}
	}

	if *shuckleAddr1 == "" {
		panic(fmt.Errorf("-shuckle-1 needs to be provided"))
	}
	shuckleResp := &msgs.ShuckleResp{}
	shuckleResp.Ip1, shuckleResp.Port1, err = lib.ParseIPV4Addr(*shuckleAddr1)
	if err != nil {
		panic(err)
	}
	if *shuckleAddr2 != "" {
		shuckleResp.Ip2, shuckleResp.Port2, err = lib.ParseIPV4Addr(*shuckleAddr2)
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
	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppInstance: "eggsshuckle", AppType: "restech_eggsfs.critical", PrintQuietAlerts: true})

	log.Info("Running shuckle beacon with options:")
	log.Info("  addr1 = %s", *addr1Str)
	log.Info("  addr2 = %s", *addr2Str)
	log.Info("  shuckleAddr1 = %s", *shuckleAddr1)
	log.Info("  shuckleAddr2 = %s", *shuckleAddr2)
	log.Info("  logFile = '%v'", *logFile)
	log.Info("  logLevel = %v", level)

	bincodeListener1, err := net.Listen("tcp", fmt.Sprintf("%v:%v", net.IP(ownIp1[:]), port1))
	if err != nil {
		panic(err)
	}
	defer bincodeListener1.Close()

	var bincodeListener2 net.Listener
	if *addr1Str != "" {
		var err error
		bincodeListener2, err = net.Listen("tcp", fmt.Sprintf("%v:%v", net.IP(ownIp2[:]), port2))
		if err != nil {
			panic(err)
		}
		defer bincodeListener2.Close()
	}

	if bincodeListener2 == nil {
		log.Info("running on %v", bincodeListener1.Addr())
	} else {
		log.Info("running on %v,%v", bincodeListener1.Addr(), bincodeListener2.Addr())
	}

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
					handleRequest(log, shuckleResp, conn.(*net.TCPConn))
				}()
			}
		}()
	}
	startBincodeHandler(bincodeListener1)
	if bincodeListener2 != nil {
		startBincodeHandler(bincodeListener2)
	}

	panic(<-terminateChan)
}
