// `make test`: noise.cpp held to the published Noise vectors (noise-vectors.txt) and to the
// answers the server's own code makes (proof-vectors.txt, written by internal/proof in
// MMagTech/cabinet-linkup). No network. A hand-written Noise is the largest risk the security
// review left, so this runs in CI and every message has to match to the byte. Also what sits
// next to it: names cleaned, boxes dropped when they do not fit, what an address means.
//
//   noise-test <folder holding the two files>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "box.h"
#include "noise.h"
#include "server_link.h"

using noise::Bytes;
using noise::Handshake;
using noise::Key;
using noise::KeyPair;
using noise::Pattern;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) return;
    ++failures;
    std::fprintf(stderr, "FAILED: %s\n", what.c_str());
}

int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Bytes unhex(const std::string& hex) {
    Bytes out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
    return out;
}

std::string hexOf(const Bytes& b) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint8_t v : b) {
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 15]);
    }
    return out;
}

Key keyOf(const std::string& hex) {
    const Bytes b = unhex(hex);
    Key k{};
    if (b.size() == k.size()) std::memcpy(k.data(), b.data(), k.size());
    return k;
}

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

using Block = std::map<std::string, std::string>;

std::vector<Block> readBlocks(const std::string& path) {
    std::vector<Block> blocks;
    std::ifstream in(path);
    if (!in) {
        check(false, "cannot read " + path);
        return blocks;
    }
    std::string line;
    Block current;
    auto flush = [&]() {
        if (!current.empty()) blocks.push_back(current);
        current.clear();
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            flush();
            continue;
        }
        current[line.substr(0, eq)] = line.substr(eq + 1);
    }
    flush();
    return blocks;
}

bool patternOf(const std::string& name, Pattern& p, size_t& handshakeMessages) {
    if (name.rfind("Noise_XXpsk0_", 0) == 0) { p = Pattern::XXpsk0; handshakeMessages = 3; return true; }
    if (name.rfind("Noise_XX_", 0) == 0) { p = Pattern::XX; handshakeMessages = 3; return true; }
    if (name.rfind("Noise_IK_", 0) == 0) { p = Pattern::IK; handshakeMessages = 2; return true; }
    if (name.rfind("Noise_K_", 0) == 0) { p = Pattern::K; handshakeMessages = 1; return true; }
    return false;
}

KeyPair pairOf(const std::string& hex) {
    KeyPair k;
    check(noise::keyPairFrom(keyOf(hex), k), "a private key from a vector is refused");
    return k;
}

