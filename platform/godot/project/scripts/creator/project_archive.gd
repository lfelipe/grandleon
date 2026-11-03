# SPDX-License-Identifier: MIT
class_name GrandleonProjectArchive
extends RefCounted

const ARCHIVE_FORMAT := "grandleon-project"
const ARCHIVE_VERSION := 1
const MAXIMUM_ARCHIVE_BYTES := 64 * 1024 * 1024
const MAXIMUM_ENTRIES := 65535
const MAXIMUM_ENTRY_BYTES := 32 * 1024 * 1024
const MAXIMUM_TOTAL_BYTES := 64 * 1024 * 1024
const MAXIMUM_PATH_BYTES := 512
const FORMAT_FILE := "grandleon.json"
const PROJECT_FILE := "project.json"
const ZIP_LOCAL_SIGNATURE := 0x04034b50
const ZIP_CENTRAL_SIGNATURE := 0x02014b50
const ZIP_END_SIGNATURE := 0x06054b50
const ZIP_UTF8_FLAG := 0x0800
const ZIP_VERSION := 20
const ZIP_DOS_DATE_1980 := 0x0021
const FORBIDDEN_NAMES := [
	"graphics.local.json",
	"controls.local.json",
	"project.local.json",
]
const WINDOWS_RESERVED_STEMS := [
	"aux",
	"clock$",
	"con",
	"nul",
	"prn",
	"com1",
	"com2",
	"com3",
	"com4",
	"com5",
	"com6",
	"com7",
	"com8",
	"com9",
	"lpt1",
	"lpt2",
	"lpt3",
	"lpt4",
	"lpt5",
	"lpt6",
	"lpt7",
	"lpt8",
	"lpt9",
]

var _crc_table: Array[int] = _build_crc_table()


func write_archive(path: String, source: String, asset_root: String) -> Dictionary:
	var collected := _collect_assets(asset_root)
	if not collected.ok:
		return collected
	var entries: Array[Dictionary] = [
		{
			"path": FORMAT_FILE,
			"bytes": (
				JSON.stringify({"format": ARCHIVE_FORMAT, "version": ARCHIVE_VERSION}, "  ", true)
				+ "\n"
			).to_utf8_buffer(),
		},
		{"path": PROJECT_FILE, "bytes": source.to_utf8_buffer()},
	]
	entries.append_array(collected.entries)
	entries.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
		return str(left.path) < str(right.path)
	)
	var total := 0
	for entry: Dictionary in entries:
		var bytes: PackedByteArray = entry.bytes
		if bytes.size() > MAXIMUM_ENTRY_BYTES:
			return _failure("archive_limit_exceeded", "A project file exceeds the portable archive limit.")
		total += bytes.size()
		if total > MAXIMUM_TOTAL_BYTES:
			return _failure("archive_limit_exceeded", "The project exceeds the portable archive limit.")

	var written := _write_stored_zip(path, entries)
	if not written.ok:
		return written
	return {"ok": true, "entries": entries.size(), "bytes": total}


