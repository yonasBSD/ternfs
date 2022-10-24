package loglevels

type LogLevels interface {
	Info(format string, v ...any)
	Debug(format string, v ...any)
	RaiseAlert(err error)
}
