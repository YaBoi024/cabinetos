// Noise and the slow hash on OpenSSL (noise.h).

#include "noise.h"

#include <cstring>
#include <vector>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

namespace noise {

namespace {

// ---- X25519 ---------------------------------------------------------------

EVP_PKEY* privateKey(const Key& priv) {
    return EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv.data(), priv.size());
}

// False for a key that cannot be used, and for a low-order public key: OpenSSL refuses to
// derive when the answer would be all zeros, and that return is what stops it.
bool x25519(const Key& priv, const Key& pub, Key& out) {
    EVP_PKEY* mine = privateKey(priv);
    EVP_PKEY* theirs = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, pub.data(), pub.size());
    EVP_PKEY_CTX* ctx = mine ? EVP_PKEY_CTX_new(mine, nullptr) : nullptr;
    size_t len = out.size();
    const bool ok = ctx && theirs && EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_derive_set_peer(ctx, theirs) == 1 &&
                    EVP_PKEY_derive(ctx, out.data(), &len) == 1 && len == out.size();
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(theirs);
    EVP_PKEY_free(mine);
    return ok;
}

// ---- SHA-256 and HKDF, as Noise defines its own ---------------------------

Key hashOf(const uint8_t* a, size_t aLen, const uint8_t* b = nullptr, size_t bLen = 0) {
    Key out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    unsigned len = 0;
    if (ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 && EVP_DigestUpdate(ctx, a, aLen) == 1 &&
        (bLen == 0 || EVP_DigestUpdate(ctx, b, bLen) == 1)) {
        EVP_DigestFinal_ex(ctx, out.data(), &len);
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

Key hmac(const Key& key, const uint8_t* data, size_t len) {
    Key out{};
    unsigned outLen = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), data, len, out.data(), &outLen);
    return out;
}

// HKDF(chaining_key, input_key_material, num_outputs), section 4.3 of the spec.
void hkdf(const Key& ck, const uint8_t* ikm, size_t ikmLen, Key& one, Key& two, Key* three) {
    Key temp = hmac(ck, ikm, ikmLen);
    const uint8_t first = 0x01;
    one = hmac(temp, &first, 1);
    uint8_t in[kKeyLen + 1];
    std::memcpy(in, one.data(), kKeyLen);
    in[kKeyLen] = 0x02;
    two = hmac(temp, in, sizeof in);
    if (three) {
        std::memcpy(in, two.data(), kKeyLen);
        in[kKeyLen] = 0x03;
        *three = hmac(temp, in, sizeof in);
    }
    OPENSSL_cleanse(temp.data(), temp.size());
    OPENSSL_cleanse(in, sizeof in);
}

void nonceOf(uint64_t n, uint8_t nonce[12]) {
    std::memset(nonce, 0, 4);
    for (int i = 0; i < 8; ++i) nonce[4 + i] = static_cast<uint8_t>(n >> (8 * i));
}

// ---- Which messages hold which tokens --------------------------------------

enum Token { TokPsk, TokE, TokS, TokEE, TokES, TokSE, TokSS };

using Message = std::vector<Token>;

std::vector<Message> messagesOf(Pattern p) {
    switch (p) {
        case Pattern::XX: return {{TokE}, {TokE, TokEE, TokS, TokES}, {TokS, TokSE}};
        case Pattern::XXpsk0: return {{TokPsk, TokE}, {TokE, TokEE, TokS, TokES}, {TokS, TokSE}};
        case Pattern::IK: return {{TokE, TokES, TokS, TokSS}, {TokE, TokEE, TokSE}};
        case Pattern::K: return {{TokE, TokES, TokSS}};
    }
    return {};
}

const char* nameOf(Pattern p) {
    switch (p) {
        case Pattern::XX: return "Noise_XX_25519_ChaChaPoly_SHA256";
        case Pattern::XXpsk0: return "Noise_XXpsk0_25519_ChaChaPoly_SHA256";
        case Pattern::IK: return "Noise_IK_25519_ChaChaPoly_SHA256";
        case Pattern::K: return "Noise_K_25519_ChaChaPoly_SHA256";
    }
    return "";
}

}  // namespace

// ---- The plain functions ----------------------------------------------------

bool newKeyPair(KeyPair& out) {
    EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "X25519");
    if (!key) return false;
    size_t privLen = out.priv.size();
    size_t pubLen = out.pub.size();
    const bool ok = EVP_PKEY_get_raw_private_key(key, out.priv.data(), &privLen) == 1 &&
                    EVP_PKEY_get_raw_public_key(key, out.pub.data(), &pubLen) == 1 &&
                    privLen == kKeyLen && pubLen == kKeyLen;
    EVP_PKEY_free(key);
    return ok;
}

