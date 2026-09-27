#ifndef GOSHA_V1_MOTION_PACKAGE_STORE_H_
#define GOSHA_V1_MOTION_PACKAGE_STORE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_package_upload.h"

namespace gosha::motion_live {

constexpr char kMotionPackageStoreSlotAKey[] = "motion_pkg_a";
constexpr char kMotionPackageStoreSlotBKey[] = "motion_pkg_b";
constexpr char kMotionPackageStoreSlotCKey[] = "motion_pkg_c";
constexpr char kMotionPackageStoreSlotDKey[] = "motion_pkg_d";
constexpr char kMotionPackageStoreActiveKey[] = "motion_pkg_act";
constexpr char kMotionPackageStoreCatalogKey[] = "motion_pkg_idx";
constexpr char kMotionPackageStoreMigrationKey[] = "motion_pkg_mig";
static_assert(sizeof(kMotionPackageStoreSlotAKey) <= 16,
              "NVS key names must fit in 15 bytes plus terminator");
static_assert(sizeof(kMotionPackageStoreSlotBKey) <= 16,
              "NVS key names must fit in 15 bytes plus terminator");
static_assert(sizeof(kMotionPackageStoreSlotCKey) <= 16,
              "NVS key names must fit in 15 bytes plus terminator");
static_assert(sizeof(kMotionPackageStoreSlotDKey) <= 16,
              "NVS key names must fit in 15 bytes plus terminator");
static_assert(sizeof(kMotionPackageStoreActiveKey) <= 16,
              "NVS key names must fit in 15 bytes plus terminator");
static_assert(sizeof(kMotionPackageStoreCatalogKey) <= 16,
              "NVS key names must fit in 15 bytes plus terminator");
static_assert(sizeof(kMotionPackageStoreMigrationKey) <= 16,
              "NVS key names must fit in 15 bytes plus terminator");
constexpr size_t kMotionPackageStoreLibraryLimit = 3;
constexpr size_t kMotionPackageStoreMaxRecordBytes =
    kMotionPackageUploadMaxBytes + 256;

struct MotionPackageStoreRecord {
    const char* package_id = "";
    const char* profile_id = "";
    const char* calibration_id = "";
    uint32_t payload_crc32 = 0;
    const uint8_t* payload = nullptr;
    size_t payload_size = 0;
};

struct MotionPackageLoadedRecord {
    std::string package_id;
    std::string profile_id;
    std::string calibration_id;
    uint32_t payload_crc32 = 0;
    std::vector<uint8_t> payload;
};

struct MotionPackageStoreEntry {
    MotionPackageLoadedRecord record;
    bool active = false;
};

struct MotionPackageStoreResult {
    bool ok = false;
    const char* code = "store_invalid";
};

class MotionPackageStoreBackend {
public:
    virtual ~MotionPackageStoreBackend() = default;
    virtual bool Read(const char* key, std::vector<uint8_t>* value) = 0;
    virtual bool Write(const char* key, const std::vector<uint8_t>& value) = 0;
    // Erase is idempotent: a missing key is considered successfully erased.
    virtual bool Erase(const char* key) = 0;
};

class MotionPackageStore {
public:
    explicit MotionPackageStore(MotionPackageStoreBackend* backend)
        : backend_(backend) {}

    MotionPackageStoreResult Save(const MotionPackageStoreRecord& record);
    MotionPackageStoreResult List(std::vector<MotionPackageStoreEntry>* entries);
    MotionPackageStoreResult Load(MotionPackageLoadedRecord* record);
    MotionPackageStoreResult LoadById(const char* package_id,
                                      MotionPackageLoadedRecord* record);
    MotionPackageStoreResult Select(const char* package_id);
    MotionPackageStoreResult Delete();
    MotionPackageStoreResult DeleteById(const char* package_id);

private:
    struct Catalog {
        uint8_t mask = 0;
        uint8_t active = 0xff;
        bool legacy = false;
    };

    MotionPackageStoreResult ReadCatalog(Catalog* catalog);
    bool WriteCatalog(const Catalog& catalog);
    bool SlotExists(const char* slot);
    MotionPackageStoreResult LoadSlot(const char* slot,
                                      MotionPackageLoadedRecord* record);
    MotionPackageStoreResult ValidateCatalog(const Catalog& catalog);

    MotionPackageStoreBackend* backend_ = nullptr;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_STORE_H_
