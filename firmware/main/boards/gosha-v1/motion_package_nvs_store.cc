#include "motion_package_nvs_store.h"

#include <esp_log.h>
#include <nvs.h>

#include <cstring>

namespace gosha::motion_live {

namespace {

constexpr const char* TAG = "MotionPkgNvs";

bool Streq(const char* left, const char* right) {
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

bool IsKnownKey(const char* key) {
    return Streq(key, kMotionPackageStoreSlotAKey) ||
           Streq(key, kMotionPackageStoreSlotBKey) ||
           Streq(key, kMotionPackageStoreActiveKey);
}

size_t MaxValueBytesForKey(const char* key) {
    return Streq(key, kMotionPackageStoreActiveKey)
               ? 1
               : kMotionPackageStoreMaxRecordBytes;
}

bool Open(nvs_open_mode_t mode, nvs_handle_t* handle) {
    if (handle == nullptr) {
        return false;
    }
    *handle = 0;
    const esp_err_t err = nvs_open(kMotionPackageNvsNamespace, mode, handle);
    if (err == ESP_ERR_NVS_NOT_FOUND && mode == NVS_READONLY) {
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for motion package store: %s",
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace

bool MotionPackageNvsStoreBackend::Read(const char* key,
                                        std::vector<uint8_t>* value) {
    if (!IsKnownKey(key) || value == nullptr) {
        return false;
    }
    nvs_handle_t handle = 0;
    if (!Open(NVS_READONLY, &handle)) {
        return false;
    }

    size_t length = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &length);
    if (err != ESP_OK) {
        nvs_close(handle);
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS read length failed for %s: %s", key,
                     esp_err_to_name(err));
        }
        return false;
    }
    if (length == 0 || length > MaxValueBytesForKey(key)) {
        nvs_close(handle);
        ESP_LOGW(TAG, "NVS value size for %s is invalid: %u", key,
                 static_cast<unsigned>(length));
        return false;
    }

    std::vector<uint8_t> buffer(length);
    err = nvs_get_blob(handle, key, buffer.data(), &length);
    nvs_close(handle);
    if (err != ESP_OK || length != buffer.size()) {
        ESP_LOGW(TAG, "NVS read failed for %s: %s", key, esp_err_to_name(err));
        return false;
    }
    *value = std::move(buffer);
    return true;
}

bool MotionPackageNvsStoreBackend::Write(
    const char* key, const std::vector<uint8_t>& value) {
    if (!IsKnownKey(key) || value.empty() ||
        value.size() > MaxValueBytesForKey(key)) {
        return false;
    }
    nvs_handle_t handle = 0;
    if (!Open(NVS_READWRITE, &handle)) {
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, key, value.data(), value.size());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS write failed for %s: %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool MotionPackageNvsStoreBackend::Erase(const char* key) {
    if (!IsKnownKey(key)) {
        return false;
    }
    nvs_handle_t handle = 0;
    if (!Open(NVS_READWRITE, &handle)) {
        return false;
    }
    esp_err_t err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return true;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS erase failed for %s: %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace gosha::motion_live
