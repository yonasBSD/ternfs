package lib

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"net"
	"os"
	"strings"
	"sync/atomic"
	"time"
)

const (
	XMON_CREATE int32 = 0x4
	XMON_UPDATE int32 = 0x5
	XMON_CLEAR  int32 = 0x3
)

type xmonRequest struct {
	msgType     int32
	alertId     int64
	quietPeriod time.Duration
	binnable    bool
	message     string
	file        string
	line        int
	logLevel    LogLevel
}

type Xmon struct {
	appType          string
	appInstance      string
	hostname         string
	xmonAddr         string
	requests         chan xmonRequest
	onlyLogging      bool
	printQuietAlerts bool
}

type XmonConfig struct {
	// If this is true, the alerts won't actually be sent,
	// but it'll still log alert creation etc
	OnlyLogging      bool
	Prod             bool
	AppInstance      string
	AppType          string
	PrintQuietAlerts bool
}

func (x *Xmon) packString(buf *bytes.Buffer, s string) {
	if len(s) >= 1<<16 {
		panic(fmt.Errorf("string too long (%v)", len(s)))
	}
	binary.Write(buf, binary.BigEndian, uint16(len(s)))
	buf.Write([]byte(s))
}

const heartbeatIntervalSecs uint8 = 5

func (x *Xmon) packLogon(buf *bytes.Buffer) {
	// <https://REDACTED>
	// Name 	Type 	Description
	// Magic 	int16 	always 'T'
	// Version 	int32 	version number (latest version is 4)
	// Message type 	int32 	0x0
	// (unused) 	int32 	must be 0
	// Hostname 	string 	your hostname
	// Heartbeat interval 	byte 	in seconds, 0 for default (5s))
	// (unused) 	byte 	must be 0
	// (unused) 	int16 	must be 0
	// App type 	string 	defines rota and schedule
	// App inst 	string 	arbitrary identifier
	// Heap size 	int64 	(deprecated)
	// Num threads 	int32 	(deprecated)
	// Num errors 	int32 	(deprecated)
	// Mood 	int32 	current mood
	// Details JSON 	string 	(unused)
	buf.Reset()
	binary.Write(buf, binary.BigEndian, int16('T')) // magic
	binary.Write(buf, binary.BigEndian, int32(4))   // version
	binary.Write(buf, binary.BigEndian, int32(0x0)) // message type
	binary.Write(buf, binary.BigEndian, int32(0x0)) // unused
	x.packString(buf, x.hostname)                   // hostname
	buf.Write([]byte{heartbeatIntervalSecs})        // interval
	buf.Write([]byte{0})                            // unused
	binary.Write(buf, binary.BigEndian, int16(0))   // unused
	x.packString(buf, x.appType)                    // app type
	x.packString(buf, x.appInstance)                // app instance
	binary.Write(buf, binary.BigEndian, int64(0))   // heap size
	binary.Write(buf, binary.BigEndian, int32(0))   // num threads
	binary.Write(buf, binary.BigEndian, int32(0))   // num errors
	binary.Write(buf, binary.BigEndian, int32(0))   // happy mood
	x.packString(buf, "")                           // details
}

func (x *Xmon) packUpdate(buf *bytes.Buffer) {
	buf.Reset()
	// Message type 	int32 	0x1
	// (unused) 	int32 	must be 0
	// Heap size 	int64 	(deprecated)
	// Num threads 	int32 	(deprecated)
	// Num errors 	int32 	(deprecated)
	// Mood 	int32 	updated mood
	binary.Write(buf, binary.BigEndian, int32(0x1))
	binary.Write(buf, binary.BigEndian, int32(0))
	binary.Write(buf, binary.BigEndian, int64(0))
	binary.Write(buf, binary.BigEndian, int32(0))
	binary.Write(buf, binary.BigEndian, int32(0))
	binary.Write(buf, binary.BigEndian, int32(0)) // happy mood
}

func (x *Xmon) packRequest(buf *bytes.Buffer, req *xmonRequest) {
	if req.alertId < 0 {
		panic(fmt.Errorf("bad alert id %v", req.alertId))
	}
	buf.Reset()
	binary.Write(buf, binary.BigEndian, req.msgType)
	binary.Write(buf, binary.BigEndian, req.alertId)
	if req.msgType == XMON_CREATE || req.msgType == XMON_UPDATE {
		binary.Write(buf, binary.BigEndian, req.binnable)
		x.packString(buf, req.message)
	}
}

