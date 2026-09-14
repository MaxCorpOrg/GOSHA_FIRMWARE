#include "motion_package_store.h"

#include <cctype>
#include <cstring>

namespace gosha::motion_live {

namespace {

constexpr uint32_t kStoreMagic = 0x474D504Bu;  // GMPK
constexpr uint16_t kStoreVersion = 1;

bool Streq(const char* left, const char* right) {
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

const char* SlotForIndex(int index) {
    return index == 0 ? kMotionPackageStoreSlotAKey : kMotionPackageStoreSlotBKey;
}

uint8_t ActiveValueForSlot(const char* slot) {
    return Streq(slot, kMotionPackageStoreSlotBKey) ? 'B' : 'A';
}

bool IsHex64(const char* value) {
    if (value == nullptr) {
        return false;
    }
    for (int i = 0; i < 64; ++i) {
        const char c = value[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return false;
        }
    }
    return value[64] == '\0';
}

bool IsSafePackageId(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    size_t length = 0;
    for (; value[length] != '\0'; ++length) {
        if (length >= kMotionPackageUploadMaxIdBytes) {
            return false;
        }
        const auto ch = static_cast<unsigned char>(value[length]);
        if (!std::isalnum(ch) && value[length] != '-' && value[length] != '_' &&
            value[length] != '.') {
            return false;
        }
    }
    if (length == 1 && value[0] == '.') {
        return false;
    }
    if (length == 2 && value[0] == '.' && value[1] == '.') {
        return false;
    }
    return length > 0;
}

MotionPackageStoreResult Error(const char* code) {
    MotionPackageStoreResult result;
    result.ok = false;
    result.code = code;
    return result;
}

MotionPackageStoreResult Ok() {
    MotionPackageStoreResult result;
    result.ok = true;
    result.code = "ok";
    return result;
}

void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>(value & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out->push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
    }
}

bool ReadU16(const std::vector<uint8_t>& input, size_t* offset, uint16_t* value) {
    if (offset == nullptr || value == nullptr || *offset + 2 > input.size()) {
        return false;
    }
    *value = static_cast<uint16_t>(input[*offset]) |
             (static_cast<uint16_t>(input[*offset + 1]) << 8);
    *offset += 2;
    return true;
}

bool ReadU32(const std::vector<uint8_t>& input, size_t* offset, uint32_t* value) {
    if (offset == nullptr || value == nullptr || *offset + 4 > input.size()) {
        return false;
    }
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        out |= static_cast<uint32_t>(input[*offset]) << shift;
        ++(*offset);
    }
    *value = out;
    return true;
}

bool AppendString(std::vector<uint8_t>* out, const char* value) {
    if (out == nullptr || value == nullptr) {
        return false;
    }
    const size_t length = std::strlen(value);
    if (length > 255) {
        return false;
    }
    out->push_back(static_cast<uint8_t>(length));
    out->insert(out->end(), value, value + length);
    return true;
}

bool ReadString(const std::vector<uint8_t>& input, size_t* offset,
                std::string* value) {
    if (offset == nullptr || value == nullptr || *offset >= input.size()) {
        return false;
    }
    const size_t length = input[*offset];
    ++(*offset);
    if (*offset + length > input.size()) {
        return false;
    }
    value->assign(reinterpret_cast<const char*>(input.data() + *offset), length);
    *offset += length;
    return true;
}

bool SerializeRecord(const MotionPackageStoreRecord& record,
                     std::vector<uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    if (!IsSafePackageId(record.package_id) ||
        !Streq(record.profile_id, kModelProfileId) ||
        !IsHex64(record.calibration_id) ||
        record.payload == nullptr ||
        record.payload_size == 0 ||
        record.payload_size > kMotionPackageUploadMaxBytes ||
        MotionPackageCrc32(record.payload, record.payload_size) !=
            record.payload_crc32) {
        return false;
    }

    out->clear();
    AppendU32(out, kStoreMagic);
    AppendU16(out, kStoreVersion);
    if (!AppendString(out, record.package_id) ||
        !AppendString(out, record.profile_id) ||
        !AppendString(out, record.calibration_id)) {
        out->clear();
        return false;
    }
    AppendU32(out, record.payload_crc32);
    AppendU32(out, static_cast<uint32_t>(record.payload_size));
    out->insert(out->end(), record.payload, record.payload + record.payload_size);
    if (out->size() > kMotionPackageStoreMaxRecordBytes) {
        out->clear();
        return false;
    }
    return true;
}

bool DeserializeRecord(const std::vector<uint8_t>& input,
                       MotionPackageLoadedRecord* record) {
    if (record == nullptr || input.empty() ||
        input.size() > kMotionPackageStoreMaxRecordBytes) {
        return false;
    }
    size_t offset = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint32_t payload_size = 0;
    if (!ReadU32(input, &offset, &magic) ||
        !ReadU16(input, &offset, &version) ||
        magic != kStoreMagic ||
        version != kStoreVersion ||
        !ReadString(input, &offset, &record->package_id) ||
        !ReadString(input, &offset, &record->profile_id) ||
        !ReadString(input, &offset, &record->calibration_id) ||
        !ReadU32(input, &offset, &record->payload_crc32) ||
        !ReadU32(input, &offset, &payload_size) ||
        payload_size == 0 ||
        payload_size > kMotionPackageUploadMaxBytes ||
        offset + payload_size != input.size() ||
        !IsSafePackageId(record->package_id.c_str()) ||
        record->profile_id != kModelProfileId ||
        !IsHex64(record->calibration_id.c_str())) {
        return false;
    }
    record->payload.assign(input.begin() + static_cast<long>(offset), input.end());
    return MotionPackageCrc32(record->payload.data(), record->payload.size()) ==
           record->payload_crc32;
}

}  // namespace

