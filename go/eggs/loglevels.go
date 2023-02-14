package eggs

import (
	"fmt"
	"io"
	"log"
)

type LogLevel uint8

const TRACE LogLevel = 0
const DEBUG LogLevel = 1
const INFO LogLevel = 2
const ERROR LogLevel = 3

type Logger struct {
	logger *log.Logger
	level  LogLevel
}

func NewLoggerLogger(out io.Writer) *log.Logger {
	return log.New(out, "", log.Ldate|log.Ltime|log.Lmicroseconds|log.Lshortfile)
}

func NewLogger(verbose bool, out io.Writer) *Logger {
	return NewLoggerFromLogger(verbose, NewLoggerLogger(out))
}

func NewLoggerFromLogger(verbose bool, logger *log.Logger) *Logger {
	l := Logger{
		logger: logger,
	}
	if verbose {
		l.level = DEBUG
	} else {
		l.level = INFO
	}
	return &l
}

func (l *Logger) shouldLog(level LogLevel) bool {
	return level >= l.level
}

func (l *Logger) Log(level LogLevel, format string, v ...any) {
	if l.shouldLog(level) {
		l.logger.Output(2, fmt.Sprintf(format, v...))
	}
}

func (l *Logger) LogStack(calldepth int, level LogLevel, format string, v ...any) {
	if l.shouldLog(level) {
		l.logger.Output(2+calldepth, fmt.Sprintf(format, v...))
	}
}

func (l *Logger) Trace(format string, v ...any) {
	l.LogStack(1, TRACE, format, v...)
}

func (l *Logger) Debug(format string, v ...any) {
	l.LogStack(1, DEBUG, format, v...)
}

func (l *Logger) DebugStack(calldepth int, format string, v ...any) {
	l.LogStack(1+calldepth, DEBUG, format, v...)
}

func (l *Logger) Info(format string, v ...any) {
	l.LogStack(1, INFO, format, v...)
}

func (l *Logger) Error(format string, v ...any) {
	l.LogStack(1, ERROR, format, v...)
}

func (l *Logger) RaiseAlert(err any) {
	l.logger.Output(2, fmt.Sprintf("ALERT %v\n", err))
}

func (l *Logger) RaiseAlertStack(calldepth int, err any) {
	l.logger.Output(2+calldepth, fmt.Sprintf("ALERT %v\n", err))
}
