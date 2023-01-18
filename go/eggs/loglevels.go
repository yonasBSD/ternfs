package eggs

import (
	"fmt"
	"io"
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

// Creates a logger with the formatting we want
func NewLogger(out io.Writer) *log.Logger {
	return log.New(out, "", log.Ldate|log.Ltime|log.Lmicroseconds|log.Lshortfile)
}

type LogLogger struct {
	Verbose bool
	Logger  *log.Logger
}

func (l *LogLogger) Info(format string, v ...any) {
	l.Logger.Output(2, fmt.Sprintf(format+"\n", v...))
}

func (l *LogLogger) Debug(format string, v ...any) {
	if l.Verbose {
		l.Logger.Output(2, fmt.Sprintf(format+"\n", v...))
	}
}

func (l *LogLogger) RaiseAlert(err error) {
	l.Logger.Output(2, fmt.Sprintf("ALERT %s\n", err))
}
