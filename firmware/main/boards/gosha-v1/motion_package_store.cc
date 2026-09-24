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
    constexpr const char* slots[] = {kMotionPackageStoreSlotAKey, kMotionPackageStoreSlotBKey,
                                     kMotionPackageStoreSlotCKey, kMotionPackageStoreSlotDKey};
    return index >= 0 && index < 4 ? slots[index] : nullptr;
}

constexpr uint8_t kCatalogMagic[] = {'G', 'M', 'I', 1};
constexpr uint8_t kMigrationMagic[] = {'M', 'I', 'G', 1};
constexpr int kSlotCount = 4;

int IndexForActiveValue(uint8_t value) {
    return value == 'A' ? 0 : value == 'B' ? 1 : -1;
}

int CountSlots(uint8_t mask) {
    int count = 0;
    for (int i = 0; i < kSlotCount; ++i) count += (mask >> i) & 1;
    return count;
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

MotionPackageStoreResult MotionPackageStore::ReadCatalog(Catalog* catalog) {
    if (backend_ == nullptr || catalog == nullptr) return Error("store_backend_missing");
    *catalog = {};
    std::vector<uint8_t> raw;
    if (backend_->Read(kMotionPackageStoreCatalogKey, &raw)) {
        if (raw.size() != 6 ||
            std::memcmp(raw.data(), kCatalogMagic, sizeof(kCatalogMagic)) != 0 ||
            (raw[4] & 0xf0u) != 0 ||
            CountSlots(raw[4]) > static_cast<int>(kMotionPackageStoreLibraryLimit) ||
            (raw[4] == 0 ? raw[5] != 0xff :
             raw[5] >= kSlotCount || (raw[4] & (1u << raw[5])) == 0)) {
            return Error("store_catalog_corrupt");
        }
        catalog->mask = raw[4];
        catalog->active = raw[5];
        return Ok();
    }

    std::vector<uint8_t> migration;
    const bool migrating = backend_->Read(kMotionPackageStoreMigrationKey, &migration);
    if (migrating &&
        (migration.size() != 6 ||
         std::memcmp(migration.data(), kMigrationMagic, sizeof(kMigrationMagic)) != 0 ||
         (migration[4] & ~3u) != 0 ||
         (migration[4] == 0 ? migration[5] != 0xff :
          migration[5] >= 2 || (migration[4] & (1u << migration[5])) == 0)))
        return Error("store_migration_corrupt");
    // C/D without the migration marker mean the indexed catalog was lost.
    if (!migrating && (SlotExists(kMotionPackageStoreSlotCKey) ||
                       SlotExists(kMotionPackageStoreSlotDKey)))
        return Error("store_catalog_missing");

    // The accepted firmware stores records in A/B and the active pointer
    // separately. Read that layout without modifying either payload.
    catalog->legacy = true;
    std::vector<uint8_t> active;
    const bool has_active = backend_->Read(kMotionPackageStoreActiveKey, &active);
    const bool has_a = SlotExists(kMotionPackageStoreSlotAKey);
    const bool has_b = SlotExists(kMotionPackageStoreSlotBKey);
    if (migrating) {
        catalog->mask = migration[4];
        catalog->active = migration[5];
        if (((catalog->mask & 1u) && !has_a) ||
            ((catalog->mask & 2u) && !has_b))
            return Error("store_migration_missing");
        return Ok();
    }
    if (!has_active) {
        return has_a || has_b ? Error("store_active_missing") : Ok();
    }
    if (active.size() != 1 || IndexForActiveValue(active[0]) < 0)
        return Error("store_active_corrupt");
    catalog->active = static_cast<uint8_t>(IndexForActiveValue(active[0]));
    catalog->mask = (has_a ? 1u : 0u) | (has_b ? 2u : 0u);
    if ((catalog->mask & (1u << catalog->active)) == 0)
        return Error("store_active_missing");
    return Ok();
}

bool MotionPackageStore::WriteCatalog(const Catalog& catalog) {
    if (backend_ == nullptr) return false;
    const std::vector<uint8_t> raw = {'G', 'M', 'I', 1, catalog.mask, catalog.active};
    return backend_->Write(kMotionPackageStoreCatalogKey, raw);
}

bool MotionPackageStore::SlotExists(const char* slot) {
    std::vector<uint8_t> value;
    return backend_ != nullptr && slot != nullptr && backend_->Read(slot, &value);
}

MotionPackageStoreResult MotionPackageStore::LoadSlot(
    const char* slot, MotionPackageLoadedRecord* record) {
    if (backend_ == nullptr || slot == nullptr || record == nullptr)
        return Error("store_backend_missing");
    std::vector<uint8_t> serialized;
    if (!backend_->Read(slot, &serialized)) return Error("store_slot_missing");
    return DeserializeRecord(serialized, record) ? Ok() : Error("store_corrupt");
}

MotionPackageStoreResult MotionPackageStore::ValidateCatalog(const Catalog& catalog) {
    std::vector<std::string> ids;
    for (int i = 0; i < kSlotCount; ++i) {
        if ((catalog.mask & (1u << i)) == 0) continue;
        MotionPackageLoadedRecord loaded;
        const auto result = LoadSlot(SlotForIndex(i), &loaded);
        if (!result.ok) return result;
        for (const auto& id : ids)
            if (id == loaded.package_id) return Error("store_duplicate_id");
        ids.push_back(loaded.package_id);
    }
    return Ok();
}

MotionPackageStoreResult MotionPackageStore::Save(
    const MotionPackageStoreRecord& record) {
    if (backend_ == nullptr) return Error("store_backend_missing");
    std::vector<uint8_t> serialized;
    if (!SerializeRecord(record, &serialized)) return Error("store_record_invalid");

    Catalog catalog;
    auto result = ReadCatalog(&catalog);
    if (!result.ok) return result;
    result = ValidateCatalog(catalog);
    if (!result.ok) return result;

    int old_slot = -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if ((catalog.mask & (1u << i)) == 0) continue;
        MotionPackageLoadedRecord loaded;
        result = LoadSlot(SlotForIndex(i), &loaded);
        if (!result.ok) return result;
        if (loaded.package_id == record.package_id) old_slot = i;
    }
    if (old_slot < 0 &&
        CountSlots(catalog.mask) >= static_cast<int>(kMotionPackageStoreLibraryLimit))
        return Error("store_full");

    if (catalog.legacy) {
        const std::vector<uint8_t> marker = {'M', 'I', 'G', 1,
                                             catalog.mask, catalog.active};
        if (!backend_->Write(kMotionPackageStoreMigrationKey, marker))
            return Error("store_migration_write_failed");
    } else if (SlotExists(kMotionPackageStoreMigrationKey) &&
               !backend_->Erase(kMotionPackageStoreMigrationKey)) {
        return Error("store_migration_cleanup_failed");
    }

    // A spare slot keeps the old version intact until the catalog commit.
    // On first migration prefer C/D so failed writes cannot alter legacy A/B.
    int free_slot = -1;
    const int first_candidate = catalog.legacy
                                    ? (catalog.mask == 0 ? 1 : 2)
                                    : 0;
    for (int i = first_candidate; i < kSlotCount; ++i) {
        if ((catalog.mask & (1u << i)) == 0) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) return Error("store_full");
    const char* key = SlotForIndex(free_slot);
    if (SlotExists(key) && !backend_->Erase(key))
        return Error("store_staging_cleanup_failed");
    if (!backend_->Write(key, serialized)) return Error("store_slot_write_failed");
    MotionPackageLoadedRecord verified;
    result = LoadSlot(key, &verified);
    if (!result.ok || verified.package_id != record.package_id ||
        verified.payload_crc32 != record.payload_crc32 ||
        verified.payload.size() != record.payload_size ||
        std::memcmp(verified.payload.data(), record.payload, record.payload_size) != 0) {
        backend_->Erase(key);
        return Error("store_verify_failed");
    }

    Catalog next = catalog;
    next.legacy = false;
    next.mask = static_cast<uint8_t>((catalog.mask & ~(old_slot < 0 ? 0u : (1u << old_slot))) |
                                     (1u << free_slot));
    next.active = static_cast<uint8_t>(free_slot);
    if (!WriteCatalog(next)) {
        backend_->Erase(key);
        return Error("store_commit_failed");
    }
    // Keep the old slots until the marker is gone; a power cut before this
    // point can still reconstruct exactly the old legacy catalog.
    if (catalog.legacy && !backend_->Erase(kMotionPackageStoreMigrationKey))
        return Error("store_migration_cleanup_failed");
    if (free_slot < 2) {
        const std::vector<uint8_t> legacy_active = {
            static_cast<uint8_t>('A' + free_slot)};
        backend_->Write(kMotionPackageStoreActiveKey, legacy_active);
    }
    if (old_slot >= 0) backend_->Erase(SlotForIndex(old_slot));
    return Ok();
}

