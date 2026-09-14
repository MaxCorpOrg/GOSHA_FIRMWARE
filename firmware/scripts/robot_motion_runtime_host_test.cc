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

int TestLegacyCatalog() {
    LegacyMotionPlan plan;
    const LegacyMotionPlan::Pose neutral{90, 90, 90, 90, 90, 135};
    LegacyMotionPlan::Pose target;
    CHECK(LegacyMotionPlan::Catalog().size() == 27);
    CHECK(plan.Build("builtin/walk_forward", neutral));
    CHECK(plan.duration_ms() == 3050);  // 3 * 700 + 50 scheduling + Home 700 + 200.
    CHECK(plan.Sample(175, &target));
    CHECK(target[0] == 120 && target[1] == 120 && target[2] == 95 && target[3] == 85);
    CHECK(plan.Sample(350, &target)); CHECK(target[2] == 125 && target[3] == 115);
    CHECK(plan.Build("builtin/walk_backward", neutral));
    CHECK(plan.Sample(0, &target)); CHECK(target[2] == 125 && target[3] == 115);
    CHECK(plan.Build("builtin/jump", neutral));
    CHECK(plan.Sample(700, &target)); CHECK(target[2] == 150 && target[3] == 30);
    CHECK(plan.Build("builtin/hand_wave", neutral));
    CHECK(plan.Sample(0, &target)); CHECK(target[5] == 40);
    CHECK(plan.Sample(150, &target)); CHECK(target[5] == 0);
    CHECK(plan.Sample(300, &target)); CHECK(target[5] == 40);
    CHECK(plan.Build("builtin/sit", neutral));
    CHECK(plan.Sample(600, &target)); CHECK(target[2] == 0 && target[3] == 180);
    CHECK(!plan.Build("builtin/servo_sequence", neutral));

    auto profile = MotionEditorProfile();
    MemoryStore backend;
    MotionPackageManager manager(&backend);
    RobotMotionRuntime robot(&manager);
    MotionLiveCore core;
    core.SetLocalOptInEnabled(true); core.SetPreparedProfile(&profile); core.SetRuntimeConfig(RuntimeConfig());
    int initialized = 0, applied = 0;
    auto last = neutral;
    core.SetRightArmInitializer([&](int value) { ++initialized; return value == 135; });
    core.SetHardwareApplier([&](const auto& value) {
        ++applied; last = value;
        return value[4] == 90 && std::all_of(value.begin(), value.end(), [](int x) { return x >= 0 && x <= 180; });
    });
    uint64_t now = 1000;
    int number = 0;
    for (const auto& entry : LegacyMotionPlan::Catalog()) {
        const auto id = std::string("catalog-request-") + std::to_string(++number);
        CHECK(ReplyIs(robot.Play(&profile, &core, 7, entry.id, id, false, now), "status", "in_progress"));
        CHECK(core.IsArmed());
        CHECK(!core.Arm(7, kCalibration, true, "same-owner-editor1", now).ok);
        MotionLiveTarget raw;
        raw.present[1] = true; raw.relative_degrees[1] = 10;
        CHECK(!core.Pose(7, "", 1, raw, 10, now).ok);
        // Exercise the production 10 ms clock, with Studio's 50 ms clock alongside.
        const auto limit = now + 121000;
        while (robot.running() && now < limit) {
            now += 10; robot.Tick(&core, now);
            if (now % 50 == 0) core.Tick(now);
        }
        CHECK(ReplyIs(robot.Status(), "status", "finished"));
        CHECK(!core.IsArmed() && last[4] == 90);
        if (std::string(entry.id) == "builtin/sit") {
            CHECK(last[0] == 120 && last[1] == 60 && last[2] == 0 && last[3] == 180);
            CHECK(std::string(core.EvaluateSafety()) == "ordinary_pose_outside_editor");
        } else if (std::string(entry.id) == "builtin/hands_up") {
            CHECK(last[5] == 10);
            CHECK(std::string(core.EvaluateSafety()) == "ordinary_pose_outside_editor");
        } else {
            CHECK(last == neutral);
            CHECK(std::string(core.EvaluateSafety()) == "ok");
        }
        now += 100;
    }
    CHECK(initialized == 1 && applied > 1000 && backend.writes == 0);
    // Stop preserves the actual commanded pose; a later Home restores Studio access.
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/sit", "stop-sit-request01", false, now), "status", "in_progress"));
    for (int i = 0; i < 50; ++i) { now += 10; robot.Tick(&core, now); }
    const auto held = last; const auto before = applied;
    CHECK(ReplyIs(robot.Stop(&core, 7), "status", "stopped"));
    now += 1000; robot.Tick(&core, now); CHECK(last == held && applied == before);
    CHECK(!core.Arm(8, kCalibration, true, "editor-after-sit01", now).ok);
    // Valid stored payload prepares Home locally from a legacy pose before using
    // the unchanged Studio validator/player. No store or active-pointer writes.
    auto record = RecordFor(Payload());
    MotionPackageStore store(&backend);
    CHECK(store.Save({record.package_id.c_str(), record.profile_id.c_str(), record.calibration_id.c_str(),
                     record.payload_crc32, record.payload.data(), record.payload.size()}).ok);
    const auto saved = backend.values;
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "stored/" + record.package_id, "stored-after-sit1", false, now), "status", "in_progress"));
    const auto deadline = now + 20000;
    while (robot.running() && now < deadline) { now += 10; robot.Tick(&core, now); if (now % 50 == 0) core.Tick(now); }
    CHECK(ReplyIs(robot.Status(), "status", "finished"));
    CHECK(backend.values == saved);
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/hand_wave", "watchdog-request1", false, now), "status", "in_progress"));
    const auto before_gap = applied;
    now += 301; robot.Tick(&core, now);
    CHECK(ReplyIs(robot.Status(), "reason", "watchdog_timeout"));
    CHECK(!robot.running() && !core.IsArmed() && applied == before_gap);
    // Partial PWM failure latches the fault; ordinary and editor commands cannot
    // assume a known position after some channels may have accepted the write.
    core.SetHardwareApplier([&](const auto&) { ++applied; return false; });
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/jump", "failure-request01", false, now), "status", "in_progress"));
    now += 50; robot.Tick(&core, now);
    CHECK(ReplyIs(robot.Status(), "status", "failed"));
    const auto failed_count = applied;
    CHECK(ReplyIs(robot.Play(&profile, &core, 7, "builtin/home", "failure-request02", false, now), "status", "failed"));
    CHECK(applied == failed_count && !core.IsArmed());
    CHECK(!core.Arm(8, kCalibration, true, "editor-after-fail1", now).ok);
    return 0;
}

int main() {
    CHECK(RunExistingRunnerTests() == 0);
    CHECK(TestLegacyCatalog() == 0);
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
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(listed, "movements")) == static_cast<int>(LegacyMotionPlan::Catalog().size() + 1));
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
    CHECK(!robot.running() && !core.IsArmed() && initialized == 1 && applied > 0 && min_right == 0);
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
