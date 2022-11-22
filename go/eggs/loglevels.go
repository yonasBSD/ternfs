package eggs

import (
	"fmt"
	"log"
)

type LogLevels interface {
	Info(format string, v ...any)
	Debug(format string, v ...any)
	RaiseAlert(err error)
}

func Log(log LogLevels, debug bool, format string, v ...any) {
	if debug {
		log.Debug(format, v...)
	} else {
		log.Info(format, v...)
	}
}

type LogBlackHole struct{}

func (b LogBlackHole) Info(format string, v ...any)  {}
func (b LogBlackHole) Debug(format string, v ...any) {}
func (b LogBlackHole) RaiseAlert(err error)          {}

type LogToStdout struct {
	Verbose bool
}

func (*LogToStdout) Info(format string, v ...any) {
	fmt.Printf(format, v...)
	fmt.Println()
}

func (s *LogToStdout) Debug(format string, v ...any) {
	if s.Verbose {
		fmt.Printf(format, v...)
		fmt.Println()
	}
}

func (s *LogToStdout) RaiseAlert(err error) {
	fmt.Printf("ALERT %s\n", err)
}

type LogLogger struct {
	Verbose bool
	Logger  *log.Logger
}

func (l *LogLogger) Info(format string, v ...any) {
	l.Logger.Printf(format, v...)
	l.Logger.Println()
}

func (l *LogLogger) Debug(format string, v ...any) {
	if l.Verbose {
		l.Info(format, v...)
	}
}
