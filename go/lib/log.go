package lib

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"runtime"
	"sync"
	"time"

	"golang.org/x/sys/unix"
)

type LogLevel uint8

const TRACE LogLevel = 0
const DEBUG LogLevel = 1
const INFO LogLevel = 2
const ERROR LogLevel = 3

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

const (
	red    = 31
	yellow = 33
	blue   = 36

	syslogDebug = 7
	syslogInfo  = 6
	syslogWarn  = 4
	syslogError = 3
)

type LoggerOptions struct {
	Level       LogLevel
	Syslog      bool
	AppName     string
	AppInstance string
	Xmon        string // "dev", "qa", empty string for no xmon
}

type Logger struct {
	level     LogLevel
	hasColors bool
	syslog    bool
	xmon      *Xmon
	mu        sync.Mutex
	bufPool   sync.Pool
	out       io.Writer
}

func isTerminal(f *os.File) bool {
	if f == nil {
		return false
	}
	fd := int(f.Fd())
	_, err := unix.IoctlGetTermios(fd, unix.TCGETS)
	return err == nil
}

func (log *Logger) formatLog(level LogLevel, time time.Time, file string, line int, format string, v ...any) {
	var levelColor int
	var syslogPrio int
	switch level {
	case DEBUG, TRACE:
		levelColor = 0 // reset
		syslogPrio = syslogDebug
	case ERROR:
		levelColor = red
		syslogPrio = syslogError
	case INFO:
		levelColor = blue
		syslogPrio = syslogInfo
	default:
		panic(fmt.Errorf("bad loglevel %v", level))
	}

	cs := ""
	ce := ""
	if log.hasColors {
		cs = fmt.Sprintf("\x1b[%dm", levelColor)
		ce = "\x1b[0m"
	}

	buf := log.bufPool.Get().(*bytes.Buffer)
	if log.syslog {
		fmt.Fprintf(buf, "<%d>", syslogPrio)
		fmt.Fprintf(buf, format, v...)
		fmt.Fprintln(buf)
	} else {
		fmt.Fprintf(buf, "%-26s %s:%d [%s%s%s] ", time.Format("2006-01-02T15:04:05.999999"), file, line, cs, level.String(), ce)
		fmt.Fprintf(buf, format, v...)
		fmt.Fprintln(buf)
	}

	log.mu.Lock()
	log.out.Write(buf.Bytes())
	log.mu.Unlock()

	buf.Reset()
	log.bufPool.Put(buf)
}

func NewLogger(
	out *os.File,
	options *LoggerOptions,
) *Logger {
	logger := &Logger{
		level:     options.Level,
		hasColors: isTerminal(out),
		syslog:    options.Syslog,
		bufPool: sync.Pool{
			New: func() any {
				return bytes.NewBuffer([]byte{})
			},
		},
		out: out,
	}

	if options.Xmon != "" {
		if options.Xmon != "prod" && options.Xmon != "qa" {
			panic(fmt.Errorf("invalid xmon environment %q", options.Xmon))
		}
		ai := options.AppName
		if len(options.AppInstance) > 0 {
			ai = fmt.Sprintf("%s:%s", options.AppName, options.AppInstance)
		}
		var err error
		logger.xmon, err = NewXmon(logger, &XmonConfig{
			Prod:        options.Xmon == "prod",
			AppInstance: ai,
		})
		if err != nil {
			panic(err)
		}
	}

	return logger
}

func (l *Logger) Level() LogLevel {
	return l.level
}

func (l *Logger) shouldLog(level LogLevel) bool {
	return level >= l.level
}

func (l *Logger) Log(level LogLevel, format string, v ...any) {
	l.LogStack(1, level, format, v...)
}

func getFileLine(calldepth int) (string, int) {
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

	return file, line
}

func (l *Logger) LogStack(calldepth int, level LogLevel, format string, v ...any) {
	if l.shouldLog(level) {
		file, line := getFileLine(1 + calldepth)
		l.formatLog(level, time.Now(), file, line, format, v...)
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

// There should be very few times where you want an alert but
// not an error.
func (l *Logger) ErrorNoAlert(format string, v ...any) {
	l.LogStack(1, ERROR, format, v...)
}

func (l *Logger) NewNCAlert() *XmonNCAlert {
	if l.xmon == nil {
		return &XmonNCAlert{}
	} else {
		return l.xmon.NewNCAlert()
	}
}

func (l *Logger) RaiseAlertStack(calldepth int, format string, v ...any) {
	l.LogStack(1+calldepth, ERROR, "ALERT "+format, v...)
	if l.xmon == nil {
		return
	}
	l.xmon.RaiseStack(l, l.xmon, 1+calldepth, format, v...)
}

func (l *Logger) RaiseAlert(format string, v ...any) {
	l.RaiseAlertStack(1, format, v...)
}

func (l *Logger) RaiseNCStack(alert *XmonNCAlert, calldepth int, format string, v ...any) {
	l.LogStack(1+calldepth, ERROR, "ALERT "+format, v...)
	if l.xmon == nil {
		return
	}
	alert.RaiseStack(l, l.xmon, 1+calldepth, format, v...)
}

func (l *Logger) RaiseNC(alert *XmonNCAlert, format string, v ...any) {
	l.RaiseNCStack(alert, 1, format, v...)
}

func (l *Logger) ClearNC(alert *XmonNCAlert) {
	if alert.lastMessage != "" {
		l.LogStack(1, INFO, "clearing alert, last message %q", alert.lastMessage)
	}
	if l.xmon == nil {
		return
	}
	alert.Clear(l, l.xmon)
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
			sink.logger.LogStack(2, sink.level, string(bytes[lineBegin:lineEnd]))
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
