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
    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        auto result = store.Save(RecordFor("motion-001", payload));
        CHECK(result.ok);
        CHECK(backend.values.count(kMotionPackageStoreSlotBKey) == 1);
        CHECK(backend.values[kMotionPackageStoreActiveKey] == std::vector<uint8_t>{'B'});
        MotionPackageLoadedRecord loaded;
        result = store.Load(&loaded);
        CHECK(result.ok);
        CHECK(loaded.package_id == "motion-001");
        CHECK(loaded.profile_id == "gosha-preview-v1");
        CHECK(loaded.calibration_id == kCalibration);
        CHECK(loaded.payload == payload);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        CHECK(store.Save(RecordFor("motion-002", second)).ok);
        CHECK(backend.values[kMotionPackageStoreActiveKey] == std::vector<uint8_t>{'A'});
        std::vector<MotionPackageStoreEntry> entries;
        CHECK(store.List(&entries).ok);
        CHECK(entries.size() == 2);
        CHECK(entries[0].record.package_id == "motion-002");
        CHECK(entries[0].active);
        CHECK(entries[1].record.package_id == "motion-001");
        CHECK(!entries[1].active);
        MotionPackageLoadedRecord loaded;
        CHECK(store.Load(&loaded).ok);
        CHECK(loaded.package_id == "motion-002");
        CHECK(loaded.payload == second);
        CHECK(store.LoadById("motion-001", &loaded).ok);
        CHECK(loaded.package_id == "motion-001");
        CHECK(loaded.payload == first);
        CHECK(store.Select("motion-001").ok);
        CHECK(store.Load(&loaded).ok);
        CHECK(loaded.package_id == "motion-001");
        auto select_result = store.Select("missing-package");
        CHECK(!select_result.ok);
        CHECK(CodeIs(select_result.code, "store_package_not_found"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        const std::vector<uint8_t> replacement = PayloadFor("first-replaced");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        CHECK(store.Save(RecordFor("motion-002", second)).ok);
        CHECK(store.Save(RecordFor("motion-001", replacement)).ok);
        std::vector<MotionPackageStoreEntry> entries;
        CHECK(store.List(&entries).ok);
        CHECK(entries.size() == 2);
        CHECK(entries[0].record.package_id == "motion-002");
        CHECK(!entries[0].active);
        CHECK(entries[0].record.payload == second);
        CHECK(entries[1].record.package_id == "motion-001");
        CHECK(entries[1].active);
        CHECK(entries[1].record.payload == replacement);
        MotionPackageLoadedRecord loaded;
        CHECK(store.Load(&loaded).ok);
        CHECK(loaded.package_id == "motion-001");
        CHECK(loaded.payload == replacement);
        CHECK(store.LoadById("motion-002", &loaded).ok);
        CHECK(loaded.payload == second);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        CHECK(store.Save(RecordFor("motion-002", second)).ok);
        CHECK(store.DeleteById("motion-001").ok);
        std::vector<MotionPackageStoreEntry> entries;
        CHECK(store.List(&entries).ok);
        CHECK(entries.size() == 1);
        CHECK(entries[0].record.package_id == "motion-002");
        CHECK(entries[0].active);
        MotionPackageLoadedRecord loaded;
        CHECK(store.Load(&loaded).ok);
        CHECK(loaded.package_id == "motion-002");
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        CHECK(store.Save(RecordFor("motion-002", second)).ok);
        CHECK(store.DeleteById("motion-002").ok);
        std::vector<MotionPackageStoreEntry> entries;
        CHECK(store.List(&entries).ok);
        CHECK(entries.size() == 1);
        CHECK(entries[0].record.package_id == "motion-001");
        CHECK(entries[0].active);
        MotionPackageLoadedRecord loaded;
        CHECK(store.Load(&loaded).ok);
        CHECK(loaded.package_id == "motion-001");
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(store.Save(RecordFor("motion-001", payload)).ok);
        CHECK(store.DeleteById("motion-001").ok);
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(store.Save(RecordFor("motion-001", payload)).ok);
        auto result = store.DeleteById("missing-package");
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_package_not_found"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        backend.fail_writes.insert(kMotionPackageStoreSlotAKey);
        auto result = store.Save(RecordFor("motion-002", second));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_slot_write_failed"));
        MotionPackageLoadedRecord loaded;
        CHECK(store.Load(&loaded).ok);
        CHECK(loaded.package_id == "motion-001");
        CHECK(loaded.payload == first);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        backend.fail_writes.insert(kMotionPackageStoreActiveKey);
        auto result = store.Save(RecordFor("motion-002", second));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_commit_failed"));
        CHECK(backend.values.count(kMotionPackageStoreSlotAKey) == 0);
        MotionPackageLoadedRecord loaded;
        CHECK(store.Load(&loaded).ok);
        CHECK(loaded.package_id == "motion-001");
        CHECK(loaded.payload == first);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("bad");
        MotionPackageStoreRecord record = RecordFor(".", payload);
        auto result = store.Save(record);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_record_invalid"));
        record = RecordFor("motion-001", payload);
        record.payload_crc32 ^= 1u;
        result = store.Save(record);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_record_invalid"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        std::vector<uint8_t> payload(kMotionPackageUploadMaxBytes + 1, 'x');
        auto result = store.Save(RecordFor("motion-oversize", payload));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_record_invalid"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(store.Save(RecordFor("motion-001", payload)).ok);
        backend.values[kMotionPackageStoreSlotBKey].back() ^= 0x01u;
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_corrupt"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        backend.values[kMotionPackageStoreActiveKey] = std::vector<uint8_t>{'X'};
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_active_corrupt"));
        result = store.Save(RecordFor("motion-002", second));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_active_corrupt"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        const std::vector<uint8_t> third = PayloadFor("third");
        const std::vector<uint8_t> fourth = PayloadFor("fourth");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        CHECK(store.Save(RecordFor("motion-002", second)).ok);
        CHECK(store.Save(RecordFor("motion-003", third)).ok);
        CHECK(backend.values[kMotionPackageStoreActiveKey] == std::vector<uint8_t>{'B'});
        std::vector<MotionPackageStoreEntry> entries;
        CHECK(store.List(&entries).ok);
        CHECK(entries.size() == 2);
        CHECK(entries[0].record.package_id == "motion-002");
        CHECK(!entries[0].active);
        CHECK(entries[1].record.package_id == "motion-003");
        CHECK(entries[1].active);
        backend.values.erase(kMotionPackageStoreActiveKey);
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_active_missing"));
        result = store.Save(RecordFor("motion-004", fourth));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_active_missing"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        backend.values[kMotionPackageStoreSlotAKey] = payload;
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_active_missing"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        CHECK(store.Delete().ok);
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(store.Save(RecordFor("motion-001", payload)).ok);
        CHECK(store.Delete().ok);
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        CHECK(store.Save(RecordFor("motion-001", first)).ok);
        backend.values.erase(kMotionPackageStoreActiveKey);
        CHECK(store.Delete().ok);
        MotionPackageLoadedRecord loaded;
        auto result = store.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(store.Save(RecordFor("motion-001", payload)).ok);
        backend.fail_erases.insert(kMotionPackageStoreSlotBKey);
        auto result = store.Delete();
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_delete_failed"));
    }

    std::cout << "motion_package_store_host_test: PASS\n";
    return 0;
}
