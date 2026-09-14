#define main RunExistingRunnerTests
#include "motion_package_hardware_runner_host_test.cc"
#undef main
#include <map>
#include "../main/boards/gosha-v1/robot_motion_runtime.h"

using namespace gosha::motion_live;

class MemoryStore : public MotionPackageStoreBackend {
public:
    std::map<std::string, std::vector<uint8_t>> values;
    int writes = 0;
    bool Read(const char* key, std::vector<uint8_t>* data) override {
        const auto found = values.find(key);
        if (found == values.end()) return false;
        *data = found->second; return true;
    }
    bool Write(const char* key, const std::vector<uint8_t>& data) override {
        ++writes; values[key] = data; return true;
    }
    bool Erase(const char* key) override { ++writes; values.erase(key); return true; }
};

bool ReplyIs(cJSON* reply, const char* field, const char* expected) {
    const auto* value = cJSON_GetObjectItem(reply, field);
    const bool ok = cJSON_IsString(value) && std::string(value->valuestring) == expected;
    if (!ok) { char* json = cJSON_PrintUnformatted(reply); std::cerr << (json ? json : "null") << "\n"; cJSON_free(json); }
    cJSON_Delete(reply); return ok;
}

int main() {
    CHECK(RunExistingRunnerTests() == 0);
    auto profile = MotionEditorProfile();
    MemoryStore backend;
    MotionPackageStore store(&backend);
    auto record = RecordFor(Payload());
    CHECK(store.Save({record.package_id.c_str(), record.profile_id.c_str(), record.calibration_id.c_str(),
                      record.payload_crc32, record.payload.data(), record.payload.size()}).ok);
    const auto before_store = backend.values;
    const int writes = backend.writes;
    MotionPackageManager manager(&backend);
    RobotMotionRuntime robot(&manager);
    MotionLiveCore core;
    core.SetLocalOptInEnabled(true); core.SetPreparedProfile(&profile); core.SetRuntimeConfig(RuntimeConfig());
    int initialized = 0, applied = 0, min_right = 135;
    core.SetRightArmInitializer([&](int degrees) { ++initialized; return degrees == 135; });
    core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& degrees) {
        ++applied; min_right = std::min(min_right, degrees[5]); return true;
    });
    cJSON* listed = robot.List(&profile);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(listed, "movements")) == 3);
    cJSON_Delete(listed);
    CHECK(initialized == 0 && applied == 0 && writes == backend.writes);
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "missing", "request-invalid0001", false, 0), "reason", "movement_unavailable"));
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/hand_wave", "request-editor0001", true, 0), "reason", "editor_busy"));
    CHECK(initialized == 0 && applied == 0);
    // Failed hardware initialization stays latched by the existing core.
    // Neither redelivery nor another request may bypass that hardware fault.
    {
        RobotMotionRuntime failed_robot(&manager);
        MotionLiveCore failed_core;
        failed_core.SetLocalOptInEnabled(true); failed_core.SetPreparedProfile(&profile);
        failed_core.SetRuntimeConfig(RuntimeConfig());
        int attempts = 0;
        failed_core.SetRightArmInitializer([&](int) { ++attempts; return false; });
        CHECK(ReplyIs(failed_robot.Play(&profile, &failed_core, 7, "builtin/hand_wave", "request-initfail01", false, 0), "status", "failed"));
        CHECK(ReplyIs(failed_robot.Play(&profile, &failed_core, 7, "builtin/hand_wave", "request-initfail01", false, 0), "status", "failed"));
        CHECK(ReplyIs(failed_robot.Play(&profile, &failed_core, 7, "builtin/hand_wave", "request-initfail02", false, 0), "status", "failed"));
        CHECK(attempts == 1 && !failed_robot.running() && !failed_core.IsArmed());
    }
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/hand_wave", "request-wave000001", false, 1000), "status", "in_progress"));
    CHECK(initialized == 1);
    CHECK(ReplyIs(robot.List(&profile), "reason", "movement_busy"));
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/hand_wave", "request-wave000001", false, 1000), "status", "in_progress"));
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/greeting", "request-wave000001", false, 1000), "reason", "request_id_conflict"));
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/greeting", "request-wave000002", false, 1000), "reason", "movement_busy"));
    for (uint64_t t = 1000; t <= 7100; t += 50) { robot.Tick(&core, t); core.Tick(t); }
    CHECK(ReplyIs(robot.Status(), "status", "finished"));
    CHECK(!robot.running() && !core.IsArmed() && initialized == 1 && applied > 0 && min_right == 120);
    for (const auto degrees : core.CommandedPose().relative_degrees) CHECK(degrees == 0);
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/hand_wave", "request-wave000001", false, 7200), "status", "finished"));
    CHECK(!robot.running());
    // Stored package is loaded by ID; runtime never changes the Studio active pointer.
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "stored/" + record.package_id, "request-store00001", false, 8000), "status", "in_progress"));
    for (uint64_t t = 8000; t <= 16100; t += 50) { robot.Tick(&core, t); core.Tick(t); }
    CHECK(ReplyIs(robot.Status(), "status", "finished"));
    CHECK(core.CommandedPose().relative_degrees[1] > 0);
    // Next normal movement internally prepares its starting pose at bounded speed.
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/hand_wave", "request-wave000003", false, 17000), "status", "in_progress"));
    for (uint64_t t = 17000; t <= 35000; t += 50) { robot.Tick(&core, t); core.Tick(t); }
    CHECK(ReplyIs(robot.Status(), "status", "finished"));
    for (const auto degrees : core.CommandedPose().relative_degrees) CHECK(degrees == 0);
    CHECK(initialized == 1 && backend.writes == writes && backend.values == before_store);
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/greeting", "request-stop00001", false, 36000), "status", "in_progress"));
    for (uint64_t t = 36000; t <= 38000; t += 50) robot.Tick(&core, t);
    CHECK(ReplyIs(robot.Stop(&core, 8), "reason", "movement_not_owner"));
    CHECK(robot.running());
    robot.OnTransportClosed(&core, 8); CHECK(robot.running());
    robot.OnTransportClosed(&core, 7); CHECK(!robot.running() && !core.IsArmed());
    CHECK(ReplyIs(robot.Status(), "status", "stopped"));
    // A published movement may begin away from zero. Playback prepares that
    // authored pose locally; the editor runner's default zero-start gate remains.
    auto offset_record = RecordFor(Payload());
    std::string text(offset_record.payload.begin(), offset_record.payload.end());
    const auto first_target = text.find("\"time_ms\":0,\"target\":{\"arm_positive_x\":0");
    CHECK(first_target != std::string::npos);
    const auto angle = text.find("\"arm_positive_x\":0", first_target);
    text.replace(angle, std::string("\"arm_positive_x\":0").size(), "\"arm_positive_x\":20");
    offset_record.package_id = "offset-start";
    offset_record.payload.assign(text.begin(), text.end());
    offset_record.payload_crc32 = MotionPackageCrc32(offset_record.payload.data(), offset_record.payload.size());
    CHECK(store.Save({offset_record.package_id.c_str(), offset_record.profile_id.c_str(), offset_record.calibration_id.c_str(),
                      offset_record.payload_crc32, offset_record.payload.data(), offset_record.payload.size()}).ok);
    CHECK(ReplyIs(robot.Play(&profile, &core, 9, "stored/offset-start", "request-offset001", false, 40000), "status", "in_progress"));
    for (uint64_t t = 40000; t <= 68000; t += 50) { robot.Tick(&core, t); core.Tick(t); }
    CHECK(ReplyIs(robot.Status(), "status", "finished"));
    CHECK(!robot.running() && !core.IsArmed());
    // A Studio session already holding the drives blocks normal playback.
    CHECK(core.Arm(21, kCalibration, true, "editor-session-0001", 70000).ok);
    const auto before_editor = applied;
    CHECK(ReplyIs(robot.Play(&profile, &core, 9, "builtin/hand_wave", "request-busy00001", false, 70000), "reason", "editor_busy"));
    CHECK(applied == before_editor && core.IsArmed());
    core.OnTransportClosed(21);
    // Corrupted stored bytes are rejected before any hardware preparation.
    for (auto& entry : backend.values) {
        if (entry.second.size() > 100) entry.second[entry.second.size() / 2] ^= 0x20;
    }
    CHECK(ReplyIs(robot.Play(&profile, &core, 9, "stored/offset-start", "request-corrupt01", false, 71000), "status", "rejected"));
    CHECK(applied == before_editor && !core.IsArmed() && !robot.running());
    std::cout << "Robot movement catalog/local playback/ownership/persistence: PASS\n";
    return 0;
}