MotionPackageStore::ActiveSlotStatus MotionPackageStore::ReadActiveSlot(
    const char** active_slot) {
    if (backend_ == nullptr || active_slot == nullptr) {
        return ActiveSlotStatus::kCorrupt;
    }
    *active_slot = nullptr;
    std::vector<uint8_t> active;
    if (!backend_->Read(kMotionPackageStoreActiveKey, &active)) {
        return ActiveSlotStatus::kMissing;
    }
    if (active.size() == 1 && active[0] == 'A') {
        *active_slot = kMotionPackageStoreSlotAKey;
        return ActiveSlotStatus::kFound;
    }
    if (active.size() == 1 && active[0] == 'B') {
        *active_slot = kMotionPackageStoreSlotBKey;
        return ActiveSlotStatus::kFound;
    }
    return ActiveSlotStatus::kCorrupt;
}

bool MotionPackageStore::SlotExists(const char* slot) {
    if (backend_ == nullptr || slot == nullptr) {
        return false;
    }
    std::vector<uint8_t> value;
    return backend_->Read(slot, &value);
}

MotionPackageStoreResult MotionPackageStore::LoadSlot(
    const char* slot, MotionPackageLoadedRecord* record) {
    if (backend_ == nullptr || slot == nullptr || record == nullptr) {
        return Error("store_backend_missing");
    }
    std::vector<uint8_t> serialized;
    if (!backend_->Read(slot, &serialized)) {
        return Error("store_slot_missing");
    }
    MotionPackageLoadedRecord loaded;
    if (!DeserializeRecord(serialized, &loaded)) {
        return Error("store_corrupt");
    }
    *record = loaded;
    return Ok();
}

const char* MotionPackageStore::InactiveSlotFor(const char* active_slot) const {
    return Streq(active_slot, kMotionPackageStoreSlotAKey)
               ? kMotionPackageStoreSlotBKey
               : kMotionPackageStoreSlotAKey;
}

bool MotionPackageStore::WriteActiveSlot(const char* active_slot) {
    if (backend_ == nullptr || active_slot == nullptr) {
        return false;
    }
    const std::vector<uint8_t> active_value = {ActiveValueForSlot(active_slot)};
    return backend_->Write(kMotionPackageStoreActiveKey, active_value);
}

