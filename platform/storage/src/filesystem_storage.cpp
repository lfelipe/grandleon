// SPDX-License-Identifier: MIT
#include <grandleon/storage/filesystem_storage.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace grandleon::storage {
namespace {

namespace fs = std::filesystem;

// A slot name becomes a path only after `is_valid_slot_name` has accepted it,
// so this appends a leaf and can never traverse.
fs::path path_of(const std::string& root, std::string_view slot) {
    fs::path path = fs::path(root);
    path /= std::string(slot) + std::string(FilesystemSlotStorage::file_suffix);
    return path;
}

// A slot's whole contents, or the reason there are none.
//
// `maximum` is the device's own per-slot budget, and it is consulted before a
// byte is read. A slot file is untrusted: `write` cannot produce one larger
// than the budget, so one that is larger was dropped into the directory,
// grown by another program, or is not a save at all, and reading it means
// allocating whatever its directory entry claims. Refusing on the size is what
// keeps a file somebody put there from choosing this process's allocation.
//
// The buffer is also sized before the file is opened, so an allocation that
// throws cannot leave a `FILE*` behind: there is not one yet.
StorageError read_whole_file(
    const fs::path& path, std::size_t maximum, std::vector<std::uint8_t>& out
) {
    std::error_code code;
    const std::uintmax_t size = fs::file_size(path, code);
    if (code) {
        return StorageError::io_failure;
    }
    if (size > static_cast<std::uintmax_t>(maximum)) {
        return StorageError::too_large;
    }
    out.resize(static_cast<std::size_t>(size));
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        out.clear();
        return StorageError::io_failure;
    }
    const std::size_t read =
        out.empty() ? 0U : std::fread(out.data(), 1, out.size(), file);
    const bool complete = read == out.size();
    std::fclose(file);
    if (!complete) {
        out.clear();
        return StorageError::io_failure;
    }
    return StorageError::none;
}

bool write_whole_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t written =
        bytes.empty() ? 0U : std::fwrite(bytes.data(), 1, bytes.size(), file);
    const bool complete = written == bytes.size();
    // A save is worth the flush: a slot that reads back short because the
    // buffer never reached the device is exactly the failure the staged write
    // below exists to prevent.
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    return complete && flushed && closed;
}

}  // namespace

FilesystemSlotStorage::FilesystemSlotStorage(std::string root)
    : FilesystemSlotStorage(std::move(root), StorageBudget{1U << 20U, 64U << 20U, 256U}) {}

FilesystemSlotStorage::FilesystemSlotStorage(std::string root, StorageBudget budget)
    : root_(std::move(root)), budget_(budget) {
    std::error_code code;
    fs::create_directories(fs::path(root_), code);
    available_ = fs::is_directory(fs::path(root_), code);
}

StorageError FilesystemSlotStorage::write(
    std::string_view slot,
    const std::vector<std::uint8_t>& bytes
) {
    if (!is_valid_slot_name(slot)) {
        return StorageError::invalid_slot_name;
    }
    if (!available_) {
        return StorageError::unavailable;
    }
    if (bytes.size() > budget_.maximum_slot_bytes) {
        return StorageError::too_large;
    }

    const std::vector<std::string> existing = slots();
    const bool replacing =
        std::find(existing.begin(), existing.end(), std::string(slot)) != existing.end();
    if (!replacing && existing.size() >= budget_.maximum_slots) {
        return StorageError::out_of_space;
    }
    std::size_t total = bytes.size();
    for (const std::string& name : existing) {
        if (name == slot) {
            continue;
        }
        std::error_code code;
        const std::uintmax_t size = fs::file_size(path_of(root_, name), code);
        if (!code) {
            total += static_cast<std::size_t>(size);
        }
    }
    if (total > budget_.maximum_total_bytes) {
        return StorageError::out_of_space;
    }

    // Staged, then renamed. Between these two lines the slot still holds the
    // save it held before; after the rename it holds all of the new one. There
    // is no moment at which it holds part of each.
    const fs::path final_path = path_of(root_, slot);
    fs::path staged = final_path;
    staged += ".tmp";
    if (!write_whole_file(staged, bytes)) {
        std::error_code discard;
        fs::remove(staged, discard);
        return StorageError::io_failure;
    }
    std::error_code code;
    fs::rename(staged, final_path, code);
    if (code) {
        std::error_code discard;
        fs::remove(staged, discard);
        return StorageError::io_failure;
    }
    return StorageError::none;
}

StorageRead FilesystemSlotStorage::read(std::string_view slot) const {
    StorageRead result;
    if (!is_valid_slot_name(slot)) {
        result.error = StorageError::invalid_slot_name;
        return result;
    }
    if (!available_) {
        result.error = StorageError::unavailable;
        return result;
    }
    const fs::path path = path_of(root_, slot);
    std::error_code code;
    if (!fs::is_regular_file(path, code)) {
        result.error = StorageError::not_found;
        return result;
    }
    result.error =
        read_whole_file(path, budget_.maximum_slot_bytes, result.bytes);
    return result;
}

bool FilesystemSlotStorage::contains(std::string_view slot) const {
    if (!is_valid_slot_name(slot) || !available_) {
        return false;
    }
    std::error_code code;
    return fs::is_regular_file(path_of(root_, slot), code);
}

StorageError FilesystemSlotStorage::erase(std::string_view slot) {
    if (!is_valid_slot_name(slot)) {
        return StorageError::invalid_slot_name;
    }
    if (!available_) {
        return StorageError::unavailable;
    }
    const fs::path path = path_of(root_, slot);
    std::error_code code;
    if (!fs::is_regular_file(path, code)) {
        return StorageError::not_found;
    }
    return fs::remove(path, code) && !code ? StorageError::none
                                           : StorageError::io_failure;
}

std::vector<std::string> FilesystemSlotStorage::slots() const {
    std::vector<std::string> names;
    if (!available_) {
        return names;
    }
    std::error_code code;
    fs::directory_iterator entries(fs::path(root_), code);
    if (code) {
        return names;
    }
    for (const fs::directory_entry& entry : entries) {
        if (!entry.is_regular_file(code)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.size() <= file_suffix.size() ||
            name.compare(
                name.size() - file_suffix.size(), file_suffix.size(), file_suffix
            ) != 0) {
            continue;
        }
        // A staged write in progress, or a file somebody dropped in by hand.
        // The directory belongs to this adapter, but it is a directory: it says
        // what it recognises rather than assuming everything in it is a slot.
        const std::string stem = name.substr(0, name.size() - file_suffix.size());
        if (!is_valid_slot_name(stem)) {
            continue;
        }
        names.push_back(stem);
    }
    // The directory order is whatever the host gave; the interface promises
    // ascending.
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace grandleon::storage
