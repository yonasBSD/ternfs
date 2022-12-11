#include <curl/curl.h>
#include <arpa/inet.h>

#include "Exception.hpp"
#include "Shuckle.hpp"
#include "json.hpp"

#define CURL_CHECKED(code) \
    do { \
        if (code != 0) { \
            throw CurlException(__LINE__, SHORT_FILE, removeTemplates(__PRETTY_FUNCTION__).c_str(), code); \
        } \
    } while (false)

#define CURL_CHECKED_MSG(code, ...) \
    do { \
        if (code != 0) { \
            throw CurlException(__LINE__, SHORT_FILE, removeTemplates(__PRETTY_FUNCTION__).c_str(), code, VALIDATE_FORMAT(__VA_ARGS__)); \
        } \
    } while (false)

class CurlException : public AbstractException {
public:
    template <typename ... Args>
    CurlException(int line, const char *file, const char *function, CURLcode code, const char *fmt, Args ... args) {
        std::stringstream ss;
        ss << "CurlException(" << file << "@" << line << ", " << code << "=" << curl_easy_strerror(code) << "):\n";
        format_pack(ss, fmt, args...);
        _msg = ss.str();
    }

    CurlException(int line, const char *file, const char *function, CURLcode code) {
        std::stringstream ss;
        ss << "CurlException(" << file << "@" << line << ", " << code << "=" << curl_easy_strerror(code) << ")";
        _msg = ss.str();
    }

    virtual const char *what() const throw() override {
        return _msg.c_str();
    };
private:
    std::string _msg;
};

struct WrappedCurl {
    CURL* curl;

    WrappedCurl(CURL* curl_) : curl(curl_) {
        if (curl == nullptr) {
            throw EGGS_EXCEPTION("could not initialize curl");
        }
    }

    ~WrappedCurl() {
        curl_easy_cleanup(curl);
    }

    CURL* operator->() {
        return curl;
    }
};

__attribute__((constructor))
static void curlGlobalInit() {
    CURL_CHECKED(curl_global_init(0));
}

static size_t curlWriteCallback(char* contents, size_t size, size_t nmemb, void* strPtr) {
    ALWAYS_ASSERT(size == 1);
    auto str = (std::string*)strPtr;
    str->append(contents, nmemb);
    return nmemb;
}

bool fetchBlockServices(const std::string& host, uint64_t timeout, std::string& errString, UpdateBlockServicesEntry& blocks) {
    WrappedCurl curl(curl_easy_init());

    std::string url = host + "/show_me_what_you_got";
    CURL_CHECKED(curl_easy_setopt(curl.curl, CURLOPT_URL, url.c_str()));
    CURL_CHECKED(curl_easy_setopt(curl.curl, CURLOPT_WRITEFUNCTION, curlWriteCallback));
    std::string responseBody;
    CURL_CHECKED(curl_easy_setopt(curl.curl, CURLOPT_WRITEDATA, &responseBody));
    char errbuf[CURL_ERROR_SIZE];
    CURL_CHECKED(curl_easy_setopt(curl.curl, CURLOPT_ERRORBUFFER, errbuf));
    CURL_CHECKED(curl_easy_setopt(curl.curl, CURLOPT_TIMEOUT_MS, timeout));

    {
        CURLcode code = curl_easy_perform(curl.curl);
        if (code != 0) {
            std::stringstream ss;
            ss << "curl failed with code " << code << "=" << curl_easy_strerror(code) << ": " << errbuf;
            errString = ss.str();
            return false;
        }
    }

    {
        using json = nlohmann::json;
        json j;
        try {
            j = json::parse(responseBody);
        } catch (json::parse_error e) {
            std::stringstream ss;
            ss << "could not parse response body: " << e.what();
            errString = ss.str();
            return false;
        }
        for (const auto& block: j) {
            auto& blockService = blocks.blockServices.els.emplace_back();
            blockService.id = block["id"];
            std::string ip = block["ip"];
            if (inet_pton(AF_INET, ip.c_str(), &blockService.ip.data[0]) != 1) {
                throw SYSCALL_EXCEPTION("inet_pton");
            }
            blockService.port = block["port"];
            std::string failureDomain = block["failure_domain"];
            ALWAYS_ASSERT(failureDomain.size() < blockService.failureDomain.data.size());
            memcpy(&blockService.failureDomain.data[0], failureDomain.c_str(), failureDomain.size()+1);
            std::string secretKey = block["secret_key"];
            ALWAYS_ASSERT(secretKey.size() == blockService.secretKey.data.size()*2);
            for (int i = 0; i < secretKey.size(); i += 2) {
                // TODO we should check that strtol didn't fail, which is very annoying
                // in its own right...
                blockService.secretKey.data[i/2] = strtol(secretKey.substr(i, 2).c_str(), nullptr, 16);
            }
            blockService.storageClass = block["storage_class"];
        }
    }

    return true;
}