bool keyPairFrom(const Key& priv, KeyPair& out) {
    EVP_PKEY* key = privateKey(priv);
    if (!key) return false;
    size_t pubLen = out.pub.size();
    const bool ok = EVP_PKEY_get_raw_public_key(key, out.pub.data(), &pubLen) == 1 && pubLen == kKeyLen;
    EVP_PKEY_free(key);
    if (ok) out.priv = priv;
    return ok;
}

bool slowHash(const std::string& twelve, Key& out) {
    // PROTOCOL.md, "The code". Fixed by the protocol and never negotiated, so a fake server
    // cannot ask for a weaker one.
    static const char kSalt[] = "cabinet-linkup 1";
    uint32_t iterations = 3;
    uint32_t lanes = 1;
    uint32_t threads = 1;
    uint32_t memcost = 65536;
    uint32_t version = 0x13;
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    EVP_KDF_CTX* ctx = kdf ? EVP_KDF_CTX_new(kdf) : nullptr;
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, const_cast<char*>(twelve.data()), twelve.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char*>(kSalt), sizeof kSalt - 1),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iterations),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memcost),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_VERSION, &version),
        OSSL_PARAM_construct_end(),
    };
    const bool ok = ctx && EVP_KDF_derive(ctx, out.data(), out.size(), params) == 1;
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    return ok;
}

Key sha256(const std::string& text) {
    return hashOf(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

Bytes seal(const Key& k, uint64_t n, const uint8_t* ad, size_t adLen, const uint8_t* plain, size_t plainLen) {
    uint8_t nonce[12];
    nonceOf(n, nonce);
    Bytes out(plainLen + kTagLen);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    int total = 0;
    bool ok = ctx && EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, k.data(), nonce) == 1;
    if (ok && adLen > 0) ok = EVP_EncryptUpdate(ctx, nullptr, &len, ad, static_cast<int>(adLen)) == 1;
    if (ok && plainLen > 0) {
        ok = EVP_EncryptUpdate(ctx, out.data(), &len, plain, static_cast<int>(plainLen)) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_EncryptFinal_ex(ctx, out.data() + total, &len) == 1;
        total += len;
    }
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, kTagLen, out.data() + total) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok || static_cast<size_t>(total) != plainLen) out.clear();
    return out;
}

bool open(const Key& k, uint64_t n, const uint8_t* ad, size_t adLen, const uint8_t* cipher, size_t cipherLen,
          Bytes& plain) {
    if (cipherLen < kTagLen) return false;
    uint8_t nonce[12];
    nonceOf(n, nonce);
    const size_t bodyLen = cipherLen - kTagLen;
    Bytes out(bodyLen);
    uint8_t tag[kTagLen];
    std::memcpy(tag, cipher + bodyLen, kTagLen);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    int total = 0;
    bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, k.data(), nonce) == 1;
    if (ok && adLen > 0) ok = EVP_DecryptUpdate(ctx, nullptr, &len, ad, static_cast<int>(adLen)) == 1;
    if (ok && bodyLen > 0) {
        ok = EVP_DecryptUpdate(ctx, out.data(), &len, cipher, static_cast<int>(bodyLen)) == 1;
        total = len;
    }
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, kTagLen, tag) == 1;
    if (ok) ok = EVP_DecryptFinal_ex(ctx, out.data() + total, &len) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        OPENSSL_cleanse(out.data(), out.size());
        return false;
    }
    plain = std::move(out);
    return true;
}

// A request is in domain 4 and its answer in domain 5, so an answer cannot pass for a request.
static Bytes sealedHeader(uint8_t domain, const SessionId& id, uint64_t counter) {
    Bytes ad(1 + id.size() + 8);
    ad[0] = domain;
    std::memcpy(ad.data() + 1, id.data(), id.size());
    for (int i = 0; i < 8; ++i) ad[1 + id.size() + static_cast<size_t>(i)] = static_cast<uint8_t>(counter >> (56 - 8 * i));
    return ad;
}

Bytes sealRequest(const Key& send, const SessionId& id, uint64_t counter, const Bytes& plain) {
    const Bytes ad = sealedHeader(4, id, counter);
    const Bytes c = seal(send, counter, ad.data(), ad.size(), plain.data(), plain.size());
    if (c.empty()) return {};
    Bytes body(ad.begin() + 1, ad.end());
    body.insert(body.end(), c.begin(), c.end());
    return body;
}

