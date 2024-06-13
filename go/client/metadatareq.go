package client

import (
	"sync/atomic"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

// Starts from 1, we use 0 as a placeholder in `requestIds`
func (c *Client) newRequestId() uint64 {
	return atomic.AddUint64(&c.requestIdCounter, 1)
}

func (c *Client) metadataRequest(
	log *lib.Logger,
	shid int16, // -1 for cdc
	reqBody bincode.Packable,
	respBody bincode.Unpackable,
	counters *ReqCounters,
	dontWait bool,
) error {
	attempts := 0
	respChan := make(chan *metadataProcessorResponse, 16)
	timeouts := c.shardTimeout
	startedAt := time.Now()
	requestId := c.newRequestId()
	// will keep trying as long as we get timeouts
	timeoutAlertQuietPeriod := 10 * time.Second
	if shid < 0 {
		timeouts = c.cdcTimeout
		// currently the CDC can be extremely slow as we sync stuff
		timeoutAlertQuietPeriod = time.Minute
	}
	timeoutAlert := log.NewNCAlert(timeoutAlertQuietPeriod)
	defer log.ClearNC(timeoutAlert)
	for {
		now := time.Now()
		timeout := timeouts.NextNow(startedAt, now)
		if timeout == 0 {
			log.RaiseAlert("giving up on request to shard %v after waiting for %v", shid, now.Sub(startedAt))
			return msgs.TIMEOUT
		}
		if counters != nil {
			atomic.AddUint64(&counters.Attempts, 1)
		}
		c.clientMetadata.incoming <- &metadataProcessorRequest{
			requestId: requestId,
			timeout:   timeout,
			shard:     shid,
			req:       reqBody,
			resp:      respBody,
			extra:     nil,
			respCh:    respChan,
		}
		if dontWait {
			log.Debug("dontWait is on, we've sent the request, goodbye")
			return nil
		}
		log.DebugStack(1, "waiting for response for req id %v on channel", requestId)
		resp := <-respChan
		if resp.err == msgs.TIMEOUT {
			log.RaiseNC(timeoutAlert, "timed out when receiving resp %v %+v of typ %T (started %v ago) to shard %v, might retry", requestId, reqBody, reqBody, now.Sub(startedAt), shid)
			attempts++
			continue
		}
		// At this point, we know we've got a response
		elapsed := time.Since(startedAt)
		if counters != nil {
			counters.Timings.Add(elapsed)
		}
		// If we're past the first attempt, there are cases where errors are not what they seem.
		var eggsError msgs.EggsError
		if resp.err != nil {
			var isEggsError bool
			eggsError, isEggsError = resp.err.(msgs.EggsError)
			if isEggsError && attempts > 0 {
				if shid >= 0 {
					eggsError = c.checkRepeatedShardRequestError(log, reqBody.(msgs.ShardRequest), respBody.(msgs.ShardResponse), eggsError)
				} else {
					eggsError = c.checkRepeatedCDCRequestError(log, reqBody.(msgs.CDCRequest), respBody.(msgs.CDCResponse), eggsError)
				}
			}
		}
		// Check if it's an error or not. We only use debug here because some errors are legitimate
		// responses (e.g. FILE_EMPTY)
		if eggsError != 0 {
			log.DebugStack(1, "got error %v for req %T id %v from shard %v (took %v)", eggsError, reqBody, requestId, shid, elapsed)
			return eggsError
		}
		log.Debug("got response %T from shard %v (took %v)", respBody, shid, elapsed)
		log.Trace("respBody %+v", respBody)
		return nil
	}
}
