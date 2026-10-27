// FORK (handheld): see netplaywire.h.

#include "netplaywire.h"

#include <cstdio>
#include <cstring>
#include <utility>

namespace netplaywire {
namespace {

void put8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
void put16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(static_cast<uint8_t>(v >> 8));
    o.push_back(static_cast<uint8_t>(v));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
    put16(o, static_cast<uint16_t>(v >> 16));
    put16(o, static_cast<uint16_t>(v));
}
void put64(std::vector<uint8_t>& o, uint64_t v) {
    put32(o, static_cast<uint32_t>(v >> 32));
    put32(o, static_cast<uint32_t>(v));
}
bool putText(std::vector<uint8_t>& o, const std::string& s) {
    if (s.size() > kMaxText) return false;
    put8(o, static_cast<uint8_t>(s.size()));
    o.insert(o.end(), s.begin(), s.end());
    return true;
}

// Reads a body that must be used up exactly.
struct Reader {
    const uint8_t* p;
    size_t left;
    bool ok = true;
    uint8_t u8() {
        if (left < 1) { ok = false; return 0; }
        --left;
        return *p++;
    }
    uint16_t u16() {
        const uint16_t hi = u8();
        return static_cast<uint16_t>((hi << 8) | u8());
    }
    uint32_t u32() {
        const uint32_t hi = u16();
        return (hi << 16) | u16();
    }
    uint64_t u64() {
        const uint64_t hi = u32();
        return (hi << 32) | u32();
    }
    std::string text() {
        const size_t n = u8();
        if (!ok || n > kMaxText || n > left) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        left -= n;
        return s;
    }
};

void putPad(std::vector<uint8_t>& o, const Pad& d) {
    put16(o, d.buttons);
    put16(o, static_cast<uint16_t>(d.leftX));
    put16(o, static_cast<uint16_t>(d.leftY));
    put16(o, static_cast<uint16_t>(d.rightX));
    put16(o, static_cast<uint16_t>(d.rightY));
    put8(o, d.leftTrigger);
    put8(o, d.rightTrigger);
}

Pad getPad(Reader& r) {
    Pad d;
    d.buttons = r.u16();
    d.leftX = static_cast<int16_t>(r.u16());
    d.leftY = static_cast<int16_t>(r.u16());
    d.rightX = static_cast<int16_t>(r.u16());
    d.rightY = static_cast<int16_t>(r.u16());
    d.leftTrigger = r.u8();
    d.rightTrigger = r.u8();
    return d;
}

}  // namespace

