#ifndef GOSHA_V1_MOTION_LIVE_AUTH_H_
#define GOSHA_V1_MOTION_LIVE_AUTH_H_

namespace gosha::motion_live {

inline bool IsSha256Hex(const char* value) {
    if (value == nullptr) return false;
    for (int i = 0; i < 64; ++i) {
        const char c = value[i];
        // Stop at a short string's terminator before reading any later byte.
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return value[64] == '\0';
}

inline bool ConstantTimeEquals64(const char* left, const char* right) {
    if (!IsSha256Hex(left) || !IsSha256Hex(right)) return false;
    unsigned char diff = 0;
    for (int i = 0; i < 64; ++i) {
        diff |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return diff == 0;
}

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_LIVE_AUTH_H_
