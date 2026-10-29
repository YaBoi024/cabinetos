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
            ok = putText(body, m.core) && putText(body, m.coreVersion) &&
                 putText(body, m.gameHash) && putText(body, m.saveHash);
            break;
        case Type::Accept:
            put8(body, m.players);
            put8(body, m.sync);
            put32(body, m.saveSize);
            break;
        case Type::Start:
            put8(body, m.players);
            put8(body, m.player);
            put8(body, m.delay);
            break;
        case Type::Roster:
            put8(body, m.players);
            break;
        case Type::Left:
            put8(body, m.player);
            put32(body, m.frame);
            break;
        case Type::Lead:
            put8(body, m.lead);
            break;
        case Type::Save:
            if (m.saveBytes.empty() || m.saveBytes.size() > kMaxSaveChunk) { ok = false; break; }
            put32(body, m.saveSize);
            put32(body, m.saveOffset);
            body.insert(body.end(), m.saveBytes.begin(), m.saveBytes.end());
            break;
        case Type::Refuse:
            ok = putText(body, m.reason);
            break;
        case Type::Inputs:
            put8(body, m.player);
            put32(body, m.frame);
            putPad(body, m.pad);
            break;
        case Type::Digest:
            put8(body, m.player);
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
            m.saveHash = r.text();
            break;
        case static_cast<uint8_t>(Type::Accept):
            m.type = Type::Accept;
            m.players = r.u8();
            m.sync = r.u8();
            m.saveSize = r.u32();
            if (m.players < 2 || m.players > kMaxPlayers) r.ok = false;
            if (m.sync > 1 || m.saveSize > kMaxSave || (m.sync == 0 && m.saveSize != 0)) r.ok = false;
            break;
        case static_cast<uint8_t>(Type::Start):
            m.type = Type::Start;
            m.players = r.u8();
            m.player = r.u8();
            m.delay = r.u8();
            // Two to the most; a guest is never slot 0, which is the host's.
            if (m.players < 2 || m.players > kMaxPlayers || m.player < 1 || m.player >= m.players)
                r.ok = false;
            break;
        case static_cast<uint8_t>(Type::Roster):
            m.type = Type::Roster;
            m.players = r.u8();
            if (m.players < 1 || m.players > kMaxPlayers) r.ok = false;
            break;
        case static_cast<uint8_t>(Type::Left):
            m.type = Type::Left;
            m.player = r.u8();
            m.frame = r.u32();
            if (m.player < 1 || m.player >= kMaxPlayers) r.ok = false;   // the host's leaving is Bye
            break;
        case static_cast<uint8_t>(Type::Lead):
            m.type = Type::Lead;
            m.lead = r.u8();
            if (m.lead < 1 || m.lead > kMaxLead) r.ok = false;
            break;
        case static_cast<uint8_t>(Type::Save): {
            m.type = Type::Save;
            m.saveSize = r.u32();
            m.saveOffset = r.u32();
            const size_t n = r.left;
            if (n < 1 || n > kMaxSaveChunk || m.saveSize > kMaxSave ||
                static_cast<uint64_t>(m.saveOffset) + n > m.saveSize) {
                r.ok = false;
                break;
            }
            m.saveBytes.assign(r.p, r.p + n);
            r.p += n;
            r.left = 0;
            break;
        }
        case static_cast<uint8_t>(Type::Refuse):
            m.type = Type::Refuse;
            m.reason = r.text();
            break;
        case static_cast<uint8_t>(Type::Inputs):
            m.type = Type::Inputs;
            m.player = r.u8();
            m.frame = r.u32();
            m.pad = getPad(r);
            if (m.player >= kMaxPlayers) r.ok = false;
            break;
        case static_cast<uint8_t>(Type::Digest):
            m.type = Type::Digest;
            m.player = r.u8();
            m.frame = r.u32();
            m.digest = r.u64();
            if (m.player >= kMaxPlayers) r.ok = false;
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
    hello.players = 1;
    hello.core = "snes9x";
    hello.coreVersion = "Snes9x 1.63 890b5d4";
    hello.gameHash = "0123456789abcdef0123456789abcdef";
    hello.saveHash = "00ff00ff00ff00ff";
    Message accept;
    accept.type = Type::Accept;
    accept.players = 4;
    accept.sync = 1;
    accept.saveSize = 296960;
    Message start;
    start.type = Type::Start;
    start.players = 4;
    start.player = 2;
    start.delay = 3;
    Message roster;
    roster.type = Type::Roster;
    roster.players = 3;
    Message left;
    left.type = Type::Left;
    left.player = 2;
    left.frame = 500;
    Message lead;
    lead.type = Type::Lead;
    lead.lead = 4;
    Message save;
    save.type = Type::Save;
    save.saveSize = 1000;
    save.saveOffset = 600;
    save.saveBytes.assign(400, 0x5A);
    Message refuse;
    refuse.type = Type::Refuse;
    refuse.reason = "different game file";
    Message inputs;
    inputs.type = Type::Inputs;
    inputs.player = 3;
    inputs.frame = 123456;
    inputs.pad = {0xA5C3, -32767, 32767, -1, 0, 255, 7};
    Message digest;
    digest.type = Type::Digest;
    digest.player = 2;
    digest.frame = 60;
    digest.digest = 0x24a27075b447a290ull;
    Message bye;
    bye.type = Type::Bye;

    // Every message comes back as it went in, fed whole and fed a byte at a time.
    for (const Message* m : {&hello, &accept, &refuse, &inputs, &digest, &bye, &lead, &save, &start,
                             &roster, &left}) {
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
                          back.coreVersion == m->coreVersion && back.gameHash == m->gameHash &&
                          back.saveHash == m->saveHash;
                    break;
                case Type::Accept:
                    got = back.players == m->players && back.sync == m->sync &&
                          back.saveSize == m->saveSize;
                    break;
                case Type::Start:
                    got = back.players == m->players && back.player == m->player &&
                          back.delay == m->delay;
                    break;
                case Type::Roster: got = back.players == m->players; break;
                case Type::Left: got = back.player == m->player && back.frame == m->frame; break;
                case Type::Lead: got = back.lead == m->lead; break;
                case Type::Save:
                    got = back.saveSize == m->saveSize && back.saveOffset == m->saveOffset &&
                          back.saveBytes == m->saveBytes;
                    break;
                case Type::Refuse: got = back.reason == m->reason; break;
                case Type::Inputs:
                    got = back.player == m->player && back.frame == m->frame && back.pad == m->pad;
                    break;
                case Type::Digest:
                    got = back.player == m->player && back.frame == m->frame &&
                          back.digest == m->digest;
                    break;
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
    fails({0x00, 0x08, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09}, "extra bytes after an Accept fail");
    fails({0x00, 0x02, 0x02, 0x03}, "a short Accept fails");
    fails({0x00, 0x06, 0x04, 0x01, 0x00, 0x00, 0x00, 0x01}, "a short Inputs fails");
    fails({0x00, 0x03, 0x03, 0x09, 'x'}, "a text longer than its message fails");
    {
        std::vector<uint8_t> big = {0x00, 0x66, 0x03, 0x65};   // a text of 101
        big.resize(2 + 0x66, 'a');
        fails(big, "a text over the limit fails");
    }
    {
        Message bad0 = lead;
        bad0.lead = 0;
        Message bad9 = lead;
        bad9.lead = kMaxLead + 1;
        Parser p0, p9;
        const std::vector<uint8_t> b0 = encode(bad0), b9 = encode(bad9);
        p0.feed(b0.data(), b0.size());
        p9.feed(b9.data(), b9.size());
        Message x;
        check(!p0.next(x) && p0.failed() && !p9.next(x) && p9.failed(),
              "a Lead of 0 or past the limit fails");
    }
    {
        Message past = save;
        past.saveOffset = 700;   // 700 + 400 is beyond the 1000 bytes
        Message empty = save;
        empty.saveBytes.clear();
        Message huge = save;
        huge.saveBytes.assign(kMaxSaveChunk + 1, 1);
        Parser p;
        const std::vector<uint8_t> b = encode(past);
        p.feed(b.data(), b.size());
        Message x;
        check(!p.next(x) && p.failed(), "a piece of a save that runs past its end fails");
        check(encode(empty).empty() && encode(huge).empty(), "an empty or oversize piece is not sent");
    }
    {
        // Slots and counts that cannot be right.
        auto bad = [&](Message m, const char* what) {
            const std::vector<uint8_t> b = encode(m);
            Parser p;
            p.feed(b.data(), b.size());
            Message x;
            check(!b.empty() && !p.next(x) && p.failed(), what);
        };
        Message m = start;
        m.player = 0;
        bad(m, "a Start that makes a guest the host's slot fails");
        m = start;
        m.player = m.players;
        bad(m, "a Start with a slot past the players fails");
        m = start;
        m.players = 1;
        m.player = 1;
        bad(m, "a Start for one player fails");
        m = start;
        m.players = kMaxPlayers + 1;
        bad(m, "a Start for more than the most fails");
        m = roster;
        m.players = 0;
        bad(m, "a Roster of nobody fails");
        m = left;
        m.player = 0;
        bad(m, "a Left for the host fails (that is Bye)");
        m = inputs;
        m.player = kMaxPlayers;
        bad(m, "Inputs for a slot past the most fail");
        m = digest;
        m.player = kMaxPlayers;
        bad(m, "a Digest for a slot past the most fails");
        m = accept;
        m.players = 1;
        bad(m, "an Accept for one player fails");
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
