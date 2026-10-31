// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "aa_cryptor.hpp"
#include <stdio.h>
#include <string.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

extern "C" {
    const SSL_METHOD* TLSv1_2_client_method(void);
}

// Canonical Android Auto Head Unit Certificate signed by Google Automotive Link CA (valid through 2048)
static const char* kHeadUnitCertPem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDPzCCAicCAgERMA0GCSqGSIb3DQEBCwUAMFsxCzAJBgNVBAYTAlVTMRMwEQYD\n"
    "VQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1Nb3VudGFpbiBWaWV3MR8wHQYDVQQK\n"
    "DBZHb29nbGUgQXV0b21vdGl2ZSBMaW5rMCYXETE0MDcwNDAwMDAwMC0wNzAwFxE0\n"
    "ODA4MDExMDIxMjMtMDcwMDBnMQswCQYDVQQGEwJVUzETMBEGA1UECAwKQ2FsaWZv\n"
    "cm5pYTEWMBQGA1UEBwwNTW91bnRhaW4gVmlldzEeMBwGA1UECgwVQW5kcm9pZC1B\n"
    "dXRvLUludGVybmFsMQswCQYDVQQLDAIwMTCCASIwDQYJKoZIhvcNAQEBBQADggEP\n"
    "ADCCAQoCggEBAOWghAac2eJLbi/ijgZGRB6/MuaBVfOImkQddBJUhXbnskTJB/JI\n"
    "12Ea22E5GeVN8CkWULAZT28yDWqsKMyq9BzpjpsHc9TKxMYqrIn0HP7mIJcBu5z7\n"
    "K8DoXqc86encncJlkGeuQkUA68yyp7RG7eQ6XoBHEjNmyvX13Y8NY5sPUHfLfmp6\n"
    "A2n+Jdmecq3L0GS84ctdNtnp2zSopTy0L1Gp6+lrnuOPAYZeV+Ei2jAvhycvuSoB\n"
    "yV6rT9wvREvC2TDncurMwR6ws44+ZStqkhnvDLhV04ray5aPplQwwB9GELFCYSRk\n"
    "56sm57uYSJj/LlmOMcvyBmUHVJ7MLxgtlykCAwEAATANBgkqhkiG9w0BAQsFAAOC\n"
    "AQEA1Bs8v6HuAIiBdhGDGHzZJDwO6lW0LheBqsGLG9KsVvIVrTMPP9lpdTPjStGn\n"
    "en1RIce4R4l3YTBwxOadLMkf8rymAE5JNjPsWlBue7eI4TFFw/cvnKxcTQ61bC4i\n"
    "2uosyDI5VfrXm38zYcZoK4TFtMhNyx6aYSEClWB9MjHa+n6eR3dLBCg1kMGqGdZ/\n"
    "AoK0UEkyI3UFU8sW86iaS4dvPSaQ+z0tmfUzbrc5ZSk4hYCeUYvuyd2ShxjKmxvD\n"
    "0K8A7gKLY0jP8Zp+6rYBcpxc7cylWMbdlhFTHAGiKI+XeQ/9u+RPeocZsn5jGlDt\n"
    "K3ftMoWFce+baNq/WcMzRj04AA==\n"
    "-----END CERTIFICATE-----\n";

