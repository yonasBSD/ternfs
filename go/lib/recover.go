package lib

import (
	"fmt"
	"os"
	"runtime/debug"
	"strings"
	"sync"
)

var stacktraceLock sync.Mutex

func HandleRecoverChan(log *Logger, terminateChan chan any, err any) {
	if err != nil {
		log.RaiseAlert(err)
		stacktraceLock.Lock()
		fmt.Fprintf(os.Stderr, "PANIC %v. Stacktrace:\n", err)
		for _, line := range strings.Split(string(debug.Stack()), "\n") {
			fmt.Fprintln(os.Stderr, line)
		}
		stacktraceLock.Unlock()
		if terminateChan != nil {
			terminateChan <- err
		}
	}
}

func HandleRecoverPanic(log *Logger, err any) {
	if err != nil {
		log.RaiseAlert(err)
		stacktraceLock.Lock()
		fmt.Fprintf(os.Stderr, "PANIC %v. Stacktrace:\n", err)
		for _, line := range strings.Split(string(debug.Stack()), "\n") {
			fmt.Fprintln(os.Stderr, line)
		}
		stacktraceLock.Unlock()
		panic(err)
	}
}