const maxBinnableAlerts int = 20

// alert in quiet period
type quietAlert struct {
	quietUntil time.Time
	message    string
}

func (x *Xmon) run(log *Logger) {
	buffer := bytes.NewBuffer([]byte{})
	requestsCap := uint64(4096)
	requestsMask := requestsCap - 1
	requests := make([]xmonRequest, requestsCap)
	requestsHead := uint64(0)
	requestsTail := uint64(0)
	var binnableAlerts map[int64]struct{}
	if !x.onlyLogging {
		binnableAlerts = make(map[int64]struct{})
	}
	// alerts in quiet period
	quietAlerts := make(map[int64]*quietAlert)

	var conn *net.TCPConn
	defer func() {
		if conn != nil {
			conn.Close()
		}
	}()
	var err error

Reconnect:
	if x.onlyLogging {
		log.Info("using xmon only for logging")
	} else {
		// close previous conn
		if conn != nil {
			conn.Close()
			conn = nil
		}
		// delay if we're recovering after an error
		if err != nil {
			log.Info("about to reconnect to Xmon (%v) after err %v, waiting for 1 sec first", x.xmonAddr, err)
			time.Sleep(time.Second)
		} else {
			log.Info("connecting to Xmon (%v)", x.xmonAddr)
		}
		// connect
		var someConn net.Conn
		someConn, err = net.Dial("tcp", x.xmonAddr)
		if err != nil {
			log.ErrorNoAlert("could not connect to xmon, will reconnect: %v", err)
			goto Reconnect
		}
		conn = someConn.(*net.TCPConn)
		// send logon message
		x.packLogon(buffer)
		if _, err = buffer.WriteTo(conn); err != nil {
			log.ErrorNoAlert("could not logon to xmon, will reconnect: %v", err)
			goto Reconnect
		}
		log.Info("connected to xmon, logon sent")
	}
	// we're in, start loop
	gotHeartbeatAt := time.Time{}
	quietAlertsLevel := DEBUG
	if x.printQuietAlerts {
		quietAlertsLevel = ERROR
	}
	for {
		// reconnect when too much time has passed
		if !x.onlyLogging && !gotHeartbeatAt.Equal(time.Time{}) && time.Since(gotHeartbeatAt) > time.Second*2*time.Duration(heartbeatIntervalSecs) {
			log.Info("heartbeat deadline has passed, will reconnect")
			gotHeartbeatAt = time.Time{}
			goto Reconnect
		}

		// fetch a bunch of requests and then send them out
		if x.onlyLogging || !gotHeartbeatAt.Equal(time.Time{}) {
			now := time.Now()
			// unquiet alerts that are due
			for aid, alert := range quietAlerts {
				if alert.quietUntil.Before(now) {
					delete(quietAlerts, aid)
					requests[requestsTail&requestsMask] = xmonRequest{
						msgType:  XMON_CREATE,
						alertId:  aid,
						binnable: false,
						message:  alert.message,
					}
					requestsTail++
				}
			}
			// fill in requests from channel
			for requestsTail-requestsHead < requestsCap {
				select {
				case requests[requestsTail&requestsMask] = <-x.requests:
					requestsTail++
				default:
					goto NoMoreRequests
				}
			}
		NoMoreRequests:
			// send them out
			for requestsHead < requestsTail {
				req := &requests[requestsHead&requestsMask]
				switch req.msgType {
				case XMON_CREATE:
					if req.quietPeriod > 0 {
						if req.binnable {
							panic(fmt.Errorf("got alert create with quietPeriod=%v, but it is binnable", req.quietPeriod))
						}
						log.LogLocation(quietAlertsLevel, req.file, req.line, "quiet non-binnable alertId=%v message=%q quietPeriod=%v, will wait", req.alertId, req.message, req.quietPeriod)
						quietAlerts[req.alertId] = &quietAlert{
							message:    req.message,
							quietUntil: now.Add(req.quietPeriod),
						}
						goto SkipRequest
					} else if req.binnable && !x.onlyLogging && len(binnableAlerts) > maxBinnableAlerts {
						log.LogLocation(ERROR, req.file, req.line, "skipping create alert, alertId=%v binnable=%v message=%q, too many already", req.alertId, req.binnable, req.message)
						// if we don't have the "too many alerts" alert, create it
						if _, ok := binnableAlerts[tooManyAlertsAlertId]; !ok {
							req = &xmonRequest{
								msgType:  XMON_CREATE,
								alertId:  tooManyAlertsAlertId,
								message:  "too many alerts, alerts dropped",
								binnable: true,
							}
						} else {
							goto SkipRequest
						}
					} else {
						log.LogLocation(req.logLevel, req.file, req.line, "creating alert, alertId=%v binnable=%v message=%q", req.alertId, req.binnable, req.message)
					}
				case XMON_UPDATE:
					if req.binnable {
						panic(fmt.Errorf("unexpected update to non-binnable alert"))
					}
					quiet := quietAlerts[req.alertId]
					if quiet != nil {
						log.LogLocation(quietAlertsLevel, req.file, req.line, "skipping update alertId=%v message=%q since it's quiet until %v", req.alertId, req.message, quiet.quietUntil)
						quiet.message = req.message
						goto SkipRequest
					}
					log.LogLocation(req.logLevel, req.file, req.line, "updating alert, alertId=%v binnable=%v message=%q", req.alertId, req.binnable, req.message)
				case XMON_CLEAR:
					if req.binnable {
						panic(fmt.Errorf("unexpected clear to non-binnable alert"))
					}
					quiet := quietAlerts[req.alertId]
					if quiet != nil {
						log.LogLocation(DEBUG, req.file, req.line, "skipping clear alertId=%v lastMessage=%q since it's quiet until %v", req.alertId, req.message, quiet.quietUntil)
						delete(quietAlerts, req.alertId)
						goto SkipRequest
					}
					log.LogLocation(INFO, req.file, req.line, "sending clear alert, alertId=%v lastMessage=%v", req.alertId, req.message)
				default:
					panic(fmt.Errorf("bad req type %v", req.msgType))
				}
				if !x.onlyLogging {
					x.packRequest(buffer, req)
					if _, err = buffer.WriteTo(conn); err != nil {
						log.ErrorNoAlert("could not write request to xmon: %v", err)
						// note that we haven't removed the request from the ring buffer yet
						goto Reconnect
					}
					switch req.msgType {
					case XMON_CREATE:
						if req.binnable {
							binnableAlerts[req.alertId] = struct{}{}
						}
					}
				}
			SkipRequest:
				requestsHead++
			}
		}
		// read all responses, not waiting too long
		if x.onlyLogging {
			time.Sleep(time.Millisecond * 100)
		} else {
			for {
				if err = conn.SetReadDeadline(time.Now().Add(time.Millisecond * 100)); err != nil {
					log.ErrorNoAlert("could not set deadline: %v", err)
					goto Reconnect
				}
				var respType int32
				if err = binary.Read(conn, binary.BigEndian, &respType); err != nil {
					if netErr, ok := err.(net.Error); ok && netErr.Timeout() { // nothing to read
						break
					}
					log.ErrorNoAlert("could not read request from xmon: %v", err)
					goto Reconnect
				}
				switch respType {
				case 0x0:
					if gotHeartbeatAt.Equal(time.Time{}) {
						log.Info("got first xmon heartbeat, we're in")
					}
					gotHeartbeatAt = time.Now()
					x.packUpdate(buffer)
					if _, err = buffer.WriteTo(conn); err != nil {
						log.ErrorNoAlert("cold not send update to xmon: %v", err)
						goto Reconnect
					}
				case 0x1:
					// we need to wait for the rest
					if err = conn.SetReadDeadline(time.Time{}); err != nil {
						log.ErrorNoAlert("could not set deadline: %v", err)
						goto Reconnect
					}
					var alertId int64
					if err = binary.Read(conn, binary.BigEndian, &alertId); err != nil {
						log.ErrorNoAlert("could not read alert id: %v", err)
						goto Reconnect
					}
					delete(binnableAlerts, alertId)
					log.Info("UI cleared alert alertId=%v", alertId)
				default:
					panic(fmt.Errorf("bad message type %v", respType))
				}
			}
		}
	}
}