func read_archive(path: String) -> Dictionary:
	var archive_file := FileAccess.open(path, FileAccess.READ)
	if archive_file == null:
		return _failure("archive_missing", "The selected archive could not be opened.")
	var archive_bytes := archive_file.get_length()
	archive_file.close()
	if archive_bytes > MAXIMUM_ARCHIVE_BYTES:
		return _failure("archive_limit_exceeded", "The selected archive exceeds the portable size limit.")
	var preflight := _validate_stored_zip(path)
	if not preflight.ok:
		return preflight

	var reader := ZIPReader.new()
	if reader.open(path) != OK:
		return _failure("archive_invalid", "The selected file is not a readable ZIP archive.")
	var files := reader.get_files()
	if files.is_empty() or files.size() > MAXIMUM_ENTRIES:
		reader.close()
		return _failure("archive_limit_exceeded", "The archive has an invalid number of entries.")
	var seen: Dictionary = {}
	var total := 0
	var project_bytes := PackedByteArray()
	var format_bytes := PackedByteArray()
	var assets: Dictionary = {}
	for value: String in files:
		var archive_path := value.replace("\\", "/")
		if not _valid_archive_path(archive_path):
			reader.close()
			return _failure("archive_path_invalid", "The archive contains a non-portable path.")
		var comparison := archive_path.to_lower()
		if seen.has(comparison):
			reader.close()
			return _failure("archive_duplicate_path", "The archive contains a duplicate path.")
		seen[comparison] = true
		if archive_path.ends_with("/"):
			continue
		var bytes := reader.read_file(archive_path)
		var expected: Dictionary = preflight.entries.get(archive_path, {})
		if (
			expected.is_empty()
			or bytes.size() != int(expected.size)
			or _crc32(bytes) != int(expected.crc)
		):
			reader.close()
			return _failure("archive_integrity_failed", "A project file failed archive integrity validation.")
		if bytes.size() > MAXIMUM_ENTRY_BYTES:
			reader.close()
			return _failure("archive_limit_exceeded", "A project file exceeds the portable archive limit.")
		total += bytes.size()
		if total > MAXIMUM_TOTAL_BYTES:
			reader.close()
			return _failure("archive_limit_exceeded", "The expanded project exceeds the portable archive limit.")
		if archive_path == PROJECT_FILE:
			project_bytes = bytes
		elif archive_path == FORMAT_FILE:
			format_bytes = bytes
		elif archive_path.begins_with("assets/"):
			assets[archive_path] = bytes
	reader.close()
	if project_bytes.is_empty():
		return _failure("archive_project_missing", "The archive does not contain project.json.")
	if not format_bytes.is_empty():
		var marker = JSON.parse_string(format_bytes.get_string_from_utf8())
		if (
			not marker is Dictionary
			or str(marker.get("format", "")) != ARCHIVE_FORMAT
			or int(marker.get("version", 0)) != ARCHIVE_VERSION
		):
			return _failure("archive_version_unsupported", "The archive format version is not supported.")
	return {
		"ok": true,
		"source": project_bytes.get_string_from_utf8(),
		"assets": assets,
		"entries": files.size(),
		"bytes": total,
	}


func _collect_assets(asset_root: String) -> Dictionary:
	var entries: Array[Dictionary] = []
	if not DirAccess.dir_exists_absolute(asset_root):
		return {"ok": true, "entries": entries}
	var pending: Array[String] = [""]
	while not pending.is_empty():
		var relative_directory: String = pending.pop_back()
		var absolute_directory := asset_root
		if not relative_directory.is_empty():
			absolute_directory = absolute_directory.path_join(relative_directory)
		var directory := DirAccess.open(absolute_directory)
		if directory == null:
			return _failure("asset_read_failed", "A project asset directory could not be read.")
		directory.set_include_hidden(true)
		var directories := directory.get_directories()
		directories.sort()
		for index in range(directories.size() - 1, -1, -1):
			var child_directory := str(directories[index])
			if directory.is_link(child_directory):
				return _failure("asset_path_invalid", "Project archives cannot follow asset links.")
			var child_relative := child_directory
			if not relative_directory.is_empty():
				child_relative = relative_directory.path_join(child_directory)
			if not _valid_archive_path("assets/" + child_relative + "/"):
				return _failure("asset_path_invalid", "A project asset uses a non-portable path.")
			pending.append(child_relative)
		var files := directory.get_files()
		files.sort()
		for value: String in files:
			if directory.is_link(value):
				return _failure("asset_path_invalid", "Project archives cannot follow asset links.")
			var relative_file := value
			if not relative_directory.is_empty():
				relative_file = relative_directory.path_join(value)
			var archive_path := "assets/" + relative_file.replace("\\", "/")
			if not _valid_archive_path(archive_path):
				return _failure("asset_path_invalid", "A project asset uses a non-portable path.")
			var bytes := FileAccess.get_file_as_bytes(absolute_directory.path_join(value))
			entries.append({"path": archive_path, "bytes": bytes})
	if entries.size() + 2 > MAXIMUM_ENTRIES:
		return _failure("archive_limit_exceeded", "The project has too many portable files.")
	return {"ok": true, "entries": entries}


func _valid_archive_path(path: String) -> bool:
	if path.is_empty() or path.begins_with("/") or path.contains("\\"):
		return false
	if path.to_utf8_buffer().size() > MAXIMUM_PATH_BYTES:
		return false
	var directory_entry := path.ends_with("/")
	var comparable := path.trim_suffix("/") if directory_entry else path
	var components := comparable.split("/", false)
	if components.is_empty():
		return false
	for component: String in components:
		if not _valid_portable_component(component):
			return false
		if component.to_lower() in FORBIDDEN_NAMES:
			return false
	if comparable in [PROJECT_FILE, FORMAT_FILE, "manifest.json"]:
		return not directory_entry
	if comparable.begins_with("assets/"):
		return components.size() >= 2
	if comparable.begins_with("recovered/"):
		return components.size() >= 2
	return false