bool openAnswer(const Key& recv, const SessionId& id, uint64_t counter, const Bytes& cipher, Bytes& plain) {
    const Bytes ad = sealedHeader(5, id, counter);
    return open(recv, counter, ad.data(), ad.size(), cipher.data(), cipher.size(), plain);
}

// ---- The handshake ----------------------------------------------------------

Handshake::Handshake(const Config& config)
    : pattern_(config.pattern),
      initiator_(config.initiator),
      s_(config.s),
      rs_(config.rs),
      psk_(config.psk),
      fixedE_(config.e),
      usesPsk_(config.pattern == Pattern::XXpsk0) {
    // InitializeSymmetric. A name of a hash's length or less is the hash, with zeros after it,
    // and only a longer one is hashed: XX, IK and K are 32 or 31 bytes and XXpsk0 is 36.
    const char* name = nameOf(pattern_);
    const size_t nameLen = std::strlen(name);
    if (nameLen <= h_.size()) std::memcpy(h_.data(), name, nameLen);
    else h_ = hashOf(reinterpret_cast<const uint8_t*>(name), nameLen);
    ck_ = h_;
    mixHash(reinterpret_cast<const uint8_t*>(config.prologue.data()), config.prologue.size());

    // The pre-messages: what each side already holds of the other, in the spec's order.
    if (pattern_ == Pattern::IK || pattern_ == Pattern::K) {
        const Key* initiatorKey = nullptr;
        const Key* responderKey = nullptr;
        if (initiator_) {
            initiatorKey = &s_.pub;
            responderKey = rs_ ? &*rs_ : nullptr;
        } else {
            initiatorKey = rs_ ? &*rs_ : nullptr;
            responderKey = &s_.pub;
        }
        if (pattern_ == Pattern::K) {
            if (!initiatorKey) broken_ = true;
            else mixHash(initiatorKey->data(), initiatorKey->size());
        }
        if (!responderKey) broken_ = true;
        else mixHash(responderKey->data(), responderKey->size());
    }
    if (usesPsk_ && !psk_) broken_ = true;
}

Handshake::~Handshake() {
    OPENSSL_cleanse(s_.priv.data(), s_.priv.size());
    OPENSSL_cleanse(e_.priv.data(), e_.priv.size());
    OPENSSL_cleanse(ck_.data(), ck_.size());
    OPENSSL_cleanse(k_.data(), k_.size());
    OPENSSL_cleanse(send1_.data(), send1_.size());
    OPENSSL_cleanse(send2_.data(), send2_.size());
    if (psk_) OPENSSL_cleanse(psk_->data(), psk_->size());
    if (fixedE_) OPENSSL_cleanse(fixedE_->priv.data(), fixedE_->priv.size());
}

void Handshake::mixHash(const uint8_t* data, size_t len) {
    h_ = hashOf(h_.data(), h_.size(), data, len);
}

void Handshake::mixKey(const uint8_t* ikm, size_t len) {
    Key ck;
    Key k;
    hkdf(ck_, ikm, len, ck, k, nullptr);
    ck_ = ck;
    k_ = k;
    hasK_ = true;
    n_ = 0;
    OPENSSL_cleanse(ck.data(), ck.size());
    OPENSSL_cleanse(k.data(), k.size());
}

void Handshake::mixKeyAndHash(const uint8_t* ikm, size_t len) {
    Key ck;
    Key th;
    Key k;
    hkdf(ck_, ikm, len, ck, th, &k);
    ck_ = ck;
    mixHash(th.data(), th.size());
    k_ = k;
    hasK_ = true;
    n_ = 0;
    OPENSSL_cleanse(ck.data(), ck.size());
    OPENSSL_cleanse(k.data(), k.size());
}

bool Handshake::encryptAndHash(const uint8_t* plain, size_t len, Bytes& out) {
    if (!hasK_) {
        mixHash(plain, len);
        out.insert(out.end(), plain, plain + len);
        return true;
    }
    Bytes c = seal(k_, n_, h_.data(), h_.size(), plain, len);
    if (c.empty()) return false;
    ++n_;
    mixHash(c.data(), c.size());
    out.insert(out.end(), c.begin(), c.end());
    return true;
}

bool Handshake::decryptAndHash(const uint8_t* cipher, size_t len, Bytes& out) {
    if (!hasK_) {
        out.assign(cipher, cipher + len);
        mixHash(cipher, len);
        return true;
    }
    if (!open(k_, n_, h_.data(), h_.size(), cipher, len, out)) return false;
    ++n_;
    mixHash(cipher, len);
    return true;
}

