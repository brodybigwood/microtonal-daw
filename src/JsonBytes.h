#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

/**
 * Raw byte blobs (VST plugin states) stored in JSON as base64 strings.
 * One string token parses ~9x faster than the legacy int-per-byte array and
 * is ~5x smaller in a pretty-printed dump. jsonBytesDecode accepts both
 * encodings so saves written before the switch still load.
 */

inline nlohmann::json jsonBytesEncode(const std::vector<uint8_t>& bytes) {
    static const char* tab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out((bytes.size() + 2) / 3 * 4, '=');
    size_t o = 0;
    for (size_t i = 0; i < bytes.size(); i += 3, o += 4) {
        uint32_t v = static_cast<uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= static_cast<uint32_t>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) v |= bytes[i + 2];
        out[o] = tab[v >> 18];
        out[o + 1] = tab[(v >> 12) & 63];
        if (i + 1 < bytes.size()) out[o + 2] = tab[(v >> 6) & 63];
        if (i + 2 < bytes.size()) out[o + 3] = tab[v & 63];
    }
    return out;
}

inline std::vector<uint8_t> jsonBytesDecode(const nlohmann::json& j) {
    if (j.is_array())   // legacy int-per-byte encoding
        return std::vector<uint8_t>(j.begin(), j.end());
    if (!j.is_string()) return {};
    const std::string& s = j.get_ref<const std::string&>();
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(s.size() / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v = val(c);
        if (v < 0) continue;   // '=' padding / whitespace
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}
