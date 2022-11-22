#include <algorithm>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "Crypto.hpp"

#include "Bincode.hpp"

#define OPENSSL_EXCEPTION() OpenSSLException(__LINE__, SHORT_FILE, removeTemplates(__PRETTY_FUNCTION__).c_str(), ERR_get_error())

class OpenSSLException : public AbstractException {
public:
    OpenSSLException(int line, const char *file, const char *function, unsigned long err);
    virtual const char *what() const throw() override;
private:
    std::string _msg;
};

OpenSSLException::OpenSSLException(int line, const char *file, const char *function, unsigned long err) {
    char errStrBuf[256];
    ERR_error_string_n(err, errStrBuf, sizeof(errStrBuf));

    std::stringstream ss;
    ss << "OpenSSLException(" << file << "@" << line << ", " << err << "=" << errStrBuf << " in " << function << ")";
    _msg = ss.str();
}

const char* OpenSSLException::what() const throw() {
    return _msg.c_str();
}

void generateSecretKey(std::array<uint8_t, 16>& key) {
    if (RAND_priv_bytes((unsigned char*)&key[0], sizeof(key)) != 1) {
        throw OPENSSL_EXCEPTION();
    }
}

void cbcmac(const std::array<uint8_t, 16>& key, const uint8_t* data, size_t len, std::array<uint8_t, 8>& mac) {
    AES_KEY expandedKey;
    ALWAYS_ASSERT(
        AES_set_encrypt_key(&key[0], sizeof(key)*8, &expandedKey) == 0
    );
    uint8_t in[16], out[16], ivec[16];
    memset(out, 0, sizeof(out));
    memset(ivec, 0, sizeof(ivec));
    for (size_t i = 0; i < len; i += 16) {
        // I actually don't know if OpenSSL auto pads the data or not, but I'd rather not
        // risk it.
        memset(&in, 0, sizeof(in));
        memcpy(&in, data+i, std::min<size_t>(sizeof(in), len-i));
        AES_cbc_encrypt(in, out, sizeof(in), &expandedKey, ivec, AES_ENCRYPT);
    }
    memcpy(&mac[0], out, sizeof(mac));
}