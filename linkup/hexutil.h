// Hex and base64, for keys and sealed boxes on their way in and out of JSON.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <openssl/evp.h>

namespace hexutil {

inline std::string encode(const uint8_t* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 15]);
    }
    return out;
}

inline int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

template <size_t N>
bool decode(const std::string& hex, std::array<uint8_t, N>& out) {
    if (hex.size() != N * 2) return false;
    for (size_t i = 0; i < N; ++i) {
        const int a = nibble(hex[i * 2]);
        const int b = nibble(hex[i * 2 + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = static_cast<uint8_t>(a * 16 + b);
    }
    return true;
}

inline std::string toBase64(const std::vector<uint8_t>& in) {
    std::string out(4 * ((in.size() + 2) / 3) + 1, '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), in.data(), static_cast<int>(in.size()));
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return out;
}

// False for anything that is not padded standard base64.
inline bool fromBase64(const std::string& in, std::vector<uint8_t>& out) {
    if (in.empty() || in.size() % 4 != 0) return false;
    out.resize(in.size() / 4 * 3);
    const int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size()));
    if (n < 0) return false;
    size_t len = static_cast<size_t>(n);
    if (in[in.size() - 1] == '=') --len;
    if (in[in.size() - 2] == '=') --len;
    out.resize(len);
    return true;
}

}  // namespace hexutil