MotionPackageStoreResult MotionPackageStore::Save(
    const MotionPackageStoreRecord& record) {
    if (backend_ == nullptr) {
        return Error("store_backend_missing");
    }
    std::vector<uint8_t> serialized;
    if (!SerializeRecord(record, &serialized)) {
        return Error("store_record_invalid");
    }
    const char* active_slot = nullptr;
    const auto active_status = ReadActiveSlot(&active_slot);
    if (active_status == ActiveSlotStatus::kCorrupt) {
        return Error("store_active_corrupt");
    }
    if (active_status == ActiveSlotStatus::kMissing &&
        (SlotExists(kMotionPackageStoreSlotAKey) ||
         SlotExists(kMotionPackageStoreSlotBKey))) {
        return Error("store_active_missing");
    }
    const char* inactive_slot = active_status == ActiveSlotStatus::kFound
                                    ? InactiveSlotFor(active_slot)
                                    : kMotionPackageStoreSlotBKey;
    if (!backend_->Write(inactive_slot, serialized)) {
        return Error("store_slot_write_failed");
    }
    if (!WriteActiveSlot(inactive_slot)) {
        backend_->Erase(inactive_slot);
        return Error("store_commit_failed");
    }
    if (active_status == ActiveSlotStatus::kFound) {
        MotionPackageLoadedRecord previous;
        const MotionPackageStoreResult previous_result =
            LoadSlot(active_slot, &previous);
        if (previous_result.ok && previous.package_id == record.package_id) {
            backend_->Erase(active_slot);
        }
    }
    return Ok();
}

MotionPackageStoreResult MotionPackageStore::List(
    std::vector<MotionPackageStoreEntry>* entries) {
    if (backend_ == nullptr || entries == nullptr) {
        return Error("store_backend_missing");
    }
    entries->clear();
    const char* active_slot = nullptr;
    const auto active_status = ReadActiveSlot(&active_slot);
    if (active_status == ActiveSlotStatus::kCorrupt) {
        return Error("store_active_corrupt");
    }
    if (active_status == ActiveSlotStatus::kMissing) {
        return SlotExists(kMotionPackageStoreSlotAKey) ||
                       SlotExists(kMotionPackageStoreSlotBKey)
                   ? Error("store_active_missing")
                   : Ok();
    }

    bool active_found = false;
    for (int index = 0; index < 2; ++index) {
        const char* slot = SlotForIndex(index);
        if (!SlotExists(slot)) {
            continue;
        }
        MotionPackageLoadedRecord loaded;
        const MotionPackageStoreResult result = LoadSlot(slot, &loaded);
        if (!result.ok) {
            entries->clear();
            return result;
        }
        MotionPackageStoreEntry entry;
        entry.active = Streq(slot, active_slot);
        entry.record = loaded;
        active_found = active_found || entry.active;
        entries->push_back(entry);
    }
    if (!active_found) {
        entries->clear();
        return Error("store_active_missing");
    }
    return Ok();
}

MotionPackageStoreResult MotionPackageStore::Load(
    MotionPackageLoadedRecord* record) {
    if (backend_ == nullptr || record == nullptr) {
        return Error("store_backend_missing");
    }
    const char* active_slot = nullptr;
    const auto active_status = ReadActiveSlot(&active_slot);
    if (active_status == ActiveSlotStatus::kCorrupt) {
        return Error("store_active_corrupt");
    }
    if (active_status == ActiveSlotStatus::kMissing) {
        return SlotExists(kMotionPackageStoreSlotAKey) ||
                       SlotExists(kMotionPackageStoreSlotBKey)
                   ? Error("store_active_missing")
                   : Error("store_empty");
    }
    MotionPackageLoadedRecord loaded;
    const MotionPackageStoreResult result = LoadSlot(active_slot, &loaded);
    if (!result.ok && std::strcmp(result.code, "store_slot_missing") == 0) {
        const char* inactive_slot = InactiveSlotFor(active_slot);
        return SlotExists(inactive_slot) ? Error("store_corrupt")
                                         : Error("store_empty");
    }
    if (!result.ok) {
        return result;
    }
    *record = loaded;
    return Ok();
}

MotionPackageStoreResult MotionPackageStore::LoadById(
    const char* package_id, MotionPackageLoadedRecord* record) {
    if (!IsSafePackageId(package_id) || record == nullptr) {
        return Error("store_package_id_invalid");
    }
    std::vector<MotionPackageStoreEntry> entries;
    const MotionPackageStoreResult result = List(&entries);
    if (!result.ok) {
        return result;
    }
    for (const MotionPackageStoreEntry& entry : entries) {
        if (entry.record.package_id == package_id) {
            *record = entry.record;
            return Ok();
        }
    }
    return Error("store_package_not_found");
}

