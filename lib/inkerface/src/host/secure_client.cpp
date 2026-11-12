//
// Implements FreeInk's SecureClient (declared in its SecureClient.h) for
// the host. With INKERFACE_HAS_TLS the TLS session is OpenSSL over the
// shim WiFiClient's non-blocking socket; otherwise TLS is reported as
// unavailable, like FreeInk without wolfSSL. Also defines FreeInk's
// hostResolves() with getaddrinfo().
//
// Verification: with a CA certificate, the server chain must lead to it;
// without one, the system certificate store is used; host names are
// checked either way. setInsecure() turns verification off (testing).
//
// MIT License (see: LICENSE)
// Copyright (C) 2026 Tomaz Stih
//

#include <SecureClient.h>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>

#include <algorithm>
#include <climits>

#ifdef INKERFACE_HAS_TLS
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#endif

namespace freeink {

bool hostResolves(const char *host) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    if (::getaddrinfo(host, nullptr, &hints, &found) != 0) {
        return false;
    }
    ::freeaddrinfo(found);
    return true;
}

SecureClient::~SecureClient() {
    stop();
}

void SecureClient::setCACert(const char *rootCA) {
    _rootCA = rootCA;
}

void SecureClient::setInsecure() {
    _insecure = true;
}

bool SecureClient::abortRequested() {
    if (!_shouldAbort || !_shouldAbort()) {
        return false;
    }
    _aborted = true;
    return true;
}

int SecureClient::connect(IPAddress /*ip*/, uint16_t /*port*/) {
    return 0; // the host connects by name only
}

size_t SecureClient::write(uint8_t byte) {
    return write(&byte, 1);
}

int SecureClient::read() {
    uint8_t byte = 0;
    return read(&byte, 1) == 1 ? byte : -1;
}

int SecureClient::peek() {
    return -1; // not used by SecureHttpClient
}

void SecureClient::flush() {}

#ifdef INKERFACE_HAS_TLS

namespace {

SSL *session(void *handle) {
    return static_cast<SSL *>(handle);
}

// Wait for the socket while OpenSSL wants I/O, up to the deadline and
// polling the abort callback every 50 ms.
bool wait_for_tls(int descriptor,
                  int failure,
                  unsigned long start,
                  unsigned long limit) {
    const short events = failure == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN;
    const unsigned long elapsed = millis() - start;
    if (elapsed >= limit) {
        return false;
    }
    const unsigned long slice = std::min(limit - elapsed, 50UL);
    pollfd watched{descriptor, events, 0};
    return ::poll(&watched, 1, static_cast<int>(slice)) >= 0;
}

// Add one PEM certificate to the context's trust store.
bool trust(SSL_CTX *context, const char *pem) {
    BIO *source = BIO_new_mem_buf(pem, -1);
    if (source == nullptr) {
        return false;
    }
    X509 *certificate = PEM_read_bio_X509(source, nullptr, nullptr, nullptr);
    BIO_free(source);
    if (certificate == nullptr) {
        return false;
    }
    const int added =
        X509_STORE_add_cert(SSL_CTX_get_cert_store(context), certificate);
    X509_free(certificate);
    return added == 1;
}

} // namespace

bool SecureClient::tls13Available() {
    return true;
}

int SecureClient::connect(const char *host, uint16_t port) {
    _aborted = false;
    stop();
    if (abortRequested() || !hostResolves(host)) {
        return 0;
    }
    const unsigned long limit = getTimeout();
    _transport.setConnectionTimeout(static_cast<uint32_t>(limit));
    if (_transport.connect(host, port) == 0) {
        return 0;
    }
    _tcpConnected = true;

    SSL_CTX *context = SSL_CTX_new(TLS_client_method());
    if (context == nullptr) {
        stop();
        return 0;
    }
    _ctx = context;
    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    if (_insecure) {
        SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(context, SSL_VERIFY_PEER, nullptr);
        const bool trusted =
            _rootCA != nullptr
                ? trust(context, _rootCA)
                : SSL_CTX_set_default_verify_paths(context) == 1;
        if (!trusted) {
            stop();
            return 0;
        }
    }
    SSL *ssl = SSL_new(context);
    if (ssl == nullptr) {
        stop();
        return 0;
    }
    _ssl = ssl;
    SSL_set_fd(ssl, _transport.fd());
    SSL_set_tlsext_host_name(ssl, host);
    if (!_insecure && SSL_set1_host(ssl, host) != 1) {
        stop();
        return 0;
    }
    const unsigned long start = millis();
    while (true) {
        const int result = SSL_connect(ssl);
        if (result == 1) {
            break;
        }
        const int failure = SSL_get_error(ssl, result);
        const bool want_io =
            failure == SSL_ERROR_WANT_READ || failure == SSL_ERROR_WANT_WRITE;
        if (!want_io || abortRequested() ||
            !wait_for_tls(_transport.fd(), failure, start, limit)) {
            stop();
            return 0;
        }
    }
    _connected = true;
    return 1;
}

size_t SecureClient::write(const uint8_t *buf, size_t size) {
    size_t sent = 0;
    const unsigned long start = millis();
    while (_connected && sent < size) {
        size_t put = 0;
        if (SSL_write_ex(session(_ssl), buf + sent, size - sent, &put) == 1) {
            sent += put;
            continue;
        }
        const int failure = SSL_get_error(session(_ssl), 0);
        if ((failure != SSL_ERROR_WANT_READ &&
             failure != SSL_ERROR_WANT_WRITE) ||
            !wait_for_tls(_transport.fd(), failure, start, getTimeout())) {
            _connected = false;
            break;
        }
    }
    return sent;
}

int SecureClient::read(uint8_t *buf, size_t size) {
    if (!_connected) {
        return -1;
    }
    size_t got = 0;
    if (SSL_read_ex(session(_ssl), buf, size, &got) == 1) {
        return static_cast<int>(std::min(got, static_cast<size_t>(INT_MAX)));
    }
    const int failure = SSL_get_error(session(_ssl), 0);
    if (failure == SSL_ERROR_WANT_READ || failure == SSL_ERROR_WANT_WRITE) {
        return 0; // no complete record yet
    }
    _connected = false; // closed by the peer, or failed
    return failure == SSL_ERROR_ZERO_RETURN ? 0 : -1;
}

int SecureClient::available() {
    if (!_connected) {
        return 0;
    }
    return SSL_pending(session(_ssl)) + _transport.available();
}

void SecureClient::stop() {
    if (_ssl != nullptr) {
        SSL_free(session(_ssl));
        _ssl = nullptr;
    }
    if (_ctx != nullptr) {
        SSL_CTX_free(static_cast<SSL_CTX *>(_ctx));
        _ctx = nullptr;
    }
    _transport.stop();
    _connected = false;
    _tcpConnected = false;
}

uint8_t SecureClient::connected() {
    return _connected && _transport.connected() ? 1 : 0;
}

#else // !INKERFACE_HAS_TLS

bool SecureClient::tls13Available() {
    return false;
}

int SecureClient::connect(const char * /*host*/, uint16_t /*port*/) {
    return 0;
}

size_t SecureClient::write(const uint8_t * /*buf*/, size_t /*size*/) {
    return 0;
}

int SecureClient::read(uint8_t * /*buf*/, size_t /*size*/) {
    return -1;
}

int SecureClient::available() {
    return 0;
}

void SecureClient::stop() {
    _transport.stop();
    _connected = false;
}

uint8_t SecureClient::connected() {
    return 0;
}

#endif

} // namespace freeink
