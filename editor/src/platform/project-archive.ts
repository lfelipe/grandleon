// SPDX-License-Identifier: MIT
import {
  normalizeProjectPath,
  ProjectStoreError,
  type ProjectFile,
  type ProjectSnapshot
} from "../domain/project-store";

const localSignature = 0x04034b50;
const centralSignature = 0x02014b50;
const endSignature = 0x06054b50;
const utf8Flag = 0x0800;
const storedMethod = 0;
const dosDate1980 = 0x0021;

export interface ProjectArchiveLimits {
  readonly maximumArchiveBytes: number;
  readonly maximumEntries: number;
  readonly maximumEntryBytes: number;
  readonly maximumTotalBytes: number;
  readonly maximumPathBytes: number;
}

export const defaultProjectArchiveLimits: ProjectArchiveLimits = {
  maximumArchiveBytes: 64 * 1024 * 1024,
  maximumEntries: 100_000,
  maximumEntryBytes: 32 * 1024 * 1024,
  maximumTotalBytes: 64 * 1024 * 1024,
  maximumPathBytes: 512
};

export type ProjectArchiveErrorCode =
  | "ARCHIVE_INVALID"
  | "ARCHIVE_UNSUPPORTED"
  | "ARCHIVE_LIMIT_EXCEEDED"
  | "ARCHIVE_DUPLICATE_PATH"
  | "ARCHIVE_INTEGRITY_FAILED";

export class ProjectArchiveError extends Error {
  constructor(
    readonly code: ProjectArchiveErrorCode,
    message: string
  ) {
    super(message);
    this.name = "ProjectArchiveError";
  }
}

function writeU16(view: DataView, offset: number, value: number) {
  view.setUint16(offset, value, true);
}

function writeU32(view: DataView, offset: number, value: number) {
  view.setUint32(offset, value, true);
}

function readU16(view: DataView, offset: number): number {
  return view.getUint16(offset, true);
}

function readU32(view: DataView, offset: number): number {
  return view.getUint32(offset, true);
}

const crcTable = new Uint32Array(256);
for (let index = 0; index < crcTable.length; ++index) {
  let value = index;
  for (let bit = 0; bit < 8; ++bit) {
    value = (value & 1) !== 0 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  }
  crcTable[index] = value >>> 0;
}

function crc32(bytes: Uint8Array): number {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value = crcTable[(value ^ byte) & 0xff]! ^ (value >>> 8);
  }
  return (value ^ 0xffffffff) >>> 0;
}

interface EncodedFile {
  path: string;
  name: Uint8Array;
  bytes: Uint8Array;
  crc: number;
  localOffset: number;
}

/** One file on its way into an archive. */
export interface ArchiveEntry {
  readonly path: string;
  readonly bytes: Uint8Array;
}

/**
 * A ZIP archive of these files, stored rather than compressed.
 *
 * Split out of `exportProjectArchive` below because the editor now hands out
 * two kinds of archive and neither is a special case of the other: a project's
 * source tree, and the pair of files a console build produces. What they share
 * is this container, and a second implementation of it would be a second place
 * a central directory offset can be wrong. The paths are taken as given; a
 * project's are normalised by the caller, because that normalisation is about
 * project paths rather than about ZIP.
 *
 * Stored and not deflated, for the reason the reader below gives: the editor
 * is the only producer this repository has to be able to read back, and a
 * container with no compressor in it is a container with no compressor to be
 * wrong.
 */
