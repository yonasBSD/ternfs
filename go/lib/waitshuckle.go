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
		resp, err = ShuckleRequest(ll, shuckleAddress, &msgs.AllBlockServicesReq{})
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

func WaitForShardsCDC(ll *Logger, shuckleAddress string, expectedBlockServices int, timeout time.Duration) {
	t0 := time.Now()
	var err error
	for {
		if time.Since(t0) > timeout {
			panic(fmt.Errorf("giving up waiting for shuckle, last error: %v", err))
		}
		var resp msgs.ShuckleResponse
		// First check shards
		{
			resp, err = ShuckleRequest(ll, shuckleAddress, &msgs.ShardsReq{})
			if err != nil {
				ll.Debug("got error while getting shards from shuckle, will keep waiting: %v", err)
				goto KeepChecking
			}
			for i, shard := range resp.(*msgs.ShardsResp).Shards {
				if shard.Port1 == 0 {
					err = fmt.Errorf("shard %v isn't up yet, will keep waiting", i)
					ll.Debug("%v", err)
					goto KeepChecking
				}
			}
		}
		// Then check CDC
		{
			resp, err = ShuckleRequest(ll, shuckleAddress, &msgs.CdcReq{})
			if err != nil {
				ll.Debug("got error while getting CDC from shuckle, will keep waiting: %v", err)
				goto KeepChecking
			}
			cdc := resp.(*msgs.CdcResp)
			if cdc.Port1 == 0 {
				err = fmt.Errorf("CDC isn't up yet, will keep waiting")
				ll.Debug("%v", err)
				goto KeepChecking
			}
		}
		return
	KeepChecking:
		time.Sleep(10 * time.Millisecond)
	}
}