func _valid_portable_component(component: String) -> bool:
	if (
		component.is_empty()
		or component == "."
		or component == ".."
		or component.begins_with(".")
		or component.ends_with(".")
		or component.ends_with(" ")
	):
		return false
	for character in ['<', '>', ':', '"', '/', '\\', '|', '?', '*']:
		if component.contains(character):
			return false
	for index in component.length():
		if component.unicode_at(index) < 32:
			return false
	var stem := component.get_basename().to_lower()
	return stem not in WINDOWS_RESERVED_STEMS


func _write_stored_zip(path: String, entries: Array[Dictionary]) -> Dictionary:
	# The earlier editor deliberately accepts only Store entries so it can apply
	# expansion limits before allocating output. This compact classic-ZIP writer
	# keeps the native creator interoperable without relying on a compressor.
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return _failure("archive_open_failed", "The portable archive could not be created.")
	file.big_endian = false
	var encoded: Array[Dictionary] = []
	for entry: Dictionary in entries:
		var name := str(entry.path).to_utf8_buffer()
		var bytes: PackedByteArray = entry.bytes
		var record := {
			"name": name,
			"bytes": bytes,
			"crc": _crc32(bytes),
			"offset": file.get_position(),
		}
		encoded.append(record)
		file.store_32(ZIP_LOCAL_SIGNATURE)
		file.store_16(ZIP_VERSION)
		file.store_16(ZIP_UTF8_FLAG)
		file.store_16(0)
		file.store_16(0)
		file.store_16(ZIP_DOS_DATE_1980)
		file.store_32(record.crc)
		file.store_32(bytes.size())
		file.store_32(bytes.size())
		file.store_16(name.size())
		file.store_16(0)
		file.store_buffer(name)
		file.store_buffer(bytes)
	var central_offset := file.get_position()
	for record: Dictionary in encoded:
		var name: PackedByteArray = record.name
		var bytes: PackedByteArray = record.bytes
		file.store_32(ZIP_CENTRAL_SIGNATURE)
		file.store_16(ZIP_VERSION)
		file.store_16(ZIP_VERSION)
		file.store_16(ZIP_UTF8_FLAG)
		file.store_16(0)
		file.store_16(0)
		file.store_16(ZIP_DOS_DATE_1980)
		file.store_32(record.crc)
		file.store_32(bytes.size())
		file.store_32(bytes.size())
		file.store_16(name.size())
		file.store_16(0)
		file.store_16(0)
		file.store_16(0)
		file.store_16(0)
		file.store_32(0)
		file.store_32(record.offset)
		file.store_buffer(name)
	var central_size := file.get_position() - central_offset
	file.store_32(ZIP_END_SIGNATURE)
	file.store_16(0)
	file.store_16(0)
	file.store_16(encoded.size())
	file.store_16(encoded.size())
	file.store_32(central_size)
	file.store_32(central_offset)
	file.store_16(0)
	var archive_size := file.get_position()
	file.flush()
	var error := file.get_error()
	file.close()
	if error != OK:
		DirAccess.remove_absolute(path)
		return _failure("archive_write_failed", "The portable archive could not be finalized.")
	if archive_size > MAXIMUM_ARCHIVE_BYTES:
		DirAccess.remove_absolute(path)
		return _failure("archive_limit_exceeded", "The portable archive exceeds the size limit.")
	return {"ok": true}