export function writeArchive(entries: readonly ArchiveEntry[]): Uint8Array {
  const encoder = new TextEncoder();
  const files: EncodedFile[] = [...entries]
    .map((entry) => ({
      path: entry.path,
      name: encoder.encode(entry.path),
      bytes: entry.bytes.slice(),
      crc: crc32(entry.bytes),
      localOffset: 0
    }))
    .sort((left, right) => left.path.localeCompare(right.path));

  for (let index = 1; index < files.length; ++index) {
    if (files[index - 1]!.path === files[index]!.path) {
      throw new ProjectArchiveError(
        "ARCHIVE_DUPLICATE_PATH",
        `duplicate archive path '${files[index]!.path}'`
      );
    }
  }

  const localBytes = files.reduce(
    (sum, file) => sum + 30 + file.name.length + file.bytes.length,
    0
  );
  const centralBytes = files.reduce(
    (sum, file) => sum + 46 + file.name.length,
    0
  );
  const output = new Uint8Array(localBytes + centralBytes + 22);
  const view = new DataView(output.buffer);
  let cursor = 0;

  for (const file of files) {
    file.localOffset = cursor;
    writeU32(view, cursor, localSignature);
    writeU16(view, cursor + 4, 20);
    writeU16(view, cursor + 6, utf8Flag);
    writeU16(view, cursor + 8, storedMethod);
    writeU16(view, cursor + 10, 0);
    writeU16(view, cursor + 12, dosDate1980);
    writeU32(view, cursor + 14, file.crc);
    writeU32(view, cursor + 18, file.bytes.length);
    writeU32(view, cursor + 22, file.bytes.length);
    writeU16(view, cursor + 26, file.name.length);
    writeU16(view, cursor + 28, 0);
    output.set(file.name, cursor + 30);
    output.set(file.bytes, cursor + 30 + file.name.length);
    cursor += 30 + file.name.length + file.bytes.length;
  }

  const centralOffset = cursor;
  for (const file of files) {
    writeU32(view, cursor, centralSignature);
    writeU16(view, cursor + 4, 20);
    writeU16(view, cursor + 6, 20);
    writeU16(view, cursor + 8, utf8Flag);
    writeU16(view, cursor + 10, storedMethod);
    writeU16(view, cursor + 12, 0);
    writeU16(view, cursor + 14, dosDate1980);
    writeU32(view, cursor + 16, file.crc);
    writeU32(view, cursor + 20, file.bytes.length);
    writeU32(view, cursor + 24, file.bytes.length);
    writeU16(view, cursor + 28, file.name.length);
    writeU16(view, cursor + 30, 0);
    writeU16(view, cursor + 32, 0);
    writeU16(view, cursor + 34, 0);
    writeU16(view, cursor + 36, 0);
    writeU32(view, cursor + 38, 0);
    writeU32(view, cursor + 42, file.localOffset);
    output.set(file.name, cursor + 46);
    cursor += 46 + file.name.length;
  }

  writeU32(view, cursor, endSignature);
  writeU16(view, cursor + 4, 0);
  writeU16(view, cursor + 6, 0);
  writeU16(view, cursor + 8, files.length);
  writeU16(view, cursor + 10, files.length);
  writeU32(view, cursor + 12, centralBytes);
  writeU32(view, cursor + 16, centralOffset);
  writeU16(view, cursor + 20, 0);
  return output;
}

export function exportProjectArchive(snapshot: ProjectSnapshot): Uint8Array {
  return writeArchive(snapshot.files.map((file) => ({
    path: normalizeProjectPath(file.path),
    bytes: file.bytes
  })));
}

function checkedEnd(offset: number, length: number, maximum: number): number {
  const end = offset + length;
  if (!Number.isSafeInteger(end) || offset < 0 || length < 0 || end > maximum) {
    throw new ProjectArchiveError("ARCHIVE_INVALID", "archive field is out of bounds");
  }
  return end;
}

