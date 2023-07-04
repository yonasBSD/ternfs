package lib

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"

	"xtx/ecninfra/log"
	"xtx/ecninfra/monitor"

	"github.com/sirupsen/logrus"
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

type logFormatter struct {
	syslog    bool
	hasColors bool
	logrus.Formatter
}

const (
	red    = 31
	yellow = 33
	blue   = 36
	gray   = 37

	syslogDebug = 7
	syslogInfo  = 6
	syslogWarn  = 4
	syslogError = 3
)

func (lf *logFormatter) Format(entry *logrus.Entry) ([]byte, error) {
	f, fe := entry.Data["file"]
	n, ne := entry.Data["line"]
	caller := "???:0 "
	if fe && ne {
		caller = fmt.Sprintf("%s:%d ", f, n)
	}

	var b *bytes.Buffer
	if entry.Buffer != nil {
		b = entry.Buffer
	} else {
		b = &bytes.Buffer{}
	}

	var levelColor int
	var syslogPrio int
	switch entry.Level {
	case logrus.DebugLevel, logrus.TraceLevel:
		levelColor = gray
		syslogPrio = syslogDebug
	case logrus.WarnLevel:
		levelColor = yellow
		syslogPrio = syslogWarn
	case logrus.ErrorLevel, logrus.FatalLevel, logrus.PanicLevel:
		levelColor = red
		syslogPrio = syslogError
	case logrus.InfoLevel:
		levelColor = blue
		syslogPrio = syslogInfo
	default:
		levelColor = blue
		syslogPrio = syslogInfo
	}

	levelText := strings.ToUpper(entry.Level.String())

	if lf.syslog {
		fmt.Fprintf(b, "<%d>%s", syslogPrio, entry.Message)
	} else {
		cs := ""
		ce := ""
		if lf.hasColors {
			cs = fmt.Sprintf("\x1b[%dm", levelColor)
			ce = "\x1b[0m"
		}

		fmt.Fprintf(b, "%-26s %s[%s%s%s] %s ", entry.Time.Format("2006-01-02T15:04:05.999999"), caller, cs, levelText, ce, entry.Message)
	}
	b.WriteByte('\n')
	return b.Bytes(), nil
}

type LoggerOptions struct {
	Level       LogLevel
	Syslog      bool
	AppInstance string
	Xmon        string // "dev", "qa", empty string for no xmon
}

type Logger struct {
	level         LogLevel
	troll         *monitor.ParentTroll
	alertsLock    sync.RWMutex
	raisedAlerts  map[string]*monitor.AlertStatus
	droppedAlerts *monitor.AlertStatus
}

func isTerminal(f *os.File) bool {
	if f == nil {
		return false
	}
	fd := int(f.Fd())
	_, err := unix.IoctlGetTermios(fd, unix.TCGETS)
	return err == nil
}

var hasLogger int32

// Note: this will really use the global logger in `xtx/ecninfra/log`, so
// this function can only be called once.
func NewLogger(
	out *os.File,
	options *LoggerOptions,
) *Logger {
	logger := Logger{
		level:        options.Level,
		alertsLock:   sync.RWMutex{},
		raisedAlerts: make(map[string]*monitor.AlertStatus),
	}

	if !atomic.CompareAndSwapInt32(&hasLogger, 0, 1) {
		panic(fmt.Errorf("NewLogger called twice"))
	}

	ll := logrus.InfoLevel
	switch options.Level {
	case ERROR:
		ll = logrus.ErrorLevel
	case DEBUG:
		ll = logrus.DebugLevel
	case TRACE:
		ll = logrus.TraceLevel
	}
	f := logFormatter{
		Formatter: &logrus.TextFormatter{},
		hasColors: isTerminal(out),
		syslog:    options.Syslog,
	}
	log.SetFormatter(&f)
	log.SetLevel(ll)
	log.SetOutput(out)

	if options.Xmon != "" {
		if options.Xmon != "prod" && options.Xmon != "qa" {
			panic(fmt.Errorf("invalid xmon environment %q", options.Xmon))
		}
		hn, err := os.Hostname()
		if err != nil {
			panic(err)
		}
		hostname := strings.Split(hn, ".")[0]

		appType := "restech.info"
		prod := options.Xmon == "prod"
		appInstance := fmt.Sprintf("%s@%s", options.AppInstance, hostname)
		xh := monitor.XmonHost(prod)
		troll := monitor.NewTroll(xh, hostname, appType, appInstance, 1000)
		// The call below will log the info message once connected to xmon.
		troll.Connect()
		logger.troll = troll
		logger.droppedAlerts = troll.NewAlertStatus()
	}

	return &logger
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
		e := log.WithFields(logrus.Fields{
			"file": file,
			"line": line,
		})
		switch level {
		case INFO:
			e.Infof(format, v...)
		case ERROR:
			e.Errorf(format, v...)
		case DEBUG:
			e.Debugf(format, v...)
		case TRACE:
			e.Tracef(format, v...)
		}

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

func (l *Logger) alert(msg string) {
	if l.troll == nil {
		return
	}
	l.alertsLock.Lock()
	defer l.alertsLock.Unlock()
	// Deduplicate by alert message
	_, ok := l.raisedAlerts[msg]
	if ok {
		return
	}

	if len(l.raisedAlerts) > 10 {
		l.droppedAlerts.Alert("Alert limit exceeded, some have been dropped")
		return
	}
	l.droppedAlerts.Clear()

	binCb := func(alertID int64) {
		l.alertsLock.Lock()
		defer l.alertsLock.Unlock()
		delete(l.raisedAlerts, msg)
	}
	a := l.troll.Alert(msg, true, binCb)
	l.raisedAlerts[msg] = a
}

func (l *Logger) Error(format string, v ...any) {
	l.alert(fmt.Sprintf(format, v...))
	l.LogStack(1, ERROR, format, v...)
}

func (l *Logger) RaiseAlert(err any) {
	msg := fmt.Sprintf("ALERT %v", err)
	l.alert(msg)
	l.LogStack(1, ERROR, msg)
}

func (l *Logger) RaiseAlertStack(calldepth int, err any) {
	msg := fmt.Sprintf("ALERT %v", err)
	l.alert(msg)
	l.LogStack(1+calldepth, ERROR, "ALERT %v", err)
}

// NCAlert is a non binnable alert
type NCAlert struct {
	alert     *monitor.AlertStatus
	l         *Logger
	lastAlert string
}

func (l *Logger) NewNCAlert() NCAlert {
	var a *monitor.AlertStatus
	if l.troll != nil {
		a = l.troll.NewUnbinnableAlertStatus()
	}
	return NCAlert{alert: a, l: l}
}

func (nc *NCAlert) Alert(f string, v ...any) {
	nc.l.LogStack(1, ERROR, "nc alert: "+f, v...)
	nc.lastAlert = fmt.Sprintf(f, v...)
	if nc.alert == nil {
		return
	}
	nc.alert.Alertf(f, v...)
}

func (nc *NCAlert) Clear() {
	if len(nc.lastAlert) > 0 {
		nc.l.LogStack(1, INFO, "cleared nc alert: %s", nc.lastAlert)
		nc.lastAlert = ""
	}
	if nc.alert == nil {
		return
	}
	if nc.alert.IsRaised() {
		nc.alert.Clear()
	}
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
