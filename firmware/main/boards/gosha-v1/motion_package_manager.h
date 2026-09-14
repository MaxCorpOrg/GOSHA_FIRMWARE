#ifndef GOSHA_V1_MOTION_PACKAGE_MANAGER_H_
#define GOSHA_V1_MOTION_PACKAGE_MANAGER_H_

#include <cstdint>
#include <vector>

#include "motion_package_store.h"
#include "motion_package_upload.h"

namespace gosha::motion_live {

struct MotionPackageManagerResult {
    bool ok = false;
    const char* code = "package_invalid";
    uint32_t expected_offset = 0;
    uint32_t value = 0;
    uint32_t limit = 0;
};

class MotionPackageManager {
public:
    explicit MotionPackageManager(MotionPackageStoreBackend* backend)
        : store_(backend) {}

    MotionPackageManagerResult BeginUpload(
        const MotionLivePreparedProfile& profile,
        const MotionPackageUploadBegin& begin);
    MotionPackageManagerResult AppendUpload(const MotionPackageUploadChunk& chunk);
    MotionPackageManagerResult FinishUpload();
    void AbortUpload();

    MotionPackageManagerResult Load(MotionPackageLoadedRecord* record);
    MotionPackageManagerResult LoadById(const char* package_id,
                                        MotionPackageLoadedRecord* record);
    MotionPackageManagerResult List(std::vector<MotionPackageStoreEntry>* entries);
    MotionPackageManagerResult Select(const char* package_id);
    MotionPackageManagerResult Delete();
    MotionPackageManagerResult DeleteById(const char* package_id);

    bool upload_active() const { return upload_.active(); }
    const std::string& upload_package_id() const { return upload_.package_id(); }
    uint32_t upload_expected_size() const { return upload_.expected_size(); }
    uint32_t upload_received_size() const { return upload_.received_size(); }

private:
    MotionPackageUploadSession upload_;
    MotionPackageStore store_;
    MotionLivePreparedProfile upload_profile_{};
    bool upload_profile_ready_ = false;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_MANAGER_H_