export function readProjectArchive(
  archive: Uint8Array,
  limits: ProjectArchiveLimits = defaultProjectArchiveLimits
): ProjectSnapshot {
  if (archive.length > limits.maximumArchiveBytes) {
    throw new ProjectArchiveError(
      "ARCHIVE_LIMIT_EXCEEDED",
      `archive has ${archive.length} bytes; limit is ${limits.maximumArchiveBytes}`
    );
  }
  if (archive.length < 22) {
    throw new ProjectArchiveError("ARCHIVE_INVALID", "archive is truncated");
  }

  const view = new DataView(archive.buffer, archive.byteOffset, archive.byteLength);
  const endOffset = archive.length - 22;
  if (readU32(view, endOffset) !== endSignature || readU16(view, endOffset + 20) !== 0) {
    throw new ProjectArchiveError(
      "ARCHIVE_UNSUPPORTED",
      "archive must have one disk and no trailing comment"
    );
  }
  if (readU16(view, endOffset + 4) !== 0 || readU16(view, endOffset + 6) !== 0) {
    throw new ProjectArchiveError("ARCHIVE_UNSUPPORTED", "multi-disk ZIP is unsupported");
  }
  const entries = readU16(view, endOffset + 10);
  if (entries !== readU16(view, endOffset + 8) || entries > limits.maximumEntries) {
    throw new ProjectArchiveError(
      "ARCHIVE_LIMIT_EXCEEDED",
      "archive entry count is inconsistent or exceeds its limit"
    );
  }
  const centralSize = readU32(view, endOffset + 12);
  const centralOffset = readU32(view, endOffset + 16);
  if (checkedEnd(centralOffset, centralSize, archive.length) !== endOffset) {
    throw new ProjectArchiveError("ARCHIVE_INVALID", "central directory bounds are invalid");
  }

  const decoder = new TextDecoder("utf-8", { fatal: true });
  const seen = new Set<string>();
  const files: ProjectFile[] = [];
  let totalBytes = 0;
  let cursor = centralOffset;

  for (let index = 0; index < entries; ++index) {
    checkedEnd(cursor, 46, endOffset);
    if (readU32(view, cursor) !== centralSignature) {
      throw new ProjectArchiveError("ARCHIVE_INVALID", "central entry signature is invalid");
    }
    const flags = readU16(view, cursor + 8);
    const method = readU16(view, cursor + 10);
    const crc = readU32(view, cursor + 16);
    const compressedBytes = readU32(view, cursor + 20);
    const unpackedBytes = readU32(view, cursor + 24);
    const nameBytes = readU16(view, cursor + 28);
    const extraBytes = readU16(view, cursor + 30);
    const commentBytes = readU16(view, cursor + 32);
    const localOffset = readU32(view, cursor + 42);
    const entryEnd = checkedEnd(
      cursor,
      46 + nameBytes + extraBytes + commentBytes,
      endOffset
    );

    if (method !== storedMethod || compressedBytes !== unpackedBytes) {
      throw new ProjectArchiveError(
        "ARCHIVE_UNSUPPORTED",
        "this archive's entries are compressed, and only stored (uncompressed) " +
        "ZIP entries can be read here"
      );
    }
    // The UTF-8 flag declares how the *name* is encoded and nothing else. The
    // editor's own exporter sets it; almost nothing else does, because an
    // ASCII name needs no declaration, so an archive an author made with a
    // command-line zip is read rather than refused, as long as its names are
    // in fact ASCII. Every other flag bit is a feature this reader does not
    // implement (encryption, a streamed data descriptor) and is still refused.
    const asciiName = archive
      .subarray(cursor + 46, cursor + 46 + nameBytes)
      .every((byte) => byte < 0x80);
    if (flags !== utf8Flag && !(flags === 0 && asciiName)) {
      throw new ProjectArchiveError(
        "ARCHIVE_UNSUPPORTED",
        "only unencrypted ZIP entries with UTF-8 or ASCII names are supported"
      );
    }
    if (
      nameBytes === 0 ||
      nameBytes > limits.maximumPathBytes ||
      unpackedBytes > limits.maximumEntryBytes
    ) {
      throw new ProjectArchiveError(
        "ARCHIVE_LIMIT_EXCEEDED",
        "archive entry path or size exceeds its limit"
      );
    }
    totalBytes += unpackedBytes;
    if (totalBytes > limits.maximumTotalBytes) {
      throw new ProjectArchiveError(
        "ARCHIVE_LIMIT_EXCEEDED",
        "archive expanded size exceeds its limit"
      );
    }

    let path: string;
    try {
      path = normalizeProjectPath(
        decoder.decode(archive.subarray(cursor + 46, cursor + 46 + nameBytes))
      );
    } catch (error) {
      if (error instanceof ProjectStoreError) {
        throw new ProjectArchiveError("ARCHIVE_INVALID", error.message);
      }
      throw new ProjectArchiveError("ARCHIVE_INVALID", "archive path is not valid UTF-8");
    }
    if (seen.has(path)) {
      throw new ProjectArchiveError(
        "ARCHIVE_DUPLICATE_PATH",
        `archive contains duplicate path '${path}'`
      );
    }
    seen.add(path);

    checkedEnd(localOffset, 30, centralOffset);
    if (
      readU32(view, localOffset) !== localSignature ||
      readU16(view, localOffset + 6) !== flags ||
      readU16(view, localOffset + 8) !== method ||
      readU16(view, localOffset + 26) !== nameBytes
    ) {
      throw new ProjectArchiveError(
        "ARCHIVE_INVALID",
        `local entry for '${path}' does not match its directory`
      );
    }
    const localExtra = readU16(view, localOffset + 28);
    const dataOffset = checkedEnd(localOffset, 30 + nameBytes + localExtra, centralOffset);
    const dataEnd = checkedEnd(dataOffset, unpackedBytes, centralOffset);
    const bytes = archive.slice(dataOffset, dataEnd);
    if (crc32(bytes) !== crc) {
      throw new ProjectArchiveError(
        "ARCHIVE_INTEGRITY_FAILED",
        `archive content for '${path}' failed CRC validation`
      );
    }
    files.push({ path, bytes, revision: index + 1 });
    cursor = entryEnd;
  }

  if (cursor !== endOffset) {
    throw new ProjectArchiveError("ARCHIVE_INVALID", "central directory has trailing data");
  }
  files.sort((left, right) => left.path.localeCompare(right.path));
  return { revision: files.length, files };
}
