#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_manager.h"

using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionPackageCrc32;
using gosha::motion_live::MotionPackageLoadedRecord;
using gosha::motion_live::MotionPackageManager;
using gosha::motion_live::MotionPackageStoreEntry;
using gosha::motion_live::MotionPackageStoreBackend;
using gosha::motion_live::MotionPackageUploadBegin;
using gosha::motion_live::kMotionPackageStoreActiveKey;
using gosha::motion_live::kMotionPackageStoreSlotAKey;
using gosha::motion_live::kMotionPackageStoreSlotBKey;

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
        values.erase(key);
        return true;
    }

    std::map<std::string, std::vector<uint8_t>> values;
    std::set<std::string> fail_writes;
};

MotionLivePreparedProfile MotionEditorProfile() {
    return {
        "gosha-preview-v1",
        kCalibration,
        "dbcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        300,
        20,
        {{
            {"leg_negative_x", "left_leg", 0, 17, 0, 90, 1, -35.0, 35.0, 55, 125, 10.0},
            {"leg_positive_x", "right_leg", 1, 39, 0, 90, -1, -35.0, 35.0, 55, 125, 10.0},
            {"foot_negative_x", "left_foot", 2, 18, 0, 90, 1, -30.0, 30.0, 60, 120, 10.0},
            {"foot_positive_x", "right_foot", 3, 38, 0, 90, -1, -30.0, 30.0, 60, 120, 10.0},
            {"arm_positive_x", "right_hand", 5, 12, 0, 135, 1, -70.0, 45.0, 65, 180, 10.0},
        }},
        "motion_editor",
        5,
    };
}

std::vector<uint8_t> PayloadWithRightArm(const std::string& name, double right_arm) {
    const std::string json =
        "{\"schema_version\":1,"
        "\"package_type\":\"gosha.motion.robot-package-draft.v1\","
        "\"source_motion_id\":\"" + name + "\","
        "\"name\":\"" + name + "\","
        "\"profile_id\":\"gosha-preview-v1\","
        "\"profile_version\":1,"
        "\"calibration_id\":\"" + kCalibration + "\","
        "\"units\":\"relative_degrees\","
        "\"source_preview_only\":true,"
        "\"hardware_validated\":false,"
        "\"live_compatible\":true,"
        "\"robot_storage_implemented\":false,"
        "\"duration_ms\":8000,"
        "\"interpolation\":\"smooth\","
        "\"active_joints\":[\"arm_positive_x\",\"leg_negative_x\","
        "\"leg_positive_x\",\"foot_negative_x\",\"foot_positive_x\"],"
        "\"constraints\":["
        "{\"id\":\"arm_positive_x\",\"min\":-70,\"max\":45,\"max_speed_dps\":10},"
        "{\"id\":\"leg_negative_x\",\"min\":-35,\"max\":35,\"max_speed_dps\":10},"
        "{\"id\":\"leg_positive_x\",\"min\":-35,\"max\":35,\"max_speed_dps\":10},"
        "{\"id\":\"foot_negative_x\",\"min\":-30,\"max\":30,\"max_speed_dps\":10},"
        "{\"id\":\"foot_positive_x\",\"min\":-30,\"max\":30,\"max_speed_dps\":10}],"
        "\"keyframes\":["
        "{\"time_ms\":0,\"target\":{\"arm_positive_x\":0,\"leg_negative_x\":0,"
        "\"leg_positive_x\":0,\"foot_negative_x\":0,\"foot_positive_x\":0}},"
        "{\"time_ms\":8000,\"target\":{\"arm_positive_x\":" +
        std::to_string(right_arm) +
        ",\"leg_negative_x\":-12,\"leg_positive_x\":0,"
        "\"foot_negative_x\":0,\"foot_positive_x\":8}}]}";
    return std::vector<uint8_t>(json.begin(), json.end());
}

std::vector<uint8_t> PayloadFor(const std::string& name) {
    return PayloadWithRightArm(name, 40.0);
}

MotionPackageUploadBegin BeginFor(const char* id,
                                  const std::vector<uint8_t>& payload) {
    return {
        id,
        "gosha-preview-v1",
        kCalibration,
        static_cast<uint32_t>(payload.size()),
        MotionPackageCrc32(payload.data(), payload.size()),
    };
}

bool CodeIs(const char* actual, const char* expected) {
    return std::string(actual) == expected;
}

}  // namespace

