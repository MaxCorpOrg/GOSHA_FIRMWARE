#ifndef GOSHA_V1_MOTION_PACKAGE_NVS_STORE_H_
#define GOSHA_V1_MOTION_PACKAGE_NVS_STORE_H_

#include "motion_package_store.h"

namespace gosha::motion_live {

constexpr char kMotionPackageNvsNamespace[] = "motion_pkg";
static_assert(sizeof(kMotionPackageNvsNamespace) <= 16,
              "NVS namespace must fit in 15 bytes plus terminator");

class MotionPackageNvsStoreBackend final : public MotionPackageStoreBackend {
public:
    bool Read(const char* key, std::vector<uint8_t>* value) override;
    bool Write(const char* key, const std::vector<uint8_t>& value) override;
    bool Erase(const char* key) override;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_NVS_STORE_H_