MotionPackageStoreResult MotionPackageStore::Select(const char* package_id) {
    if (backend_ == nullptr) {
        return Error("store_backend_missing");
    }
    if (!IsSafePackageId(package_id)) {
        return Error("store_package_id_invalid");
    }
    const char* active_slot = nullptr;
    const auto active_status = ReadActiveSlot(&active_slot);
    if (active_status == ActiveSlotStatus::kCorrupt) {
        return Error("store_active_corrupt");
    }
    if (active_status == ActiveSlotStatus::kMissing) {
        return SlotExists(kMotionPackageStoreSlotAKey) ||
                       SlotExists(kMotionPackageStoreSlotBKey)
                   ? Error("store_active_missing")
                   : Error("store_package_not_found");
    }

    const char* selected_slot = nullptr;
    bool active_found = false;
    for (int index = 0; index < 2; ++index) {
        const char* slot = SlotForIndex(index);
        if (!SlotExists(slot)) {
            continue;
        }
        MotionPackageLoadedRecord loaded;
        const MotionPackageStoreResult result = LoadSlot(slot, &loaded);
        if (!result.ok) {
            return result;
        }
        active_found = active_found || Streq(slot, active_slot);
        if (loaded.package_id == package_id) {
            selected_slot = slot;
        }
    }
    if (!active_found) {
        return Error("store_active_missing");
    }
    if (selected_slot == nullptr) {
        return Error("store_package_not_found");
    }
    return WriteActiveSlot(selected_slot) ? Ok() : Error("store_commit_failed");
}

MotionPackageStoreResult MotionPackageStore::Delete() {
    if (backend_ == nullptr) {
        return Error("store_backend_missing");
    }
    const bool erased_active = backend_->Erase(kMotionPackageStoreActiveKey);
    const bool erased_a = backend_->Erase(kMotionPackageStoreSlotAKey);
    const bool erased_b = backend_->Erase(kMotionPackageStoreSlotBKey);
    return erased_active && erased_a && erased_b ? Ok() : Error("store_delete_failed");
}

MotionPackageStoreResult MotionPackageStore::DeleteById(const char* package_id) {
    if (backend_ == nullptr) {
        return Error("store_backend_missing");
    }
    if (!IsSafePackageId(package_id)) {
        return Error("store_package_id_invalid");
    }

    const char* active_slot = nullptr;
    const auto active_status = ReadActiveSlot(&active_slot);
    if (active_status == ActiveSlotStatus::kCorrupt) {
        return Error("store_active_corrupt");
    }
    if (active_status == ActiveSlotStatus::kMissing) {
        return SlotExists(kMotionPackageStoreSlotAKey) ||
                       SlotExists(kMotionPackageStoreSlotBKey)
                   ? Error("store_active_missing")
                   : Error("store_package_not_found");
    }

    const char* selected_slot = nullptr;
    const char* replacement_slot = nullptr;
    bool active_found = false;
    for (int index = 0; index < 2; ++index) {
        const char* slot = SlotForIndex(index);
        if (!SlotExists(slot)) {
            continue;
        }
        MotionPackageLoadedRecord loaded;
        const MotionPackageStoreResult result = LoadSlot(slot, &loaded);
        if (!result.ok) {
            return result;
        }
        const bool slot_active = Streq(slot, active_slot);
        active_found = active_found || slot_active;
        if (loaded.package_id == package_id) {
            selected_slot = slot;
        } else if (replacement_slot == nullptr) {
            replacement_slot = slot;
        }
    }
    if (!active_found) {
        return Error("store_active_missing");
    }
    if (selected_slot == nullptr) {
        return Error("store_package_not_found");
    }

    if (Streq(selected_slot, active_slot)) {
        if (replacement_slot != nullptr && !WriteActiveSlot(replacement_slot)) {
            return Error("store_commit_failed");
        }
        const bool erased_selected = backend_->Erase(selected_slot);
        if (!erased_selected) {
            if (replacement_slot != nullptr) {
                WriteActiveSlot(selected_slot);
            }
            return Error("store_delete_failed");
        }
        if (replacement_slot == nullptr &&
            !backend_->Erase(kMotionPackageStoreActiveKey)) {
            return Error("store_delete_failed");
        }
        return Ok();
    }

    return backend_->Erase(selected_slot) ? Ok() : Error("store_delete_failed");
}

}  // namespace gosha::motion_live