// Both ends of one published vector: every message each side writes has to be the vector's,
// and each has to read what the other wrote. Then the transport messages, on Split's keys.
void runVector(const Block& v, int index) {
    const std::string name = v.at("handshake");
    const std::string label = name + " #" + std::to_string(index);
    Pattern pattern;
    size_t count = 0;
    if (!patternOf(name, pattern, count)) {
        check(false, label + ": not a pattern this code runs");
        return;
    }
    Handshake::Config ic;
    Handshake::Config rc;
    ic.pattern = rc.pattern = pattern;
    ic.initiator = true;
    rc.initiator = false;
    if (v.count("prologue")) {
        const Bytes p = unhex(v.at("prologue"));
        ic.prologue = rc.prologue = std::string(p.begin(), p.end());
    }
    if (v.count("init_static")) ic.s = pairOf(v.at("init_static"));
    if (v.count("resp_static")) rc.s = pairOf(v.at("resp_static"));
    if (v.count("gen_init_ephemeral")) ic.e = pairOf(v.at("gen_init_ephemeral"));
    if (v.count("gen_resp_ephemeral")) rc.e = pairOf(v.at("gen_resp_ephemeral"));
    if (v.count("preshared_key")) ic.psk = rc.psk = keyOf(v.at("preshared_key"));
    if (pattern == Pattern::IK || pattern == Pattern::K) ic.rs = rc.s.pub;
    if (pattern == Pattern::K) rc.rs = ic.s.pub;

    Handshake initiator(ic);
    Handshake responder(rc);
    size_t i = 0;
    for (; i < count; ++i) {
        const std::string msg = "msg_" + std::to_string(i);
        if (!v.count(msg + "_ciphertext")) {
            check(false, label + ": the vector ends inside the handshake");
            return;
        }
        Bytes payload = unhex(v.at(msg + "_payload"));
        const Bytes want = unhex(v.at(msg + "_ciphertext"));
        Handshake& writer = i % 2 == 0 ? initiator : responder;
        Handshake& reader = i % 2 == 0 ? responder : initiator;
        Bytes wrote;
        Bytes read;
        check(writer.write(payload, wrote), label + ": " + msg + " cannot be written");
        check(wrote == want, label + ": " + msg + " differs from the vector");
        check(reader.read(want, read), label + ": " + msg + " cannot be read");
        check(read == payload, label + ": " + msg + " reads back wrong");
    }
    check(initiator.finished() && responder.finished(), label + ": the handshake did not finish");
    check(initiator.hash() == responder.hash(), label + ": the two sides hold different hashes");
    if (v.count("handshake_hash"))
        check(hexOf(Bytes(initiator.hash().begin(), initiator.hash().end())) == v.at("handshake_hash"),
              label + ": the handshake hash differs from the vector");

    // The transport messages, in the order the vector's source sends them (noise-vectors.txt).
    uint64_t n1 = 0;
    uint64_t n2 = 0;
    for (;; ++i) {
        const std::string msg = "msg_" + std::to_string(i);
        if (!v.count(msg + "_ciphertext")) break;
        const Bytes payload = unhex(v.at(msg + "_payload"));
        const Bytes want = unhex(v.at(msg + "_ciphertext"));
        const bool fromInitiator = v.count("source") ? (pattern == Pattern::K || i % 2 == 0) : (i - count) % 2 == 0;
        const Key& key = fromInitiator ? initiator.send1() : initiator.send2();
        uint64_t& n = fromInitiator ? n1 : n2;
        const Bytes wrote = noise::seal(key, n, nullptr, 0, payload.data(), payload.size());
        check(wrote == want, label + ": transport " + msg + " differs from the vector");
        Bytes read;
        const Key& theirKey = fromInitiator ? responder.send1() : responder.send2();
        check(noise::open(theirKey, n, nullptr, 0, want.data(), want.size(), read), label + ": transport " + msg + " cannot be read");
        check(read == payload, label + ": transport " + msg + " reads back wrong");
        ++n;
    }
}

void publishedVectors(const std::string& folder) {
    const std::vector<Block> blocks = readBlocks(folder + "/noise-vectors.txt");
    check(blocks.size() >= 16, "too few published vectors were found");
    std::map<std::string, int> seen;
    int index = 0;
    for (const Block& v : blocks) {
        if (!v.count("handshake")) continue;
        runVector(v, index++);
        ++seen[v.at("handshake")];
    }
    for (const char* name : {"Noise_XXpsk0_25519_ChaChaPoly_SHA256", "Noise_XX_25519_ChaChaPoly_SHA256",
                             "Noise_IK_25519_ChaChaPoly_SHA256", "Noise_K_25519_ChaChaPoly_SHA256"})
        check(seen[name] >= 4, std::string("too few vectors of ") + name);
    std::printf("  %d published vectors\n", index);
}

// A low-order public key has to end a handshake where the DH happens, not produce a key.
void lowOrderKeys() {
    static const char* kLowOrder[] = {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0100000000000000000000000000000000000000000000000000000000000000",
        "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800",
        "5f9c95bca3508c24b1d0b1559c83ef5b04445cc4581c8e86d8224eddd09f1157",
        "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
    };
    for (const char* hex : kLowOrder) {
        Handshake::Config c;
        c.pattern = Pattern::IK;
        c.initiator = true;
        check(noise::newKeyPair(c.s), "a new key pair");
        c.rs = keyOf(hex);
        Handshake h(c);
        Bytes out;
        check(!h.write({}, out), std::string("a low-order key was accepted: ") + hex);
    }
}

