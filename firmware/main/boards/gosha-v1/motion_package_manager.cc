#include "motion_package_manager.h"

#include <string>
#include <vector>

#include "motion_package.h"
#include "motion_package_json.h"

namespace gosha::motion_live {

namespace {

MotionPackageManagerResult FromUploadResult(
    const MotionPackageUploadResult& upload_result) {
    MotionPackageManagerResult result;
    result.ok = upload_result.ok;
    result.code = upload_result.code;
    result.expected_offset = upload_result.expected_offset;
    result.value = upload_result.value;
    result.limit = upload_result.limit;
    return result;
}

MotionPackageManagerResult FromStoreResult(
    const MotionPackageStoreResult& store_result) {
    MotionPackageManagerResult result;
    result.ok = store_result.ok;
    result.code = store_result.code;
    return result;
}

MotionPackageManagerResult Error(const char* code) {
    MotionPackageManagerResult result;
    result.ok = false;
    result.code = code;
    return result;
}

}  // namespace

MotionPackageManagerResult MotionPackageManager::BeginUpload(
    const MotionLivePreparedProfile& profile,
    const MotionPackageUploadBegin& begin) {
    const MotionPackageUploadResult result = upload_.Begin(profile, begin);
    if (result.ok) {
        upload_profile_ = profile;
        upload_profile_ready_ = true;
    }
    return FromUploadResult(result);
}

MotionPackageManagerResult MotionPackageManager::AppendUpload(
    const MotionPackageUploadChunk& chunk) {
    return FromUploadResult(upload_.Append(chunk));
}

MotionPackageManagerResult MotionPackageManager::FinishUpload() {
    if (!upload_.active()) {
        return Error("upload_not_started");
    }
    if (!upload_profile_ready_) {
        upload_.Abort();
        return Error("upload_profile_missing");
    }

    const std::string package_id = upload_.package_id();
    const std::string calibration_id = upload_.calibration_id();
    const uint32_t payload_crc32 = upload_.expected_crc32();

    std::vector<uint8_t> payload;
    const MotionPackageUploadResult upload_result = upload_.Finish(&payload);
    if (!upload_result.ok) {
        if (!upload_.active()) {
            upload_profile_ready_ = false;
        }
        return FromUploadResult(upload_result);
    }

    MotionPackageOwnedDraft parsed;
    const MotionPackageJsonParseResult parse_result =
        ParseMotionPackageDraftJson(payload.data(), payload.size(), &parsed);
    if (!parse_result.ok) {
        upload_profile_ready_ = false;
        return Error(parse_result.code);
    }

    const MotionPackageValidationResult validation_result =
        ValidateMotionPackageDraft(upload_profile_, parsed.draft);
    if (!validation_result.ok) {
        upload_profile_ready_ = false;
        return Error(validation_result.code);
    }

    const MotionPackageStoreRecord record = {
        package_id.c_str(),
        kModelProfileId,
        calibration_id.c_str(),
        payload_crc32,
        payload.data(),
        payload.size(),
    };
    const MotionPackageManagerResult store_result = FromStoreResult(store_.Save(record));
    upload_profile_ready_ = false;
    return store_result;
}

void MotionPackageManager::AbortUpload() {
    upload_.Abort();
    upload_profile_ready_ = false;
}

MotionPackageManagerResult MotionPackageManager::Load(
    MotionPackageLoadedRecord* record) {
    return FromStoreResult(store_.Load(record));
}

MotionPackageManagerResult MotionPackageManager::LoadById(
    const char* package_id, MotionPackageLoadedRecord* record) {
    return FromStoreResult(store_.LoadById(package_id, record));
}

MotionPackageManagerResult MotionPackageManager::List(
    std::vector<MotionPackageStoreEntry>* entries) {
    return FromStoreResult(store_.List(entries));
}

MotionPackageManagerResult MotionPackageManager::Select(const char* package_id) {
    if (upload_.active()) {
        return Error("package_upload_active");
    }
    return FromStoreResult(store_.Select(package_id));
}

MotionPackageManagerResult MotionPackageManager::Delete() {
    if (upload_.active()) {
        return Error("package_upload_active");
    }
    return FromStoreResult(store_.Delete());
}

MotionPackageManagerResult MotionPackageManager::DeleteById(const char* package_id) {
    if (upload_.active()) {
        return Error("package_upload_active");
    }
    return FromStoreResult(store_.DeleteById(package_id));
}

}  // namespace gosha::motion_live