std::vector<uint8_t> encode(const Message& m) {
    std::vector<uint8_t> body;
    put8(body, static_cast<uint8_t>(m.type));
    bool ok = true;
    switch (m.type) {
        case Type::Hello:
            put16(body, m.version);
            put16(body, m.code);
            put8(body, m.players);
            ok = putText(body, m.core) && putText(body, m.coreVersion) && putText(body, m.gameHash);
            break;
        case Type::Accept:
            put8(body, m.delay);
            put8(body, m.players);
            break;
        case Type::Refuse:
            ok = putText(body, m.reason);
            break;
        case Type::Inputs:
            put32(body, m.frame);
            putPad(body, m.pad);
            break;
        case Type::Digest:
            put32(body, m.frame);
            put64(body, m.digest);
            break;
        case Type::Bye:
            break;
    }
    if (!ok || body.size() > kMaxMessage) return {};
    std::vector<uint8_t> out;
    put16(out, static_cast<uint16_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

void Parser::fail(const char* why) {
    failed_ = true;
    error_ = why;
    buf_.clear();
}

void Parser::feed(const uint8_t* data, size_t n) {
    if (failed_) return;
    // Never hold more than one message and the start of the next.
    if (buf_.size() + n > 2 * (kMaxMessage + 2)) {
        fail("too much data waiting");
        return;
    }
    buf_.insert(buf_.end(), data, data + n);
}

bool Parser::next(Message& out) {
    if (failed_ || buf_.size() < 2) return false;
    const size_t len = (static_cast<size_t>(buf_[0]) << 8) | buf_[1];
    if (len < 1 || len > kMaxMessage) {
        fail("bad length");
        return false;
    }
    if (buf_.size() < 2 + len) return false;
    Reader r{buf_.data() + 3, len - 1};
    Message m;
    const uint8_t type = buf_[2];
    switch (type) {
        case static_cast<uint8_t>(Type::Hello):
            m.type = Type::Hello;
            m.version = r.u16();
            m.code = r.u16();
            m.players = r.u8();
            m.core = r.text();
            m.coreVersion = r.text();
            m.gameHash = r.text();
            break;
        case static_cast<uint8_t>(Type::Accept):
            m.type = Type::Accept;
            m.delay = r.u8();
            m.players = r.u8();
            break;
        case static_cast<uint8_t>(Type::Refuse):
            m.type = Type::Refuse;
            m.reason = r.text();
            break;
        case static_cast<uint8_t>(Type::Inputs):
            m.type = Type::Inputs;
            m.frame = r.u32();
            m.pad = getPad(r);
            break;
        case static_cast<uint8_t>(Type::Digest):
            m.type = Type::Digest;
            m.frame = r.u32();
            m.digest = r.u64();
            break;
        case static_cast<uint8_t>(Type::Bye):
            m.type = Type::Bye;
            break;
        default:
            fail("unknown message type");
            return false;
    }
    if (!r.ok || r.left != 0) {
        fail("body does not fit its type");
        return false;
    }
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(2 + len));
    out = std::move(m);
    return true;
}

// ---- --netplay-wire-test ----------------------------------------------------

int test() {
    int bad = 0;
    auto check = [&bad](bool ok, const char* what) {
        std::fprintf(stderr, "[wire] %-52s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++bad;
    };

    Message hello;
    hello.type = Type::Hello;
    hello.code = 4821;
    hello.players = 2;
    hello.core = "snes9x";
    hello.coreVersion = "Snes9x 1.63 890b5d4";
    hello.gameHash = "0123456789abcdef0123456789abcdef";
    Message accept;
    accept.type = Type::Accept;
    accept.delay = 3;
    accept.players = 2;
    Message refuse;
    refuse.type = Type::Refuse;
    refuse.reason = "different game file";
    Message inputs;
    inputs.type = Type::Inputs;
    inputs.frame = 123456;
    inputs.pad = {0xA5C3, -32767, 32767, -1, 0, 255, 7};
    Message digest;
    digest.type = Type::Digest;
    digest.frame = 60;
    digest.digest = 0x24a27075b447a290ull;
    Message bye;
    bye.type = Type::Bye;

    // Every message comes back as it went in, fed whole and fed a byte at a time.
    for (const Message* m : {&hello, &accept, &refuse, &inputs, &digest, &bye}) {
        const std::vector<uint8_t> bytes = encode(*m);
        Parser whole;
        whole.feed(bytes.data(), bytes.size());
        Message back;
        bool got = whole.next(back) && back.type == m->type;
        if (got) {
            switch (m->type) {
                case Type::Hello:
                    got = back.version == m->version && back.code == m->code &&
                          back.players == m->players && back.core == m->core &&
                          back.coreVersion == m->coreVersion && back.gameHash == m->gameHash;
                    break;
                case Type::Accept: got = back.delay == m->delay && back.players == m->players; break;
                case Type::Refuse: got = back.reason == m->reason; break;
                case Type::Inputs: got = back.frame == m->frame && back.pad == m->pad; break;
                case Type::Digest: got = back.frame == m->frame && back.digest == m->digest; break;
                case Type::Bye: break;
            }
        }
        Parser slow;
        Message again;
        bool early = false;   // a message before its last byte is a fault
        for (size_t i = 0; i + 1 < bytes.size(); ++i) {
            slow.feed(&bytes[i], 1);
            early = early || slow.next(again);
        }
        slow.feed(&bytes[bytes.size() - 1], 1);
        const bool gotSlow = !early && slow.next(again) && again.type == m->type;
        check(got && gotSlow, "a message survives the wire, whole and by the byte");
    }

    // Two messages in one piece come out one at a time.
    {
        std::vector<uint8_t> two = encode(inputs);
        const std::vector<uint8_t> b = encode(bye);
        two.insert(two.end(), b.begin(), b.end());
        Parser p;
        p.feed(two.data(), two.size());
        Message x, y, z;
        check(p.next(x) && x.type == Type::Inputs && p.next(y) && y.type == Type::Bye && !p.next(z),
              "two messages in one piece come out in order");
    }

    // Everything below must fail the parser, and keep it failed.
    auto fails = [&check](std::vector<uint8_t> bytes, const char* what) {
        Parser p;
        p.feed(bytes.data(), bytes.size());
        Message m;
        const bool none = !p.next(m);
        const bool dead = p.failed();
        const std::vector<uint8_t> fine = encode(Message{});
        p.feed(fine.data(), fine.size());
        check(none && dead && !p.next(m), what);
    };
    fails({0x00, 0x00}, "a length of zero fails");
    fails({0x02, 0x01, 0x01}, "a length over the limit fails");
    fails({0x00, 0x02, 0x63, 0x00}, "an unknown type fails");
    fails({0x00, 0x04, 0x02, 0x03, 0x02, 0x09}, "extra bytes after an Accept fail");
    fails({0x00, 0x02, 0x02, 0x03}, "a short Accept fails");
    fails({0x00, 0x05, 0x04, 0x00, 0x00, 0x00, 0x01}, "a short Inputs fails");
    fails({0x00, 0x03, 0x03, 0x09, 'x'}, "a text longer than its message fails");
    {
        std::vector<uint8_t> big = {0x00, 0x66, 0x03, 0x65};   // a text of 101
        big.resize(2 + 0x66, 'a');
        fails(big, "a text over the limit fails");
    }
    {
        Message tooLong = refuse;
        tooLong.reason.assign(kMaxText + 1, 'a');
        check(encode(tooLong).empty(), "a text over the limit is not sent");
    }
    {
        Parser p;
        std::vector<uint8_t> junk(4 * (kMaxMessage + 2), 0x00);
        p.feed(junk.data(), junk.size());
        check(p.failed(), "a flood with no message in it fails");
    }

    std::fprintf(stderr, "[wire] %s\n", bad == 0 ? "PASS" : "FAIL");
    return bad == 0 ? 0 : 1;
}

}  // namespace netplaywire