// The same message changed by one bit, and a message cut short, end the handshake; so does
// anything after that.
void brokenMessages() {
    Handshake::Config ic;
    Handshake::Config rc;
    ic.pattern = rc.pattern = Pattern::IK;
    ic.initiator = true;
    rc.initiator = false;
    check(noise::newKeyPair(ic.s) && noise::newKeyPair(rc.s), "new key pairs");
    ic.rs = rc.s.pub;
    {
        Handshake i(ic);
        Handshake r(rc);
        Bytes m;
        Bytes p;
        check(i.write(bytesOf("hello"), m), "a message 1");
        m[m.size() - 1] ^= 1;
        check(!r.read(m, p), "a changed message was read");
        m[m.size() - 1] ^= 1;
        check(!r.read(m, p), "a handshake went on after a failed read");
    }
    {
        Handshake i(ic);
        Handshake r(rc);
        Bytes m;
        Bytes p;
        check(i.write({}, m), "a message 1");
        m.resize(10);
        check(!r.read(m, p), "a short message was read");
    }
    {
        // The wrong side sending out of turn is refused, not reordered.
        Handshake i(ic);
        Bytes m;
        Bytes p;
        check(i.write({}, m), "a message 1");
        check(!i.write({}, m), "the initiator wrote twice in a row");
    }
    // A wrong pre-shared key fails at message 1, before the server's answer means anything.
    {
        Handshake::Config a;
        Handshake::Config b;
        a.pattern = b.pattern = Pattern::XXpsk0;
        a.initiator = true;
        b.initiator = false;
        check(noise::newKeyPair(a.s) && noise::newKeyPair(b.s), "new key pairs");
        a.psk = keyOf(std::string(64, 'a'));
        b.psk = keyOf(std::string(64, 'b'));
        Handshake i(a);
        Handshake r(b);
        Bytes m;
        Bytes p;
        check(i.write({}, m), "message 1");
        check(m.size() == 48, "message 1 is 48 bytes, as PROTOCOL.md says");
        check(!r.read(m, p), "the wrong code passed");
    }
}

// The protocol's own known answer, and the salt's length with it.
void slowHashAnswer() {
    Key k;
    check(noise::slowHash("K7MR4QXT9HPV", k), "the slow hash did not run");
    check(hexOf(Bytes(k.begin(), k.end())) == "c3c7e78d316b58eff025cd98473fc042a0ebd4ff209be27d491583c495e8b6ad",
          "the slow hash differs from PROTOCOL.md's known answer");
}

// What the server's code wrote: a pairing, a session and boxes, with this side's short-lived
// keys fixed so that each of this side's messages must come out byte for byte.
void serverAnswers(const std::string& folder) {
    const std::vector<Block> blocks = readBlocks(folder + "/proof-vectors.txt");
    check(blocks.size() == 3, "proof-vectors.txt holds a pairing, a session and boxes");
    if (blocks.size() != 3) return;

    {
        const Block& v = blocks[0];
        Key k;
        check(noise::slowHash(v.at("code"), k), "the code's slow hash");
        check(hexOf(Bytes(k.begin(), k.end())) == v.at("k"), "the slow hash differs from the server's");
        Handshake::Config c;
        c.pattern = Pattern::XXpsk0;
        c.initiator = true;
        c.prologue = "cabinet-linkup pair 1";
        c.s = pairOf(v.at("console_private"));
        c.e = pairOf(v.at("console_ephemeral"));
        c.psk = k;
        Handshake h(c);
        Bytes m1;
        check(h.write({}, m1), "pairing message 1");
        check(hexOf(m1) == v.at("msg1"), "pairing message 1 differs from what the server accepted");
        Bytes hello;
        check(h.read(unhex(v.at("msg2")), hello), "the server's message 2 cannot be read");
        check(hexOf(hello) == v.at("hello"), "the server's hello reads wrong");
        check(h.remoteStatic() && hexOf(Bytes(h.remoteStatic()->begin(), h.remoteStatic()->end())) == v.at("server_public"),
              "the server's key reads wrong");
        Bytes m3;
        check(h.write(unhex(v.at("joining")), m3), "pairing message 3");
        check(hexOf(m3) == v.at("msg3"), "pairing message 3 differs from what the server accepted");
        check(h.finished(), "the pairing did not finish");
        Bytes reply;
        const Bytes sealedReply = unhex(v.at("reply"));
        check(noise::open(h.send2(), 0, nullptr, 0, sealedReply.data(), sealedReply.size(), reply),
              "the sealed reply cannot be opened");
        check(hexOf(reply) == v.at("reply_plain"), "the sealed reply reads wrong");
    }
    {
        const Block& v = blocks[1];
        Handshake::Config c;
        c.pattern = Pattern::IK;
        c.initiator = true;
        c.prologue = "cabinet-linkup session 1";
        c.s = pairOf(v.at("console_private"));
        c.e = pairOf(v.at("console_ephemeral"));
        c.rs = keyOf(v.at("server_public"));
        Handshake h(c);
        Bytes m1;
        check(h.write(unhex(v.at("open_payload")), m1), "the Open");
        check(hexOf(m1) == v.at("open"), "the Open differs from what the server accepted");
        Bytes answer;
        check(h.read(unhex(v.at("answer")), answer), "the Open's answer cannot be read");
        check(hexOf(answer) == v.at("answer_payload"), "the Open's answer reads wrong");
        check(h.finished(), "the session's handshake did not finish");

        noise::SessionId id{};
        const Bytes idBytes = unhex(v.at("session"));
        check(idBytes.size() == id.size(), "a session id is 8 bytes");
        std::memcpy(id.data(), idBytes.data(), id.size());
        const uint64_t counter = std::stoull(v.at("counter"));
        const Bytes body = noise::sealRequest(h.send1(), id, counter, unhex(v.at("request_plain")));
        check(hexOf(body) == v.at("request"), "a sealed request differs from what the server accepted");
        Bytes plain;
        check(noise::openAnswer(h.send2(), id, counter, unhex(v.at("reply")), plain), "a sealed answer cannot be opened");
        check(hexOf(plain) == v.at("reply_plain"), "a sealed answer reads wrong");
        Bytes refused;
        check(!noise::openAnswer(h.send2(), id, counter + 1, unhex(v.at("reply")), refused),
              "an answer opened under another counter");
        check(!noise::openAnswer(h.send1(), id, counter, unhex(v.at("reply")), refused),
              "an answer opened under the wrong direction's key");
    }
    {
        const Block& v = blocks[2];
        const KeyPair console = pairOf(v.at("console_private"));
        const KeyPair frnd = pairOf(v.at("friend_private"));
        // One the server's code sealed: the console has to open it.
        Bytes plain;
        check(box::open(console, frnd.pub, v.at("invite"), unhex(v.at("from_friend")), plain), "a box the server's code sealed");
        check(hexOf(plain) == v.at("from_friend_plain"), "that box reads wrong");
        // One this side seals with a fixed key: the server's code opened exactly these bytes.
        const KeyPair fixed = pairOf(v.at("console_ephemeral"));
        Bytes mine;
        check(box::seal(console, frnd.pub, v.at("invite"), unhex(v.at("to_friend_plain")), mine, &fixed), "sealing a box");
        check(hexOf(mine) == v.at("to_friend"), "a box differs from what the server's code opened");
        // Bound to the invite, and to who sent it.
        check(!box::open(console, frnd.pub, v.at("invite") + "x", unhex(v.at("from_friend")), plain), "a box opened for another invite");
        check(!box::open(console, console.pub, v.at("invite"), unhex(v.at("from_friend")), plain), "a box opened as from someone else");
    }
    std::printf("  the server's answers\n");
}

