package lib

type GCOptions struct {
	ShuckleTimeouts        *ReqTimeouts
	ShardTimeouts          *ReqTimeouts
	CDCTimeouts            *ReqTimeouts
	Counters               *ClientCounters
	RetryOnDestructFailure bool
}

func GCClient(log *Logger, shuckleAddress string, options *GCOptions) (*Client, error) {
	client, err := NewClient(log, options.ShuckleTimeouts, shuckleAddress)
	if err != nil {
		return nil, err
	}
	if options.ShardTimeouts != nil {
		client.SetShardTimeouts(options.ShardTimeouts)
	}
	if options.CDCTimeouts != nil {
		client.SetCDCTimeouts(options.CDCTimeouts)
	}
	if options.Counters != nil {
		client.SetCounters(options.Counters)
	}
	return client, nil
}