MotionPackageStoreResult MotionPackageStore::List(
    std::vector<MotionPackageStoreEntry>* entries) {
    if (entries == nullptr) return Error("store_backend_missing");
    entries->clear();
    Catalog catalog;
    auto result = ReadCatalog(&catalog);
    if (!result.ok) return result;
    result = ValidateCatalog(catalog);
    if (!result.ok) return result;
    for (int i = 0; i < kSlotCount; ++i) {
        if ((catalog.mask & (1u << i)) == 0) continue;
        MotionPackageStoreEntry entry;
        result = LoadSlot(SlotForIndex(i), &entry.record);
        if (!result.ok) {
            entries->clear();
            return result;
        }
        entry.active = catalog.active == i;
        entries->push_back(std::move(entry));
    }
    return Ok();
}

MotionPackageStoreResult MotionPackageStore::Load(
    MotionPackageLoadedRecord* record) {
    if (record == nullptr) return Error("store_backend_missing");
    Catalog catalog;
    auto result = ReadCatalog(&catalog);
    if (!result.ok) return result;
    result = ValidateCatalog(catalog);
    if (!result.ok) return result;
    if (catalog.mask == 0) return Error("store_empty");
    return LoadSlot(SlotForIndex(catalog.active), record);
}

MotionPackageStoreResult MotionPackageStore::LoadById(
    const char* package_id, MotionPackageLoadedRecord* record) {
    if (!IsSafePackageId(package_id) || record == nullptr)
        return Error("store_package_id_invalid");
    std::vector<MotionPackageStoreEntry> entries;
    const auto result = List(&entries);
    if (!result.ok) return result;
    for (const auto& entry : entries) {
        if (entry.record.package_id == package_id) {
            *record = entry.record;
            return Ok();
        }
    }
    return Error("store_package_not_found");
}