// What a friend's console or a server can put on a television goes through this.
void cleanTextHolds() {
    const auto clean = [](const std::string& t, size_t max = 100) { return box::cleanText(t, max); };
    check(clean("Contra III") == "Contra III", "plain text changed");
    check(clean("Pok\xC3\xA9mon \xE3\x83\x9E\xF0\x9F\x8E\xAE") == "Pok\xC3\xA9mon \xE3\x83\x9E\xF0\x9F\x8E\xAE", "real UTF-8 was changed");
    check(clean("a\nb\tc\x7F" "d") == "abcd", "control characters stayed");
    check(clean("a\xC2\x85" "b") == "ab", "a C1 control stayed");
    check(clean("a\xE2\x80\xAE" "b") == "ab", "a right-to-left override stayed");
    check(clean("a\xE2\x80\x8B" "b\xEF\xBB\xBF" "c") == "abc", "a zero-width character stayed");
    check(clean("a\xEE\x80\x80" "b\xF3\xA0\x80\x81" "c") == "abc", "a private use or tag character stayed");
    check(clean("a\xC0\x80" "b") == "ab", "an overlong form stayed");
    check(clean("a\xED\xA0\x80" "b") == "ab", "a surrogate stayed");
    check(clean("a\xC3") == "a", "a cut character stayed");
    check(clean("a\xFF" "b") == "ab", "a byte that is no UTF-8 stayed");
    check(clean("\xC3\xA9\xC3\xA9", 3) == "\xC3\xA9", "cut inside a character");
    check(clean("abcdef", 3) == "abc", "not cut at the limit");
    std::printf("  names cleaned on the way in\n");
}

