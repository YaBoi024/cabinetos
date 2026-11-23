// Noise, and the slow hash, on OpenSSL: every key this program touches goes through here.
// PROTOCOL.md in MMagTech/cabinet-linkup says which pattern is used for what. Noise's own
// rules (https://noiseprotocol.org/noise.html) are followed to the letter, and noise-test.cpp
// holds this file to the published vectors and to answers from the server's own code.
//
// Only what LinkUp uses: X25519, ChaCha20-Poly1305 and SHA-256, in the patterns XXpsk0
// (pairing), IK (sessions) and K (sealed boxes). XX is here only because the published vectors
// test it.
//
// ONE MISTAKE ENDS A HANDSHAKE. A failed read leaves the state half moved on, so nothing more
// can be done with it, and the caller starts again from nothing.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace noise {

using Bytes = std::vector<uint8_t>;

constexpr size_t kKeyLen = 32;
constexpr size_t kTagLen = 16;
using Key = std::array<uint8_t, kKeyLen>;

struct KeyPair {
    Key priv{};
    Key pub{};
};

// A new long-term or short-lived key.
bool newKeyPair(KeyPair& out);
// The public half of a private key, as a key file or a test vector holds only that.
bool keyPairFrom(const Key& priv, KeyPair& out);

// K in PROTOCOL.md: Argon2id over the twelve characters of a code, tidied already.
bool slowHash(const std::string& twelve, Key& out);

// SHA-256 of anything: the account's stable id.
Key sha256(const std::string& text);

// ChaCha20-Poly1305 with Noise's nonce: four zero bytes, then `n` little endian.
Bytes seal(const Key& k, uint64_t n, const uint8_t* ad, size_t adLen, const uint8_t* plain, size_t plainLen);
bool open(const Key& k, uint64_t n, const uint8_t* ad, size_t adLen, const uint8_t* cipher, size_t cipherLen,
          Bytes& plain);

// A Sealed request and its answer, PROTOCOL.md's "Sessions": `session | counter (big endian) |
// ciphertext`. The counter is used up by whoever calls this: a retry is these same bytes or a
// new counter, never new bytes under this one.
constexpr size_t kSessionLen = 8;
using SessionId = std::array<uint8_t, kSessionLen>;
Bytes sealRequest(const Key& send, const SessionId& id, uint64_t counter, const Bytes& plain);
bool openAnswer(const Key& recv, const SessionId& id, uint64_t counter, const Bytes& cipher, Bytes& plain);

enum class Pattern { XX, XXpsk0, IK, K };

class Handshake {
public:
    struct Config {
        Pattern pattern = Pattern::XX;
        bool initiator = true;
        std::string prologue;
        KeyPair s;                    // this side's long-term key
        std::optional<Key> rs;        // the other side's, where the pattern needs it known
        std::optional<Key> psk;       // XXpsk0's
        std::optional<KeyPair> e;     // only the tests fix this; otherwise it is made here
    };

    explicit Handshake(const Config& config);
    ~Handshake();
    Handshake(const Handshake&) = delete;
    Handshake& operator=(const Handshake&) = delete;

    // The next message, carrying `payload`. False is the end of this handshake.
    bool write(const Bytes& payload, Bytes& message);
    // The next message from the other side. False is the end of this handshake.
    bool read(const Bytes& message, Bytes& payload);

    bool finished() const { return finished_; }
    // After the last message. send1 is the initiator's key for sending, send2 the responder's.
    const Key& send1() const { return send1_; }
    const Key& send2() const { return send2_; }
    // The other side's long-term key, once it has been sent.
    const std::optional<Key>& remoteStatic() const { return rs_; }
    const Key& hash() const { return h_; }

private:
    void mixHash(const uint8_t* data, size_t len);
    void mixKey(const uint8_t* ikm, size_t len);
    void mixKeyAndHash(const uint8_t* ikm, size_t len);
    bool encryptAndHash(const uint8_t* plain, size_t len, Bytes& out);
    bool decryptAndHash(const uint8_t* cipher, size_t len, Bytes& out);
    bool dh(const Key& priv, const Key& pub);
    bool step(bool writing);
    void split();

    Pattern pattern_;
    bool initiator_;
    KeyPair s_;
    KeyPair e_;
    std::optional<Key> rs_;
    std::optional<Key> re_;
    std::optional<Key> psk_;
    std::optional<KeyPair> fixedE_;
    bool usesPsk_ = false;

    Key ck_{};
    Key h_{};
    Key k_{};
    bool hasK_ = false;
    uint64_t n_ = 0;

    size_t message_ = 0;     // which message of the pattern is next
    bool broken_ = false;
    bool finished_ = false;
    Key send1_{};
    Key send2_{};
};

}  // namespace noise
