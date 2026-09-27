#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_store.h"

using gosha::motion_live::MotionPackageCrc32;
using gosha::motion_live::MotionPackageLoadedRecord;
using gosha::motion_live::MotionPackageStore;
using gosha::motion_live::MotionPackageStoreBackend;
using gosha::motion_live::MotionPackageStoreEntry;
using gosha::motion_live::MotionPackageStoreRecord;
using gosha::motion_live::kMotionPackageStoreActiveKey;
using gosha::motion_live::kMotionPackageStoreSlotAKey;
using gosha::motion_live::kMotionPackageStoreSlotBKey;
using gosha::motion_live::kMotionPackageStoreSlotCKey;
using gosha::motion_live::kMotionPackageStoreSlotDKey;
using gosha::motion_live::kMotionPackageStoreCatalogKey;
using gosha::motion_live::kMotionPackageStoreMigrationKey;
using gosha::motion_live::kMotionPackageUploadMaxBytes;

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n"; \
            return 1;                                                                  \
        }                                                                              \
    } while (false)

namespace {

constexpr const char* kCalibration =
    "323456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0";

class FakeBackend : public MotionPackageStoreBackend {
public:
    bool Read(const char* key, std::vector<uint8_t>* value) override {
        auto it = values.find(key);
        if (it == values.end()) {
            return false;
        }
        if (value != nullptr) {
            *value = it->second;
        }
        return true;
    }

    bool Write(const char* key, const std::vector<uint8_t>& value) override {
        if (fail_writes.count(key) != 0) {
            return false;
        }
        values[key] = value;
        return true;
    }

    bool Erase(const char* key) override {
        if (fail_erases.count(key) != 0) {
            return false;
        }
        values.erase(key);
        return true;
    }

    std::map<std::string, std::vector<uint8_t>> values;
    std::set<std::string> fail_writes;
    std::set<std::string> fail_erases;
};

std::vector<uint8_t> PayloadFor(const std::string& name) {
    const std::string json =
        "{\"package_type\":\"gosha.motion.robot-package-draft.v1\",\"name\":\"" +
        name + "\"}";
    return std::vector<uint8_t>(json.begin(), json.end());
}

MotionPackageStoreRecord RecordFor(const char* id,
                                   const std::vector<uint8_t>& payload) {
    return {
        id,
        "gosha-preview-v1",
        kCalibration,
        MotionPackageCrc32(payload.data(), payload.size()),
        payload.data(),
        payload.size(),
    };
}

bool CodeIs(const char* actual, const char* expected) {
    return std::string(actual) == expected;
}

}  // namespace