func _validate_stored_zip(path: String) -> Dictionary:
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.size() < 22 or bytes.size() > MAXIMUM_ARCHIVE_BYTES:
		return _failure("archive_invalid", "The selected archive is truncated or too large.")
	var end_offset := bytes.size() - 22
	if (
		_u32(bytes, end_offset) != ZIP_END_SIGNATURE
		or _u16(bytes, end_offset + 4) != 0
		or _u16(bytes, end_offset + 6) != 0
		or _u16(bytes, end_offset + 20) != 0
	):
		return _failure("archive_invalid", "The archive must be one disk with no trailing comment.")
	var disk_entries := _u16(bytes, end_offset + 8)
	var entry_count := _u16(bytes, end_offset + 10)
	var central_size := _u32(bytes, end_offset + 12)
	var central_offset := _u32(bytes, end_offset + 16)
	if (
		entry_count == 0
		or entry_count != disk_entries
		or entry_count > MAXIMUM_ENTRIES
		or central_offset + central_size != end_offset
		or central_offset < 0
		or central_size < 0
	):
		return _failure("archive_invalid", "The archive central directory is inconsistent.")
	var cursor := central_offset
	var entries: Dictionary = {}
	var total := 0
	for index in entry_count:
		if cursor + 46 > end_offset or _u32(bytes, cursor) != ZIP_CENTRAL_SIGNATURE:
			return _failure("archive_invalid", "The archive central directory is truncated.")
		var flags := _u16(bytes, cursor + 8)
		var method := _u16(bytes, cursor + 10)
		var crc := _u32(bytes, cursor + 16)
		var compressed_size := _u32(bytes, cursor + 20)
		var expanded_size := _u32(bytes, cursor + 24)
		var name_size := _u16(bytes, cursor + 28)
		var extra_size := _u16(bytes, cursor + 30)
		var comment_size := _u16(bytes, cursor + 32)
		var disk := _u16(bytes, cursor + 34)
		var local_offset := _u32(bytes, cursor + 42)
		var record_size := 46 + name_size + extra_size + comment_size
		if (
			cursor + record_size > end_offset
			or disk != 0
			or (flags & ~ZIP_UTF8_FLAG) != 0
			or method != 0
			or compressed_size != expanded_size
			or expanded_size > MAXIMUM_ENTRY_BYTES
			or name_size == 0
			or name_size > MAXIMUM_PATH_BYTES
		):
			return _failure("archive_compression_unsupported", "The archive uses unsupported ZIP features.")
		var name_bytes := bytes.slice(cursor + 46, cursor + 46 + name_size)
		var archive_path := name_bytes.get_string_from_utf8()
		if archive_path.to_utf8_buffer() != name_bytes:
			return _failure("archive_path_invalid", "An archive path is not valid UTF-8.")
		if entries.has(archive_path.to_lower()):
			return _failure("archive_duplicate_path", "The archive contains a duplicate path.")
		if local_offset + 30 > central_offset or _u32(bytes, local_offset) != ZIP_LOCAL_SIGNATURE:
			return _failure("archive_invalid", "An archive file header is missing.")
		var local_flags := _u16(bytes, local_offset + 6)
		var local_method := _u16(bytes, local_offset + 8)
		var local_crc := _u32(bytes, local_offset + 14)
		var local_compressed := _u32(bytes, local_offset + 18)
		var local_expanded := _u32(bytes, local_offset + 22)
		var local_name_size := _u16(bytes, local_offset + 26)
		var local_extra_size := _u16(bytes, local_offset + 28)
		var data_offset := local_offset + 30 + local_name_size + local_extra_size
		if (
			local_flags != flags
			or local_method != method
			or local_crc != crc
			or local_compressed != compressed_size
			or local_expanded != expanded_size
			or local_name_size != name_size
			or data_offset + compressed_size > central_offset
			or bytes.slice(local_offset + 30, local_offset + 30 + local_name_size) != name_bytes
		):
			return _failure("archive_integrity_failed", "An archive file header is inconsistent.")
		total += expanded_size
		if total > MAXIMUM_TOTAL_BYTES:
			return _failure("archive_limit_exceeded", "The expanded project exceeds the portable archive limit.")
		entries[archive_path.to_lower()] = {
			"path": archive_path,
			"size": expanded_size,
			"crc": crc,
		}
		cursor += record_size
	if cursor != end_offset:
		return _failure("archive_invalid", "The archive central directory has trailing data.")
	var by_path: Dictionary = {}
	for value: Dictionary in entries.values():
		by_path[str(value.path)] = value
	return {"ok": true, "entries": by_path, "bytes": total}


func _u16(bytes: PackedByteArray, offset: int) -> int:
	return int(bytes[offset]) | (int(bytes[offset + 1]) << 8)


func _u32(bytes: PackedByteArray, offset: int) -> int:
	return (
		int(bytes[offset])
		| (int(bytes[offset + 1]) << 8)
		| (int(bytes[offset + 2]) << 16)
		| (int(bytes[offset + 3]) << 24)
	) & 0xffffffff


func _crc32(bytes: PackedByteArray) -> int:
	var value := 0xffffffff
	for byte: int in bytes:
		value = _crc_table[(value ^ byte) & 0xff] ^ (value >> 8)
	return (value ^ 0xffffffff) & 0xffffffff


func _build_crc_table() -> Array[int]:
	var table: Array[int] = []
	for index in 256:
		var value := index
		for bit in 8:
			value = 0xedb88320 ^ (value >> 1) if value & 1 else value >> 1
		table.append(value & 0xffffffff)
	return table


func _failure(code: String, message: String) -> Dictionary:
	return {"ok": false, "code": code, "message": message}
