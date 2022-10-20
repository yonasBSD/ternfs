package alerts

type Alerter interface {
	RaiseAlert(err error)
}