int main() {
    MotionLivePreparedProfile profile = MotionEditorProfile();

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        auto result = manager.BeginUpload(profile, BeginFor("motion-001", payload));
        CHECK(result.ok);
        CHECK(manager.upload_active());
        CHECK(manager.upload_package_id() == "motion-001");
        CHECK(manager.upload_expected_size() == payload.size());
        result = manager.AppendUpload({0, payload.data(), 7});
        CHECK(result.ok);
        result = manager.AppendUpload({7, payload.data() + 7, payload.size() - 7});
        CHECK(result.ok);
        result = manager.FinishUpload();
        CHECK(result.ok);
        CHECK(!manager.upload_active());
        CHECK(backend.values[kMotionPackageStoreActiveKey] == std::vector<uint8_t>{'B'});
        MotionPackageLoadedRecord loaded;
        result = manager.Load(&loaded);
        CHECK(result.ok);
        CHECK(loaded.package_id == "motion-001");
        CHECK(loaded.calibration_id == kCalibration);
        CHECK(loaded.payload == payload);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(manager.BeginUpload(profile, BeginFor("motion-001", first)).ok);
        CHECK(manager.AppendUpload({0, first.data(), first.size()}).ok);
        CHECK(manager.FinishUpload().ok);
        backend.fail_writes.insert(kMotionPackageStoreActiveKey);
        auto result = manager.BeginUpload(profile, BeginFor("motion-002", second));
        CHECK(result.ok);
        result = manager.AppendUpload({0, second.data(), second.size()});
        CHECK(result.ok);
        result = manager.FinishUpload();
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_commit_failed"));
        CHECK(!manager.upload_active());
        MotionPackageLoadedRecord loaded;
        result = manager.Load(&loaded);
        CHECK(result.ok);
        CHECK(loaded.package_id == "motion-001");
        CHECK(loaded.payload == first);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> first = PayloadFor("first");
        const std::vector<uint8_t> second = PayloadFor("second");
        CHECK(manager.BeginUpload(profile, BeginFor("motion-001", first)).ok);
        CHECK(manager.AppendUpload({0, first.data(), first.size()}).ok);
        CHECK(manager.FinishUpload().ok);
        CHECK(manager.BeginUpload(profile, BeginFor("motion-002", second)).ok);
        CHECK(manager.AppendUpload({0, second.data(), second.size()}).ok);
        CHECK(manager.FinishUpload().ok);

        std::vector<MotionPackageStoreEntry> entries;
        auto result = manager.List(&entries);
        CHECK(result.ok);
        CHECK(entries.size() == 2);
        CHECK(entries[0].record.package_id == "motion-002");
        CHECK(entries[0].active);
        CHECK(entries[1].record.package_id == "motion-001");
        CHECK(!entries[1].active);

        MotionPackageLoadedRecord loaded;
        result = manager.LoadById("motion-001", &loaded);
        CHECK(result.ok);
        CHECK(loaded.payload == first);
        result = manager.Select("motion-001");
        CHECK(result.ok);
        result = manager.Load(&loaded);
        CHECK(result.ok);
        CHECK(loaded.package_id == "motion-001");
        result = manager.Select("motion-missing");
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_package_not_found"));
        result = manager.DeleteById("motion-002");
        CHECK(result.ok);
        result = manager.Load(&loaded);
        CHECK(result.ok);
        CHECK(loaded.package_id == "motion-001");
        result = manager.DeleteById("motion-001");
        CHECK(result.ok);
        result = manager.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(manager.BeginUpload(profile, BeginFor("motion-001", payload)).ok);
        CHECK(manager.AppendUpload({0, payload.data(), payload.size() - 1}).ok);
        auto result = manager.FinishUpload();
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_incomplete"));
        CHECK(manager.upload_active());
        MotionPackageLoadedRecord loaded;
        result = manager.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> payload = PayloadWithRightArm("bad-arm", 55.0);
        CHECK(manager.BeginUpload(profile, BeginFor("motion-001", payload)).ok);
        CHECK(manager.AppendUpload({0, payload.data(), payload.size()}).ok);
        auto result = manager.FinishUpload();
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "target_out_of_range"));
        MotionPackageLoadedRecord loaded;
        result = manager.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        MotionPackageUploadBegin begin = BeginFor("motion-001", payload);
        begin.crc32 ^= 1u;
        CHECK(manager.BeginUpload(profile, begin).ok);
        CHECK(manager.AppendUpload({0, payload.data(), payload.size()}).ok);
        auto result = manager.FinishUpload();
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_crc32"));
        CHECK(!manager.upload_active());
        MotionPackageLoadedRecord loaded;
        result = manager.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_empty"));
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(manager.BeginUpload(profile, BeginFor("motion-001", payload)).ok);
        auto result = manager.Delete();
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "package_upload_active"));
        manager.AbortUpload();
        result = manager.Delete();
        CHECK(result.ok);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        const std::vector<uint8_t> payload = PayloadFor("first");
        CHECK(manager.BeginUpload(profile, BeginFor("motion-001", payload)).ok);
        CHECK(manager.AppendUpload({0, payload.data(), payload.size()}).ok);
        CHECK(manager.FinishUpload().ok);
        backend.values.erase(kMotionPackageStoreActiveKey);
        MotionPackageLoadedRecord loaded;
        auto result = manager.Load(&loaded);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_active_missing"));
        result = manager.BeginUpload(profile, BeginFor("motion-002", payload));
        CHECK(result.ok);
        CHECK(manager.AppendUpload({0, payload.data(), payload.size()}).ok);
        result = manager.FinishUpload();
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "store_active_missing"));
    }

    std::cout << "motion_package_manager_host_test: PASS\n";
    return 0;
}
