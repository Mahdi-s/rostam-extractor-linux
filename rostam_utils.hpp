#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace TsConstants {
constexpr int TS_PACKET_SIZE = 188;
constexpr uint8_t TS_SYNC_BYTE = 0x47;
constexpr int DEFAULT_PID = 6530;
inline const std::vector<uint8_t> ROSTAM_MAGIC = {0xCA, 0xFE, 0xC0, 0xDE, 0xF0, 0x0D};
inline const std::vector<uint8_t> PES_START_CODE = {0x00, 0x00, 0x01};
constexpr uint8_t STREAM_ID_PRIVATE_1 = 0xBD;
constexpr int HEADER_DATA_SIZE = 18;
}  // namespace TsConstants

inline uint64_t decodeLittleEndian64(const uint8_t* buffer) {
    return (uint64_t)buffer[0] |
           ((uint64_t)buffer[1] << 8) |
           ((uint64_t)buffer[2] << 16) |
           ((uint64_t)buffer[3] << 24) |
           ((uint64_t)buffer[4] << 32) |
           ((uint64_t)buffer[5] << 40) |
           ((uint64_t)buffer[6] << 48) |
           ((uint64_t)buffer[7] << 56);
}

inline std::string sanitizeFilename(const std::string& name) {
    std::string sanitized;
    sanitized.reserve(name.size());

    for (char ch : name) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (value < 32 || ch == '\\' || ch == '/' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            sanitized.push_back('_');
        } else {
            sanitized.push_back(ch);
        }
    }

    if (sanitized.empty()) {
        return "unnamed_file_" + std::to_string(time(nullptr));
    }

    return sanitized;
}

inline std::string decodeFilename(const std::vector<uint8_t>& bytes) {
    const auto null_pos = std::find(bytes.begin(), bytes.end(), static_cast<uint8_t>(0x00));
    const std::vector<uint8_t> filename_bytes(bytes.begin(), null_pos);

    std::string decoded;
    if (filename_bytes.size() >= 2 && filename_bytes[0] == 0xFF && filename_bytes[1] == 0xFE) {
        decoded.reserve((filename_bytes.size() - 2 + 1) / 2);
        for (std::size_t i = 2; i < filename_bytes.size(); i += 2) {
            decoded.push_back(static_cast<char>(filename_bytes[i]));
        }
    } else {
        decoded.assign(filename_bytes.begin(), filename_bytes.end());
    }

    return sanitizeFilename(decoded);
}
