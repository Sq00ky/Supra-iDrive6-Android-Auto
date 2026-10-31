// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_CRYPTOR_HPP
#define AA_CRYPTOR_HPP

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

namespace nbt {

class AACryptor {
public:
    AACryptor();
    ~AACryptor();

    bool init();
    void cleanup();

    bool startHandshake(std::vector<uint8_t>& out_handshake_bytes);
    bool processHandshakeData(const uint8_t* in_data, size_t in_len, std::vector<uint8_t>& out_handshake_bytes);

    bool isHandshakeComplete() const { return is_handshake_complete_; }

    bool encrypt(const uint8_t* plain_data, size_t plain_len, std::vector<uint8_t>& out_encrypted);
    bool decrypt(const uint8_t* encrypted_data, size_t encrypted_len, std::vector<uint8_t>& out_decrypted);
    // Why the last decrypt() returned nothing (SSL_get_error code + OpenSSL error queue), and how many bytes OpenSSL
    // queued for the phone meanwhile (an alert or a renegotiation ClientHello would go out with the next encrypt()).
    const char* lastError() const { return last_error_; }
    size_t pendingOut() const;

private:
    SSL_CTX* ssl_ctx_;
    SSL*     ssl_;
    BIO*     rbio_;
    BIO*     wbio_;
    bool     is_handshake_complete_;
    char     last_error_[192];

    bool readPendingBio(std::vector<uint8_t>& out_data);
};

} // namespace nbt

#endif // AA_CRYPTOR_HPP
