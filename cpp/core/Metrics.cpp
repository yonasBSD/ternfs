// See <https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/>
// for docs.
#include "Metrics.hpp"

#include "Assert.hpp"
#define CPPHTTPLIB_USE_POLL
#include "httplib.h"

#if 0

curl -vvv --request POST \
"http://REDACTED?org=restech&bucket=metrics&precision=ns" \
  --header "Content-Type: text/plain; charset=utf-8" \
  --header "Accept: application/json" \
  --data-binary 'eggsfs_fmazzol_test,sensor_id=TLM0201 temperature=73.97038159354763,humidity=35.23103248356096,co=0.48445310567793615 1690998953988422912'

curl -G 'http://REDACTED?pretty=true' --data-urlencode "db=metrics" --data-urlencode "q=SELECT \"value\" FROM \"disk_ecn_hostmon_disk_write_bytes\""

var TeamRestech = &Team{
	Name:             "restech",
	Domain:           "REDACTED",
	ManagementDomain: "REDACTED",
	XmonRota:         "restech",
	BootServerURLs:   []string{"https://REDACTED"},
	BuildServerURL:   "https://REDACTED",
	MetricsPrefix:    "restech",
	InfluxOrg:        "restech",
	InfluxDBURL:      "http://REDACTED",
}

#endif

// We're being overly conservative here, see
// <https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/#special-characters>,
// but better being safe than sorry.
static void validMetricsName(const std::string& name) {
    for (auto ch : name) {
        ALWAYS_ASSERT(
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == ':'
        );
    }
}

void MetricsBuilder::measurement(const std::string& name) {
    ALWAYS_ASSERT(_state == State::INIT || _state == State::TIMESTAMP);
    validMetricsName(name);
    if (_state == State::TIMESTAMP) {
        _payload += '\n';
    }
    _payload += name;
    _state = State::MEASUREMENT;
}

void MetricsBuilder::tagRaw(const std::string& name, const std::string& value) {
    ALWAYS_ASSERT(_state == State::MEASUREMENT || _state == State::TAGS);
    validMetricsName(name);
    validMetricsName(value);
    _payload += ',';
    _payload += name;
    _payload += '=';
    _payload += value;
    _state = State::TAGS;
}

void MetricsBuilder::fieldRaw(const std::string& name, const std::string& value) {
    ALWAYS_ASSERT(_state == State::MEASUREMENT || _state == State::TAGS || _state == State::FIELDS);
    validMetricsName(name);
    if (_state == State::MEASUREMENT || _state == State::TAGS) {
        _payload += ' ';
    } else {
        _payload += ',';
    }
    _payload += name;
    _payload += '=';
    _payload += value;
    _state = State::FIELDS;
}

void MetricsBuilder::timestamp(TernTime t) {
    ALWAYS_ASSERT(_state == State::FIELDS);
    _payload += ' ';
    static char buf[21];
    std::sprintf(buf, "%lu", t.ns);
    _payload += buf;
    _state = State::TIMESTAMP;
}

const std::string influxDBUrl = "http://REDACTED";
const std::string influxDBOrg = "restech";
const std::string influxDBBucket = "metrics";

std::string sendMetrics(Duration timeout, const std::string& payload) {
    httplib::Client cli(influxDBUrl);
    time_t ds = timeout.ns / 1'000'000'000ull;
    time_t dus = (timeout.ns % 1'000'000'000ull) / 1'000ull;
    cli.set_connection_timeout(ds, dus);
    cli.set_read_timeout(ds, dus);
    cli.set_write_timeout(ds, dus);

    std::string path = "/api/v2/write?org=" + influxDBOrg + "&bucket=" + influxDBBucket + "&precision=ns";
    const auto res = cli.Post(path, payload, "text/plain");
    if (res.error() != httplib::Error::Success) {
        std::ostringstream ss;
        ss << "Could not insert metrics to " << influxDBUrl << path << ": " << res.error();
        return ss.str();
    }
    return {};
}
