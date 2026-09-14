#ifndef GOSHA_V1_MOTION_PACKAGE_PLAYER_H_
#define GOSHA_V1_MOTION_PACKAGE_PLAYER_H_

#include <cstdint>
#include <string>

#include "motion_package_json.h"
#include "motion_package_store.h"

namespace gosha::motion_live {

struct MotionPackagePlayerResult {
    bool ok = false;
    const char* code = "package_player_invalid";
    int frame_index = -1;
    const char* joint_id = "";
};

struct MotionPackageSample {
    MotionLiveTarget target{};
    const char* package_id = "";
    uint32_t elapsed_ms = 0;
    bool finished = false;
};

class MotionPackagePlayer {
public:
    MotionPackagePlayerResult Load(const MotionLivePreparedProfile& profile,
                                   const MotionPackageLoadedRecord& record);
    MotionPackagePlayerResult Sample(uint32_t elapsed_ms,
                                     MotionPackageSample* sample) const;
    void Clear();

    bool loaded() const { return loaded_; }
    const std::string& package_id() const { return package_id_; }
    uint32_t duration_ms() const { return loaded_ ? draft_.draft.duration_ms : 0; }

private:
    bool FrameValue(const MotionPackageKeyframe& frame,
                    const char* joint_id,
                    double* value) const;

    MotionPackageOwnedDraft draft_;
    std::string package_id_;
    bool loaded_ = false;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_PLAYER_H_