bool Handshake::dh(const Key& priv, const Key& pub) {
    Key shared;
    if (!x25519(priv, pub, shared)) return false;
    mixKey(shared.data(), shared.size());
    OPENSSL_cleanse(shared.data(), shared.size());
    return true;
}

void Handshake::split() {
    static const uint8_t kNothing = 0;
    hkdf(ck_, &kNothing, 0, send1_, send2_, nullptr);
    finished_ = true;
}

bool Handshake::write(const Bytes& payload, Bytes& message) {
    message.clear();
    const std::vector<Message> messages = messagesOf(pattern_);
    const bool mine = (message_ % 2 == 0) == initiator_;
    if (broken_ || finished_ || message_ >= messages.size() || !mine) {
        broken_ = true;
        return false;
    }
    for (Token t : messages[message_]) {
        bool ok = true;
        switch (t) {
            case TokPsk: mixKeyAndHash(psk_->data(), psk_->size()); break;
            case TokE:
                if (fixedE_) e_ = *fixedE_;
                else ok = newKeyPair(e_);
                if (ok) {
                    message.insert(message.end(), e_.pub.begin(), e_.pub.end());
                    mixHash(e_.pub.data(), e_.pub.size());
                    if (usesPsk_) mixKey(e_.pub.data(), e_.pub.size());
                }
                break;
            case TokS: ok = encryptAndHash(s_.pub.data(), s_.pub.size(), message); break;
            case TokEE: ok = re_ && dh(e_.priv, *re_); break;
            case TokES:
                if (initiator_) ok = rs_ && dh(e_.priv, *rs_);
                else ok = re_ && dh(s_.priv, *re_);
                break;
            case TokSE:
                if (initiator_) ok = re_ && dh(s_.priv, *re_);
                else ok = rs_ && dh(e_.priv, *rs_);
                break;
            case TokSS: ok = rs_ && dh(s_.priv, *rs_); break;
        }
        if (!ok) {
            broken_ = true;
            return false;
        }
    }
    if (!encryptAndHash(payload.data(), payload.size(), message)) {
        broken_ = true;
        return false;
    }
    ++message_;
    if (message_ == messages.size()) split();
    return true;
}

bool Handshake::read(const Bytes& message, Bytes& payload) {
    payload.clear();
    const std::vector<Message> messages = messagesOf(pattern_);
    const bool theirs = (message_ % 2 == 0) != initiator_;
    if (broken_ || finished_ || message_ >= messages.size() || !theirs) {
        broken_ = true;
        return false;
    }
    size_t at = 0;
    for (Token t : messages[message_]) {
        bool ok = true;
        switch (t) {
            case TokPsk: mixKeyAndHash(psk_->data(), psk_->size()); break;
            case TokE: {
                if (message.size() - at < kKeyLen) {
                    ok = false;
                    break;
                }
                Key re;
                std::memcpy(re.data(), message.data() + at, kKeyLen);
                at += kKeyLen;
                re_ = re;
                mixHash(re.data(), re.size());
                if (usesPsk_) mixKey(re.data(), re.size());
                break;
            }
            case TokS: {
                const size_t need = hasK_ ? kKeyLen + kTagLen : kKeyLen;
                if (message.size() - at < need) {
                    ok = false;
                    break;
                }
                Bytes key;
                ok = decryptAndHash(message.data() + at, need, key);
                at += need;
                if (ok) {
                    Key rs;
                    std::memcpy(rs.data(), key.data(), kKeyLen);
                    rs_ = rs;
                }
                break;
            }
            case TokEE: ok = re_ && dh(e_.priv, *re_); break;
            case TokES:
                if (initiator_) ok = rs_ && dh(e_.priv, *rs_);
                else ok = re_ && dh(s_.priv, *re_);
                break;
            case TokSE:
                if (initiator_) ok = re_ && dh(s_.priv, *re_);
                else ok = rs_ && dh(e_.priv, *rs_);
                break;
            case TokSS: ok = rs_ && dh(s_.priv, *rs_); break;
        }
        if (!ok) {
            broken_ = true;
            return false;
        }
    }
    if (!decryptAndHash(message.data() + at, message.size() - at, payload)) {
        broken_ = true;
        return false;
    }
    ++message_;
    if (message_ == messages.size()) split();
    return true;
}

}  // namespace noise
