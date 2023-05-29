package lib

import (
	"bytes"
	"fmt"
	"io"
	"runtime"
	"sync"
	"time"
)

type LogLevel uint8

const TRACE LogLevel = 0
const DEBUG LogLevel = 1
const INFO LogLevel = 2
const ERROR LogLevel = 3

func (ll LogLevel) Syslog() int {
	switch ll {
	case TRACE, DEBUG:
		return 7
	case INFO:
		return 6
	case ERROR:
		return 3
	default:
		return 3
	}
}

func (ll LogLevel) String() string {
	switch ll {
	case TRACE:
		return "TRACE"
	case DEBUG:
		return "DEBUG"
	case INFO:
		return "INFO"
	case ERROR:
		return "ERROR"
	default:
		return fmt.Sprintf("UNKNOWN(%v)", uint8(ll))
	}

}

type Logger struct {
	out    io.Writer
	mu     sync.Mutex
	level  LogLevel
	syslog bool
}

func NewLogger(level LogLevel, out io.Writer, syslog bool) *Logger {
	l := Logger{
		out:    out,
		level:  level,
		syslog: syslog,
	}
	return &l
}

func (l *Logger) shouldLog(level LogLevel) bool {
	return level >= l.level
}

func (l *Logger) Log(level LogLevel, format string, v ...any) {
	l.LogStack(1, level, format, v...)
}

func (l *Logger) LogStack(calldepth int, level LogLevel, format string, v ...any) {
	if l.shouldLog(level) {
		// get file
		_, file, line, ok := runtime.Caller(1 + calldepth)
		if !ok {
			file = "???"
			line = 0
		}
		short := file
		for i := len(file) - 1; i > 0; i-- {
			if file[i] == '/' {
				short = file[i+1:]
				break
			}
		}
		file = short

		// write out
		l.mu.Lock()
		defer l.mu.Unlock()
		if l.syslog {
			fmt.Fprintf(l.out, "<%d>%s:%d ", level.Syslog(), file, line)
		} else {
			fmt.Fprintf(l.out, "%s [%v] %s:%d ", time.Now().Format("2006-01-02T15:04:05.999999999"), level, file, line)
		}
		fmt.Fprintf(l.out, format, v...)
		l.out.Write([]byte("\n"))
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
	l.LogStack(1, ERROR, "ALERT %v", err)
}

func (l *Logger) RaiseAlertStack(calldepth int, err any) {
	l.LogStack(1+calldepth, ERROR, "ALERT %v", err)
}

type loggerSink struct {
	logger *Logger
	level  LogLevel
	buf    *bytes.Buffer
}

func (sink *loggerSink) Write(p []byte) (int, error) {
	if !sink.logger.shouldLog(sink.level) {
		return len(p), nil
	}
	lineBegin := 0
	lineEnd := sink.buf.Len()
	sink.buf.Write(p)
	bytes := sink.buf.Bytes()
	for lineEnd < len(bytes) {
		if bytes[lineEnd] == '\n' {
			sink.logger.LogStack(2, sink.level, string(bytes[lineBegin:lineEnd+1]))
			lineBegin = lineEnd + 1
		}
		lineEnd++
	}
	sink.buf.Next(lineBegin)
	return len(p), nil
}

func (l *Logger) Sink(level LogLevel) io.Writer {
	return &loggerSink{
		logger: l,
		level:  level,
		buf:    bytes.NewBuffer([]byte{}),
	}
}
