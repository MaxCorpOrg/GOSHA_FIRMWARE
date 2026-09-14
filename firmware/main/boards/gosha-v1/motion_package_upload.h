#ifndef GOSHA_V1_MOTION_PACKAGE_UPLOAD_H_
#define GOSHA_V1_MOTION_PACKAGE_UPLOAD_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_live_core.h"

namespace gosha::motion_live {

constexpr size_t kMotionPackageUploadMaxBytes = 12 * 1024;
constexpr size_t kMotionPackageUploadMaxChunkBytes = 2048;
constexpr size_t kMotionPackageUploadMaxIdBytes = 64;

struct MotionPackageUploadBegin {
    const char* package_id = "";
    const char* profile_id = "";
    const char* calibration_id = "";
    uint32_t total_size = 0;
    uint32_t crc32 = 0;
};

struct MotionPackageUploadChunk {
    uint32_t offset = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct MotionPackageUploadResult {
    bool ok = false;
    const char* code = "upload_invalid";
    uint32_t expected_offset = 0;
    uint32_t value = 0;
    uint32_t limit = 0;
};

uint32_t MotionPackageCrc32(const uint8_t* data, size_t size);

class MotionPackageUploadSession {
public:
    MotionPackageUploadResult Begin(const MotionLivePreparedProfile& profile,
                                    const MotionPackageUploadBegin& begin);
    MotionPackageUploadResult Append(const MotionPackageUploadChunk& chunk);
    MotionPackageUploadResult Finish(std::vector<uint8_t>* payload);
    void Abort();

    bool active() const { return active_; }
    const std::string& package_id() const { return package_id_; }
    const std::string& calibration_id() const { return calibration_id_; }
    uint32_t expected_crc32() const { return expected_crc32_; }
    uint32_t expected_size() const { return expected_size_; }
    uint32_t received_size() const {
        return static_cast<uint32_t>(buffer_.size());
    }

private:
    bool active_ = false;
    std::string package_id_;
    std::string calibration_id_;
    uint32_t expected_size_ = 0;
    uint32_t expected_crc32_ = 0;
    std::vector<uint8_t> buffer_;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_UPLOAD_H_