MotionPackageStoreResult MotionPackageStore::Select(const char* package_id) {
    if (!IsSafePackageId(package_id)) return Error("store_package_id_invalid");
    Catalog catalog;
    auto result = ReadCatalog(&catalog);
    if (!result.ok) return result;
    result = ValidateCatalog(catalog);
    if (!result.ok) return result;
    for (int i = 0; i < kSlotCount; ++i) {
        if ((catalog.mask & (1u << i)) == 0) continue;
        MotionPackageLoadedRecord loaded;
        result = LoadSlot(SlotForIndex(i), &loaded);
        if (!result.ok) return result;
        if (loaded.package_id != package_id) continue;
        if (catalog.legacy) {
            const std::vector<uint8_t> active = {static_cast<uint8_t>('A' + i)};
            return backend_->Write(kMotionPackageStoreActiveKey, active)
                       ? Ok() : Error("store_commit_failed");
        }
        catalog.active = static_cast<uint8_t>(i);
        return WriteCatalog(catalog) ? Ok() : Error("store_commit_failed");
    }
    return Error("store_package_not_found");
}

MotionPackageStoreResult MotionPackageStore::Delete() {
    if (backend_ == nullptr) return Error("store_backend_missing");
    Catalog empty;
    if (!WriteCatalog(empty)) return Error("store_commit_failed");
    bool ok = backend_->Erase(kMotionPackageStoreActiveKey);
    ok = backend_->Erase(kMotionPackageStoreMigrationKey) && ok;
    for (int i = 0; i < kSlotCount; ++i)
        ok = backend_->Erase(SlotForIndex(i)) && ok;
    return ok ? Ok() : Error("store_delete_failed");
}

MotionPackageStoreResult MotionPackageStore::DeleteById(const char* package_id) {
    if (!IsSafePackageId(package_id)) return Error("store_package_id_invalid");
    Catalog catalog;
    auto result = ReadCatalog(&catalog);
    if (!result.ok) return result;
    result = ValidateCatalog(catalog);
    if (!result.ok) return result;
    int selected = -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if ((catalog.mask & (1u << i)) == 0) continue;
        MotionPackageLoadedRecord loaded;
        result = LoadSlot(SlotForIndex(i), &loaded);
        if (!result.ok) return result;
        if (loaded.package_id == package_id) selected = i;
    }
    if (selected < 0) return Error("store_package_not_found");

    Catalog next = catalog;
    next.legacy = false;
    next.mask = static_cast<uint8_t>(catalog.mask & ~(1u << selected));
    if (next.mask == 0) {
        next.active = 0xff;
    } else if (catalog.active == selected) {
        for (int i = 0; i < kSlotCount; ++i)
            if (next.mask & (1u << i)) {
                next.active = static_cast<uint8_t>(i);
                break;
            }
    }
    if (!WriteCatalog(next)) return Error("store_commit_failed");
    // A failed erase leaves an unreachable blob, never a visible package.
    return backend_->Erase(SlotForIndex(selected)) ? Ok() : Error("store_delete_failed");
}

}  // namespace gosha::motion_live
