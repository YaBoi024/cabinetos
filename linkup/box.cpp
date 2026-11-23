// Sealed boxes between two consoles (box.h).

#include "box.h"

#include <cstring>

#include <json-c/json.h>

namespace box {

namespace {

std::string prologueFor(const std::string& invite) { return "cabinet-linkup box 1 " + invite; }

const char* textOf(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_string)) return nullptr;
    return json_object_get_string(v);
}

bool numberOf(json_object* o, const char* key, int64_t& out) {
    json_object* v = nullptr;
    if (!json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_int)) return false;
    out = json_object_get_int64(v);
    return true;
}

}  // namespace

bool seal(const noise::KeyPair& me, const noise::Key& peer, const std::string& invite, const noise::Bytes& plain,
          noise::Bytes& out, const noise::KeyPair* fixedEphemeral) {
    noise::Handshake::Config c;
    c.pattern = noise::Pattern::K;
    c.initiator = true;
    c.prologue = prologueFor(invite);
    c.s = me;
    c.rs = peer;
    if (fixedEphemeral) c.e = *fixedEphemeral;
    noise::Handshake h(c);
    return h.write(plain, out);
}

bool open(const noise::KeyPair& me, const noise::Key& peer, const std::string& invite, const noise::Bytes& sealed,
          noise::Bytes& plain) {
    noise::Handshake::Config c;
    c.pattern = noise::Pattern::K;
    c.initiator = false;
    c.prologue = prologueFor(invite);
    c.s = me;
    c.rs = peer;
    noise::Handshake h(c);
    return h.read(sealed, plain);
}

std::string cleanText(const std::string& text, size_t maxBytes) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        size_t len = 0;
        uint32_t cp = 0;
        if (b0 < 0x80) {
            len = 1;
            cp = b0;
        } else if (b0 >= 0xC2 && b0 <= 0xDF) {
            len = 2;
            cp = b0 & 0x1Fu;
        } else if (b0 >= 0xE0 && b0 <= 0xEF) {
            len = 3;
            cp = b0 & 0x0Fu;
        } else if (b0 >= 0xF0 && b0 <= 0xF4) {
            len = 4;
            cp = b0 & 0x07u;
        } else {
            ++i;
            continue;
        }
        if (i + len > text.size()) break;
        bool good = true;
        for (size_t j = 1; j < len; ++j) {
            const auto b = static_cast<unsigned char>(text[i + j]);
            if ((b & 0xC0u) != 0x80u) {
                good = false;
                break;
            }
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (!good) {
            ++i;
            continue;
        }
        const bool tooLong = (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF;
        const bool surrogate = cp >= 0xD800 && cp <= 0xDFFF;
        // C0, DEL and C1; the soft hyphen, the Arabic letter mark; everything from the zero-width
        // spaces through the marks; the embeddings, overrides and isolates; the word joiner and
        // its kin; the byte order mark; the private use planes and the tag characters.
        const bool control = cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || cp == 0xAD || cp == 0x061C ||
                             (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) ||
                             (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF || (cp >= 0xE000 && cp <= 0xF8FF) ||
                             (cp >= 0xE0000);
        if (!tooLong && !surrogate && !control) {
            if (out.size() + len > maxBytes) break;
            out.append(text, i, len);
        }
        i += len;
    }
    return out;
}

std::string write(const Contents& c) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string(c.type.c_str()));
    json_object_object_add(o, "n", json_object_new_int64(static_cast<int64_t>(c.n)));
    json_object_object_add(o, "expires", json_object_new_int64(c.expires));
    json_object_object_add(o, "from", json_object_new_string(c.from.c_str()));
    json_object_object_add(o, "to", json_object_new_string(c.to.c_str()));
    if (c.type == "invite") {
        json_object_object_add(o, "stream", json_object_new_int(c.stream));
        json_object_object_add(o, "game", json_object_new_string(c.game.c_str()));
    } else {
        json_object_object_add(o, "details", json_object_new_string(c.details.c_str()));
    }
    std::string out = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
    json_object_put(o);
    return out;
}

bool Reader::read(const noise::Bytes& plain, const std::string& wantType, const std::string& wantFrom,
                  const std::string& wantTo, int64_t now, Contents& out) {
    const std::string text(plain.begin(), plain.end());
    json_tokener* tok = json_tokener_new();
    json_object* o = json_tokener_parse_ex(tok, text.c_str(), static_cast<int>(text.size()));
    const bool parsed = json_tokener_get_error(tok) == json_tokener_success;
    json_tokener_free(tok);
    if (!parsed || !o || !json_object_is_type(o, json_type_object)) {
        if (o) json_object_put(o);
        return false;
    }
    bool ok = false;
    const char* type = textOf(o, "type");
    const char* from = textOf(o, "from");
    const char* to = textOf(o, "to");
    int64_t n = 0;
    int64_t expires = 0;
    if (type && from && to && numberOf(o, "n", n) && n > 0 && numberOf(o, "expires", expires) && wantType == type &&
        wantFrom == from && wantTo == to && now <= expires && !seen_.count(static_cast<uint64_t>(n))) {
        out = Contents{};
        out.type = type;
        out.n = static_cast<uint64_t>(n);
        out.expires = expires;
        out.from = from;
        out.to = to;
        ok = true;
        if (wantType == "invite") {
            int64_t stream = 0;
            const char* game = textOf(o, "game");
            if (numberOf(o, "stream", stream) && game) {
                out.stream = static_cast<int>(stream);
                out.game = cleanText(game, kMaxGameName);
            } else {
                ok = false;
            }
        } else {
            const char* details = textOf(o, "details");
            if (details) out.details = details;
            else ok = false;
        }
        if (ok) seen_.insert(out.n);
    }
    json_object_put(o);
    return ok;
}

}  // namespace box