static const char* kHeadUnitKeyPem =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDloIQGnNniS24v\n"
    "4o4GRkQevzLmgVXziJpEHXQSVIV257JEyQfySNdhGtthORnlTfApFlCwGU9vMg1q\n"
    "rCjMqvQc6Y6bB3PUysTGKqyJ9Bz+5iCXAbuc+yvA6F6nPOnp3J3CZZBnrkJFAOvM\n"
    "sqe0Ru3kOl6ARxIzZsr19d2PDWObD1B3y35qegNp/iXZnnKty9BkvOHLXTbZ6ds0\n"
    "qKU8tC9Rqevpa57jjwGGXlfhItowL4cnL7kqAcleq0/cL0RLwtkw53LqzMEesLOO\n"
    "PmUrapIZ7wy4VdOK2suWj6ZUMMAfRhCxQmEkZOerJue7mEiY/y5ZjjHL8gZlB1Se\n"
    "zC8YLZcpAgMBAAECggEANfN+0abssnSSkQGj6Ol9e9Pke7ALp8qCpFpfOSdOrF9H\n"
    "zU2GZnCi2hfbT36BDRVAxgaOMrHfrfqOmG+Wrm/EJz7g2J/poSd7ECxamZcXcPRU\n"
    "1+X6VKnW3944zwZK1z2Ue2YthY6110yeGlwndv3rSuJD7dPE/Pq72QpJYiD9NCL1\n"
    "DoX98hDpl570C8bpD/8I4lgD2YqGcLmR6cHaKx0gQjxfr2gPY22uu0rDsriTPUQX\n"
    "og01iG7iwbFZkGD0/N0LdBHQtfXclDWZvi3sm/1Td2d1GmKw5QGl69BAxNlyrpo9\n"
    "TCYIXcop5/ZJTvoJ06wxKBmimYtWJZMuKlC6eDg9AQKBgQD1RIj4fkpuL1D5Cj1b\n"
    "ceNUNAcsRNuDNZmo3WvDVBNVGQaJKQcm0EzUwc94UKuWZ/lTBOJyBYUbdsiQTHsd\nuHk0bUmP1II7/k7cieP4sGGZEV9ouB6PC8N3wPryncyBQdRAUsoaOsWu/bzAP08w\nq08LxwtweUiTzubPjnwKdBUV0QKBgQDvrMZsHmn6PLIaC1kpvWLeIAC4ZHOJffD8\n++ap5djTDLP6tmve7eZVL1XuVhsPag5JlBs1bPenvqcTMTqP4my33W6GSXz8dqHq\nX271tY7Zw+pf1rjTYZ8khufrdMia5zSAKTZ27hofVobDJWDJwo+ijQLcIwwkVN3K\nCOqdU53J2QKBgGP8jY4oBfXHSk5MU1kdI5ynHSl55Lcnub2yjoTxmHP/oL93v1ee\nYUP2wrp8wIavWl/yxNHQ2EOq9/aDh0lFc/vgdbWx32nPsZyMhihS12bMQ0XwkVyl\nYglG3cbcd4tWJ84VMhjhTseY30LMTF895xqZCVvPxbukBEom/wBP9GyRAoGAXSR6\n9DeZnesKl/6DByqXipVLUl2PXQZ5qgdHLA2sBtDv/OX54+f2VHKR5Y7G82WW6ddJ\n/6g6UpFQIrBF/vbSo13FfyB+FysPX6L43zpQqqZtphMvPs64ZxyacrGu6CwkBg5+\nIRgFRNp+gsY66A3ZfiVo0B46sL/edY6jhsKhFnkCgYEAqHKJM0GrxITZYLMjCXZC\nudZvONe14MnYUJfxpQ3CLgpBq5QbTQq8ITtVAvd7PHrdHzW4B4+yKroTQ2gn6ev0\nnTFBkYuHpa2CvVSumc30qfZpeKMatdmbQC2FVEPSOQS5cx6B7YYS0nW9nRon+Sbt\nGQ5XKUTGTaUvJR1gYDDMrlM=\n"
    "-----END PRIVATE KEY-----\n";

namespace nbt {

AACryptor::AACryptor()
    : ssl_ctx_(NULL),
      ssl_(NULL),
      rbio_(NULL),
      wbio_(NULL),
      is_handshake_complete_(false) {
    last_error_[0] = 0;
}

AACryptor::~AACryptor() {
    cleanup();
}

bool AACryptor::init() {
    cleanup();

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // Use OpenSSL 1.0.1l TLS 1.2 Client Method (car's native libssl.so.2)
    SSL_METHOD* method = const_cast<SSL_METHOD*>(TLSv1_2_client_method());
    if (!method) {
        printf("[AACryptor] Failed to get TLS 1.2 client method.\n");
        return false;
    }

    ssl_ctx_ = SSL_CTX_new(method);
    if (!ssl_ctx_) {
        printf("[AACryptor] Failed to create SSL context.\n");
        return false;
    }

    // Force modern TLS options
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_cipher_list(ssl_ctx_, "ALL:!aNULL:!eNULL:!LOW:!EXP:!RC4:!MD5");

    // Load authentic Google Automotive Link Head Unit Certificate
    BIO* bio_cert = BIO_new_mem_buf(const_cast<char*>(kHeadUnitCertPem), -1);
    X509* cert = PEM_read_bio_X509(bio_cert, NULL, NULL, NULL);
    BIO_free(bio_cert);
    if (!cert || SSL_CTX_use_certificate(ssl_ctx_, cert) != 1) {
        printf("[AACryptor] Failed to load Google headunit certificate.\n");
        return false;
    }
    X509_free(cert);

    // Load matching RSA Private Key
    BIO* bio_key = BIO_new_mem_buf(const_cast<char*>(kHeadUnitKeyPem), -1);
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio_key, NULL, NULL, NULL);
    BIO_free(bio_key);
    if (!pkey || SSL_CTX_use_PrivateKey(ssl_ctx_, pkey) != 1) {
        printf("[AACryptor] Failed to load Google headunit private key.\n");
        return false;
    }
    EVP_PKEY_free(pkey);

    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) {
        printf("[AACryptor] Failed to create SSL object.\n");
        return false;
    }

    rbio_ = BIO_new(BIO_s_mem());
    wbio_ = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl_, rbio_, wbio_);
    SSL_set_connect_state(ssl_); // Set to CLIENT mode!

    is_handshake_complete_ = false;
    printf("[AACryptor] OpenSSL engine initialized (TLS 1.2 Client Mode with mTLS X.509 cert).\n");
    return true;
}