func NewXmon(log *Logger, config *XmonConfig) (*Xmon, error) {
	var x *Xmon
	if config.OnlyLogging {
		x = &Xmon{
			requests:         make(chan xmonRequest, 4096),
			printQuietAlerts: config.PrintQuietAlerts,
			onlyLogging:      true,
		}
	} else {
		if config.AppInstance == "" {
			panic(fmt.Errorf("empty app instance"))
		}
		hostname, err := os.Hostname()
		if err != nil {
			return nil, err
		}
		hostname = strings.Split(hostname, ".")[0]
		x = &Xmon{
			appType:          config.AppType,
			appInstance:      config.AppInstance + "@" + hostname,
			hostname:         hostname,
			requests:         make(chan xmonRequest, 4096),
			printQuietAlerts: config.PrintQuietAlerts,
		}
		if config.Prod {
			x.xmonAddr = "REDACTED"
		} else {
			x.xmonAddr = "REDACTED"
		}
	}
	go x.run(log)
	return x, nil
}

type XmonNCAlert struct {
	alertId     int64 // -1 if we haven't got one yet
	lastMessage string
	quietPeriod time.Duration
}

func (x *Xmon) NewNCAlert(quietPeriod time.Duration) *XmonNCAlert {
	return &XmonNCAlert{
		alertId:     -1,
		quietPeriod: quietPeriod,
	}
}

