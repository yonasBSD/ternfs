package client

import (
	"fmt"
	"time"
	"xtx/ternfs/lib"
	"xtx/ternfs/msgs"
)

func WaitForBlockServices(ll *lib.Logger, shuckleAddress string, expectedBlockServices int, waitCurrentServicesCalcuation bool, timeout time.Duration) []msgs.BlockServiceDeprecatedInfo {
	var err error
	for {
		var resp msgs.ShuckleResponse
		var bss []msgs.BlockServiceDeprecatedInfo
		resp, err = ShuckleRequest(ll, nil, shuckleAddress, &msgs.AllBlockServicesDeprecatedReq{})
		if err != nil {
			ll.Debug("got error while getting block services from shuckle, will keep waiting: %v", err)
			goto KeepChecking
		}
		bss = resp.(*msgs.AllBlockServicesDeprecatedResp).BlockServices
		if len(bss) < expectedBlockServices {
			err = fmt.Errorf("not all block services are up yet, will keep waiting")
			ll.Debug("%v", err)
			goto KeepChecking
		}

		if waitCurrentServicesCalcuation {
			resp, err = ShuckleRequest(ll, nil, shuckleAddress, &msgs.ShardBlockServicesDEPRECATEDReq{0})
			if err != nil || len(resp.(*msgs.ShardBlockServicesDEPRECATEDResp).BlockServices) == 0 {
				ll.Debug("current block services not calculated, will keep waiting")
				goto KeepChecking
			}
		}
		return bss
	KeepChecking:
		time.Sleep(10 * time.Millisecond)
	}
}

func WaitForShuckle(ll *lib.Logger, shuckleAddress string, timeout time.Duration) error {
	t0 := time.Now()
	for {
		_, err := ShuckleRequest(ll, nil, shuckleAddress, &msgs.InfoReq{})
		if err == nil {
			return nil
		}
		if time.Since(t0) > timeout {
			return err
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// getting a client implies having all shards and cdc.
func WaitForClient(log *lib.Logger, shuckleAddress string, timeout time.Duration) {
	t0 := time.Now()
	var err error
	var client *Client
	for {
		t := time.Now()
		if t.Sub(t0) > timeout {
			panic(fmt.Errorf("giving up waiting for client, last error: %w", err))
		}
		client, err = NewClient(log, nil, shuckleAddress, msgs.AddrsInfo{})
		if err != nil {
			log.Info("getting shuckle client failed, waiting: %v", err)
			time.Sleep(time.Second)
			continue
		}
		client.Close()
		break
	}
}