void AACryptor::cleanup() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = NULL;
        rbio_ = NULL; // BIOs freed by SSL_free
        wbio_ = NULL;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = NULL;
    }
    is_handshake_complete_ = false;
}

bool AACryptor::readPendingBio(std::vector<uint8_t>& out_data) {
    out_data.clear();
    char buf[4096];
    int n;
    while ((n = BIO_read(wbio_, buf, sizeof(buf))) > 0) {
        out_data.insert(out_data.end(), buf, buf + n);
    }
    return !out_data.empty();
}

bool AACryptor::startHandshake(std::vector<uint8_t>& out_handshake_bytes) {
    if (!ssl_) return false;

    SSL_do_handshake(ssl_);
    readPendingBio(out_handshake_bytes);

    printf("[AACryptor] Generated TLS ClientHello with mTLS certificate (%u bytes):\n",
           static_cast<unsigned int>(out_handshake_bytes.size()));
    printf("            Data: ");
    for (size_t i = 0; i < out_handshake_bytes.size() && i < 32; ++i) {
        printf("%02X ", out_handshake_bytes[i]);
    }
    if (out_handshake_bytes.size() > 32) printf("...");
    printf("\n");
    return !out_handshake_bytes.empty();
}

bool AACryptor::processHandshakeData(const uint8_t* in_data, size_t in_len, std::vector<uint8_t>& out_handshake_bytes) {
    if (!ssl_ || in_len == 0) return false;

    BIO_write(rbio_, in_data, in_len);
    SSL_do_handshake(ssl_);

    readPendingBio(out_handshake_bytes);

    if (SSL_is_init_finished(ssl_)) {
        is_handshake_complete_ = true;
        printf("[AACryptor] >>> TLS HANDSHAKE FINISHED! Secure tunnel active. Cipher: %s\n",
               SSL_get_cipher(ssl_));
    }

    return true;
}

bool AACryptor::encrypt(const uint8_t* plain_data, size_t plain_len, std::vector<uint8_t>& out_encrypted) {
    if (!ssl_ || !is_handshake_complete_) return false;

    SSL_write(ssl_, plain_data, plain_len);
    return readPendingBio(out_encrypted);
}

bool AACryptor::decrypt(const uint8_t* encrypted_data, size_t encrypted_len, std::vector<uint8_t>& out_decrypted) {
    out_decrypted.clear();
    if (!ssl_ || !is_handshake_complete_ || encrypted_len == 0) return false;

    BIO_write(rbio_, encrypted_data, encrypted_len);

    char buf[4096];
    int n;
    while ((n = SSL_read(ssl_, buf, sizeof(buf))) > 0) {
        out_decrypted.insert(out_decrypted.end(), buf, buf + n);
    }
    if (out_decrypted.empty()) {
        int e = SSL_get_error(ssl_, n);
        unsigned long q = ERR_get_error();
        char qs[120] = "none";
        if (q) ERR_error_string_n(q, qs, sizeof(qs));
        snprintf(last_error_, sizeof(last_error_), "SSL_read %d, SSL_get_error %d, %s", n, e, qs);
        while (ERR_get_error()) { }
    }
    return !out_decrypted.empty();
}

size_t AACryptor::pendingOut() const {
    return wbio_ ? (size_t)BIO_ctrl_pending(wbio_) : 0;
}

} // namespace nbt