int main() {
    const auto one = PayloadFor("one");
    const auto two = PayloadFor("two");
    const auto three = PayloadFor("three");
    const auto updated = PayloadFor("one-updated");
    const auto four = PayloadFor("four");

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        CHECK(store.Save(RecordFor("motion-001", one)).ok);
        CHECK(store.Save(RecordFor("motion-002", two)).ok);
        CHECK(store.Save(RecordFor("motion-003", three)).ok);
        std::vector<MotionPackageStoreEntry> entries;
        CHECK(store.List(&entries).ok && entries.size() == 3);
        CHECK(entries[0].record.package_id == "motion-002");
        CHECK(entries[1].record.package_id == "motion-001");
        CHECK(entries[2].record.package_id == "motion-003");
        CHECK(entries[2].active);
        const auto before = backend.values;
        auto result = store.Save(RecordFor("motion-004", four));
        CHECK(!result.ok && CodeIs(result.code, "store_full"));
        CHECK(backend.values == before);

        CHECK(store.Save(RecordFor("motion-001", updated)).ok);
        CHECK(store.List(&entries).ok && entries.size() == 3);
        MotionPackageLoadedRecord loaded;
        CHECK(store.LoadById("motion-001", &loaded).ok && loaded.payload == updated);
        CHECK(store.LoadById("motion-002", &loaded).ok && loaded.payload == two);
        CHECK(store.LoadById("motion-003", &loaded).ok && loaded.payload == three);
        CHECK(store.Load(&loaded).ok && loaded.package_id == "motion-001");

        CHECK(store.Select("motion-002").ok);
        CHECK(store.Load(&loaded).ok && loaded.package_id == "motion-002");
        CHECK(store.DeleteById("motion-003").ok);
        CHECK(store.List(&entries).ok && entries.size() == 2);
        CHECK(store.Save(RecordFor("motion-004", four)).ok);
        CHECK(store.List(&entries).ok && entries.size() == 3);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        CHECK(store.Save(RecordFor("motion-001", one)).ok);
        CHECK(store.Save(RecordFor("motion-002", two)).ok);
        // The installed firmware has only A/B and motion_pkg_act.
        backend.values.erase(kMotionPackageStoreCatalogKey);
        backend.values[kMotionPackageStoreActiveKey] = {'A'};
        backend.values[kMotionPackageStoreMigrationKey] = {'M', 'I', 'G', 1, 3, 0};
        backend.values[kMotionPackageStoreSlotCKey] = {0, 1, 2};
        std::vector<MotionPackageStoreEntry> before;
        CHECK(store.List(&before).ok && before.size() == 2);
        CHECK(store.Save(RecordFor("motion-003", three)).ok);
        MotionPackageLoadedRecord loaded;
        CHECK(store.LoadById("motion-001", &loaded).ok && loaded.payload == one);
        CHECK(store.LoadById("motion-002", &loaded).ok && loaded.payload == two);
        CHECK(store.LoadById("motion-003", &loaded).ok && loaded.payload == three);
        CHECK(backend.values.count(kMotionPackageStoreSlotCKey) == 1);
        CHECK(backend.values.count(kMotionPackageStoreMigrationKey) == 0);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        // Interrupted first migration with no previously installed package.
        backend.values[kMotionPackageStoreMigrationKey] = {'M', 'I', 'G', 1, 0, 0xff};
        backend.values[kMotionPackageStoreSlotBKey] = {0, 1, 2};
        std::vector<MotionPackageStoreEntry> entries;
        CHECK(store.List(&entries).ok && entries.empty());
        CHECK(store.Save(RecordFor("motion-001", one)).ok);
        CHECK(store.List(&entries).ok && entries.size() == 1);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        CHECK(store.Save(RecordFor("motion-001", one)).ok);
        CHECK(store.Save(RecordFor("motion-002", two)).ok);
        CHECK(store.Save(RecordFor("motion-003", three)).ok);
        backend.fail_writes.insert(kMotionPackageStoreSlotDKey);
        auto result = store.Save(RecordFor("motion-001", updated));
        CHECK(!result.ok && CodeIs(result.code, "store_slot_write_failed"));
        MotionPackageLoadedRecord loaded;
        CHECK(store.LoadById("motion-001", &loaded).ok && loaded.payload == one);
        backend.fail_writes.clear();
        backend.fail_writes.insert(kMotionPackageStoreCatalogKey);
        result = store.Save(RecordFor("motion-001", updated));
        CHECK(!result.ok && CodeIs(result.code, "store_commit_failed"));
        CHECK(backend.values.count(kMotionPackageStoreSlotDKey) == 0);
        CHECK(store.LoadById("motion-001", &loaded).ok && loaded.payload == one);
        CHECK(store.LoadById("motion-002", &loaded).ok && loaded.payload == two);
        CHECK(store.LoadById("motion-003", &loaded).ok && loaded.payload == three);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        CHECK(store.Save(RecordFor("motion-001", one)).ok);
        CHECK(store.Save(RecordFor("motion-002", two)).ok);
        CHECK(store.Save(RecordFor("motion-003", three)).ok);
        // Simulate a power cut after writing the staging blob.
        backend.values[kMotionPackageStoreSlotDKey] = {0, 1, 2};
        CHECK(store.Save(RecordFor("motion-001", updated)).ok);
        MotionPackageLoadedRecord loaded;
        CHECK(store.LoadById("motion-001", &loaded).ok && loaded.payload == updated);
        CHECK(store.LoadById("motion-002", &loaded).ok && loaded.payload == two);
        CHECK(store.LoadById("motion-003", &loaded).ok && loaded.payload == three);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        CHECK(store.Save(RecordFor("motion-001", one)).ok);
        backend.values[kMotionPackageStoreCatalogKey][4] = 0xff;
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok && CodeIs(result.code, "store_catalog_corrupt"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        CHECK(store.Save(RecordFor("motion-001", one)).ok);
        CHECK(store.Save(RecordFor("motion-002", two)).ok);
        CHECK(store.Save(RecordFor("motion-003", three)).ok);
        backend.values.erase(kMotionPackageStoreCatalogKey);
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok && CodeIs(result.code, "store_catalog_missing"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        backend.values[kMotionPackageStoreMigrationKey] = {'B', 'A', 'D', 1, 0, 0xff};
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok && CodeIs(result.code, "store_migration_corrupt"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        auto invalid = RecordFor(".", one);
        CHECK(!store.Save(invalid).ok);
        invalid = RecordFor("motion-001", one);
        invalid.payload_crc32 ^= 1u;
        CHECK(!store.Save(invalid).ok);
        std::vector<uint8_t> huge(kMotionPackageUploadMaxBytes + 1, 'x');
        CHECK(!store.Save(RecordFor("huge", huge)).ok);
    }

    std::cout << "motion_package_store_host_test: PASS\n";
    return 0;
}