// A sealed box can be sent twice, or sent to someone else, so a reader drops what does not fit.
void readerDropsWhatDoesNotFit() {
    const auto bytes = [](const std::string& t) { return Bytes(t.begin(), t.end()); };
    box::Contents invite;
    invite.type = "invite";
    invite.n = 1;
    invite.expires = 1000;
    invite.from = "alex";
    invite.to = "sam";
    invite.stream = 1;
    invite.game = "Contra\nIII";
    const Bytes good = bytes(box::write(invite));
    box::Contents out;

    box::Reader first;
    check(first.read(good, "invite", "alex", "sam", 1000, out), "a good invite was dropped");
    check(out.game == "ContraIII" && out.stream == 1 && out.from == "alex", "an invite read wrongly");
    check(!first.read(good, "invite", "alex", "sam", 1000, out), "the same box was read twice");
    box::Contents later = invite;
    later.n = 2;
    check(first.read(bytes(box::write(later)), "invite", "alex", "sam", 1000, out), "the next count was dropped");

    box::Reader r;
    check(!r.read(good, "invite", "alex", "sam", 1001, out), "a box past its time was read");
    check(!r.read(good, "signal", "alex", "sam", 1000, out), "a box of another type was read");
    check(!r.read(good, "invite", "riley", "sam", 1000, out), "a box from someone else was read");
    check(!r.read(good, "invite", "alex", "riley", 1000, out), "a box to someone else was read");
    check(!r.read(bytes("not json"), "invite", "alex", "sam", 1000, out), "something that is not JSON was read");
    check(!r.read(bytes("[1]"), "invite", "alex", "sam", 1000, out), "a list was read");
    box::Contents none = invite;
    none.n = 0;
    check(!r.read(bytes(box::write(none)), "invite", "alex", "sam", 1000, out), "a count of 0 was read");
    check(!r.read(bytes("{\"type\":\"invite\",\"n\":9,\"expires\":1000,\"from\":\"alex\",\"to\":\"sam\"}"), "invite", "alex", "sam", 1000, out),
          "an invite with no game was read");
    check(!r.read(bytes("{\"type\":\"signal\",\"n\":9,\"expires\":1000,\"from\":\"alex\",\"to\":\"sam\"}"), "signal", "alex", "sam", 1000, out),
          "a signal with no details was read");
    // A dropped box does not use up its count.
    check(r.read(good, "invite", "alex", "sam", 1000, out), "a dropped box used up its count");

    box::Contents signal;
    signal.type = "signal";
    signal.n = 1;
    signal.expires = 1000;
    signal.from = "sam";
    signal.to = "alex";
    signal.details = "{\"kind\":\"offer\"}";
    box::Reader s;
    check(s.read(bytes(box::write(signal)), "signal", "sam", "alex", 1000, out) && out.details == signal.details, "a signal read wrongly");
    std::printf("  boxes that do not fit are dropped\n");
}

// PROTOCOL.md's table of what an address means.
void addressesMean() {
    using List = std::vector<std::string>;
    check(ServerLink::bases("name.example.net") == List{"https://name.example.net", "http://name.example.net"}, "a host name");
    check(ServerLink::bases("name.example.net:7845") == List{"http://name.example.net:7845"}, "a host and a port");
    check(ServerLink::bases("https://name.example.net") == List{"https://name.example.net"}, "a scheme is kept");
    check(ServerLink::bases("http://name.example.net:7845/") == List{"http://name.example.net:7845"}, "a last slash goes");
    check(ServerLink::bases("  name.example.net/linkup/  ") == List{"https://name.example.net/linkup", "http://name.example.net/linkup"}, "a path, with spaces");
    check(ServerLink::bases("[2001:db8::1]:7845") == List{"http://[2001:db8::1]:7845"}, "IPv6 with a port");
    check(ServerLink::bases("[2001:db8::1]") == List{"https://[2001:db8::1]", "http://[2001:db8::1]"}, "IPv6 with none");
    check(ServerLink::bases("").empty() && ServerLink::bases("  /").empty(), "no address is no place");
    std::printf("  what an address means\n");
}

}  // namespace

int main(int argc, char** argv) {
    const std::string folder = argc > 1 ? argv[1] : ".";
    publishedVectors(folder);
    lowOrderKeys();
    brokenMessages();
    slowHashAnswer();
    serverAnswers(folder);
    cleanTextHolds();
    readerDropsWhatDoesNotFit();
    addressesMean();
    if (failures > 0) {
        std::fprintf(stderr, "%d failed\n", failures);
        return 1;
    }
    std::printf("noise-test: all passed\n");
    return 0;
}
