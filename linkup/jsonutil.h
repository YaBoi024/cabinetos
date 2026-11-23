// A few words over json-c, so the rest reads as what it asks and not how.

#pragma once

#include <cstdint>
#include <string>

#include <json-c/json.h>

namespace jsonutil {

// Parses all of `text`, or null. The caller puts the object.
inline json_object* parse(const std::string& text) {
    json_tokener* tok = json_tokener_new();
    json_object* o = json_tokener_parse_ex(tok, text.c_str(), static_cast<int>(text.size()));
    const bool good = json_tokener_get_error(tok) == json_tokener_success;
    json_tokener_free(tok);
    if (!good) {
        if (o) json_object_put(o);
        return nullptr;
    }
    return o;
}

inline std::string str(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_string)) return {};
    return json_object_get_string(v);
}

inline int64_t num(json_object* o, const char* key, int64_t fallback = 0) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !json_object_is_type(v, json_type_int)) return fallback;
    return json_object_get_int64(v);
}

inline bool flag(json_object* o, const char* key) {
    json_object* v = nullptr;
    return o && json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_boolean) &&
           json_object_get_boolean(v);
}

inline json_object* get(json_object* o, const char* key) {
    json_object* v = nullptr;
    return o && json_object_object_get_ex(o, key, &v) ? v : nullptr;
}

inline std::string text(json_object* o) { return json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN); }

inline void set(json_object* o, const char* key, const std::string& value) {
    json_object_object_add(o, key, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
}
inline void set(json_object* o, const char* key, const char* value) { set(o, key, std::string(value)); }
inline void set(json_object* o, const char* key, int64_t value) { json_object_object_add(o, key, json_object_new_int64(value)); }
inline void set(json_object* o, const char* key, int value) { json_object_object_add(o, key, json_object_new_int64(value)); }
inline void set(json_object* o, const char* key, bool value) { json_object_object_add(o, key, json_object_new_boolean(value)); }

}  // namespace jsonutil
