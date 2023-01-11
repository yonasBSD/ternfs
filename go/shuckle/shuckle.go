package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

type blockServices struct {
	mutex    sync.RWMutex
	services map[msgs.BlockServiceId]*eggs.BlockService
}

func newBlockServices() *blockServices {
	return &blockServices{
		mutex:    sync.RWMutex{},
		services: make(map[msgs.BlockServiceId]*eggs.BlockService),
	}
}

func handleIndex(bss *blockServices, w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	fmt.Fprintf(w, "Hello world\n")
}

func handleBlockServicesForShard(ll eggs.LogLevels, bss *blockServices, w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	w.Header().Set("content-type", "application/json")

	bss.mutex.RLock()
	defer bss.mutex.RUnlock()

	bssList := make([]*eggs.BlockService, len(bss.services))
	i := 0
	for _, bs := range bss.services {
		bssList[i] = bs
		i++
	}

	ll.Debug("sending back block services")

	json.NewEncoder(w).Encode(bssList)
}

func handleRegisterBlockService(ll eggs.LogLevels, bss *blockServices, w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var bs eggs.BlockService
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	// no way to require all fields to be present, great.
	if err := decoder.Decode(&bs); err != nil {
		http.Error(w, fmt.Sprintf("bad body: %v", err), http.StatusBadRequest)
		return
	}

	ll.Debug("adding block service %+v", bs)

	bss.mutex.Lock()
	defer bss.mutex.Unlock()

	bss.services[bs.Id] = &bs
}

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func main() {
	port := flag.Uint("port", 5000, "Port on which to run on.")
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

	ll.Info("running on port %v", *port)

	blockServices := newBlockServices()

	http.HandleFunc(
		"/",
		func(w http.ResponseWriter, r *http.Request) { handleIndex(blockServices, w, r) },
	)
	http.HandleFunc(
		"/block_services_for_shard",
		func(w http.ResponseWriter, r *http.Request) { handleBlockServicesForShard(ll, blockServices, w, r) },
	)
	http.HandleFunc(
		"/all_block_services",
		func(w http.ResponseWriter, r *http.Request) { handleBlockServicesForShard(ll, blockServices, w, r) },
	)
	http.HandleFunc(
		"/register_block_service",
		func(w http.ResponseWriter, r *http.Request) { handleRegisterBlockService(ll, blockServices, w, r) },
	)

	log.Fatal(http.ListenAndServe(fmt.Sprintf(":%v", *port), nil))
}