const tooManyAlertsAlertId = int64(0)

var alertIdCount = int64(1)

func xmonRaiseStack(log *Logger, xmon *Xmon, logLevel LogLevel, calldepth int, alertId *int64, binnable bool, quietPeriod time.Duration, format string, v ...any) string {
	file, line := getFileLine(1 + calldepth)
	message := fmt.Sprintf("%s:%d "+format, append([]any{file, line}, v...)...)
	if binnable || quietPeriod == 0 {
		log.LogLocation(logLevel, file, line, message)
	}
	if *alertId < 0 {
		*alertId = atomic.AddInt64(&alertIdCount, 1)
		xmon.requests <- xmonRequest{
			msgType:     XMON_CREATE,
			alertId:     *alertId,
			quietPeriod: quietPeriod,
			binnable:    binnable,
			message:     message,
			file:        file,
			line:        line,
			logLevel:    logLevel,
		}
	} else {
		xmon.requests <- xmonRequest{
			msgType:     XMON_UPDATE,
			alertId:     *alertId,
			quietPeriod: quietPeriod,
			binnable:    binnable,
			message:     message,
			file:        file,
			line:        line,
			logLevel:    logLevel,
		}
	}
	return message
}

func (x *Xmon) RaiseStack(log *Logger, xmon *Xmon, logLevel LogLevel, calldepth int, format string, v ...any) {
	alertId := int64(-1)
	xmonRaiseStack(log, x, logLevel, 1+calldepth, &alertId, true, 0, format, v...)
}

func (x *Xmon) Raise(log *Logger, xmon *Xmon, logLevel LogLevel, format string, v ...any) {
	alertId := int64(-1)
	xmonRaiseStack(log, x, logLevel, 1, &alertId, true, 0, format, v...)
}

func (a *XmonNCAlert) RaiseStack(log *Logger, xmon *Xmon, logLevel LogLevel, calldepth int, format string, v ...any) {
	a.lastMessage = xmonRaiseStack(log, xmon, logLevel, 1+calldepth, &a.alertId, false, a.quietPeriod, format, v...)
}

func (a *XmonNCAlert) Raise(log *Logger, xmon *Xmon, logLevel LogLevel, format string, v ...any) {
	a.lastMessage = xmonRaiseStack(log, xmon, logLevel, 1, &a.alertId, false, a.quietPeriod, format, v...)
}

func (a *XmonNCAlert) Clear(log *Logger, xmon *Xmon) {
	if a.alertId < 0 {
		return
	}
	xmon.requests <- xmonRequest{
		msgType: XMON_CLEAR,
		alertId: a.alertId,
		message: a.lastMessage,
	}
	a.alertId = -1
}
