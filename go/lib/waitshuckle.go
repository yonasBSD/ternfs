package lib

import (
	"fmt"
	"time"
	"xtx/eggsfs/msgs"
)

func WaitForBlockServices(ll *Logger, shuckleAddress string, expectedBlockServices int, timeout time.Duration) []msgs.BlockServiceInfo {
	var err error
	for {
		var resp msgs.ShuckleResponse
		var bss []msgs.BlockServiceInfo
		resp, err = ShuckleRequest(ll, nil, shuckleAddress, &msgs.AllBlockServicesReq{})
		if err != nil {
			ll.Debug("got error while getting block services from shuckle, will keep waiting: %v", err)
			goto KeepChecking
		}
		bss = resp.(*msgs.AllBlockServicesResp).BlockServices
		if len(bss) < expectedBlockServices {
			err = fmt.Errorf("not all block services are up yet, will keep waiting")
			ll.Debug("%v", err)
			goto KeepChecking
		}
		if len(bss) > expectedBlockServices {
			panic(fmt.Errorf("got more block services than expected (%v > %v)", len(bss), expectedBlockServices))
		}
		return bss
	KeepChecking:
		time.Sleep(10 * time.Millisecond)
	}
}

// getting a client implies having all shards and cdc.
func WaitForClient(log *Logger, shuckleAddress string, timeout time.Duration) {
	t0 := time.Now()
	var err error
	var client *Client
	for {
		t := time.Now()
		if t.Sub(t0) > timeout {
			panic(fmt.Errorf("giving up waiting for client, last error: %w", err))
		}
		client, err = NewClient(log, nil, shuckleAddress, 1)
		if err != nil {
			log.Info("getting shuckle client failed, waiting: %v", err)
			time.Sleep(time.Second)
			continue
		}
		client.Close()
		break
	}
}
