#!/usr/bin/env python3
"""Strict, source-only readers shared by the local feedback generators."""

from __future__ import annotations

import hashlib
import importlib
import json
from pathlib import Path
import struct
import sys


ROMFS_SHA256 = "4751542f544d8c3406cb63ccfc68b42690ff0387228d1f0af021d1f0037dafcf"
REWRITTEN_MAIN_SHA256 = "5553c4124bfff15094d4def00894ddccc3a48ee9a2cd4cea06ca8f8c48359ccc"
EVENTSETTING_PATH = "RSDB/EventSetting.Product.100.rstbl.byml.zs"
EVENTSETTING_COMPRESSED_SHA256 = "20796fdd55271c53afad9ff92e73bcd22148d2a95db4ed5df5543076c0990f07"
EVENTSETTING_DECOMPRESSED_SHA256 = "be1c1bfe49f078f5d256f2baaf42bd48e5359ae8573fcdec5b3e935bd97af9a6"
EVFL_REPOSITORY = "https://github.com/zeldamods/evfl"
EVFL_COMMIT = "e996fb18b389d584811f7653ca0646074b404d59"
EVFL_SOURCE_TREE_SHA256 = "e0e6a113269994fc0a700a955691243933728e9f565f5dfb1cfc483c354c64d4"
ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"


class FeedbackInputError(ValueError):
    """The supplied game data or parser is outside the verified input set."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1 << 20):
            digest.update(block)
    return digest.hexdigest()


def _checked(data: bytes, offset: int, size: int, what: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise FeedbackInputError(f"{what} is outside its container")
    return data[offset:offset + size]


def sarc_members(data: bytes) -> dict[str, bytes]:
    """Parse the subset of SARC used by the game's verified Zstandard dictionary pack."""
    if data[:4] != b"SARC" or len(data) < 20:
        raise FeedbackInputError("expected a SARC dictionary pack")
    endian = "<" if data[6:8] == b"\xff\xfe" else ">"
    header, declared_size, data_start = struct.unpack_from(endian + "HxxII", data, 4)
    if declared_size != len(data) or header < 20 or _checked(data, header, 4, "SARC SFAT") != b"SFAT":
        raise FeedbackInputError("unsupported SARC header")
    count = struct.unpack_from(endian + "H", data, header + 6)[0]
    names = header + 12 + count * 16
    if _checked(data, names, 4, "SARC SFNT") != b"SFNT":
        raise FeedbackInputError("missing SARC string table")
    names += 8
    result: dict[str, bytes] = {}
    for index in range(count):
        _, attribute, lo, hi = struct.unpack_from(
            endian + "4I", _checked(data, header + 12 + index * 16, 16, "SARC member")
        )
        name_at = names + (attribute & 0xFFFFFF) * 4
        if attribute >> 24 != 1 or not names <= name_at < data_start or hi < lo:
            raise FeedbackInputError("unsupported SARC member")
        try:
            name_end = data.index(0, name_at, data_start)
        except ValueError as exc:
            raise FeedbackInputError("unterminated SARC member name") from exc
        name = data[name_at:name_end].decode("utf-8")
        if (name.startswith("/") or "\\" in name or name in result or
                any(part in ("", ".", "..") for part in name.split("/"))):
            raise FeedbackInputError("unsafe or duplicate SARC member name")
        result[name] = _checked(data, data_start + lo, hi - lo, "SARC member data")
    return result


class VerifiedRomFS:
    """Bounded reader for the already verified base-1.0.0 RomFS image and index."""

    def __init__(self, root: Path | str):
        if not __debug__:
            raise FeedbackInputError("feedback generation must not run with Python -O")
        self.root = Path(root)
        index_path = self.root / "index.json"
        image_path = self.root / "romfs.bin"
        try:
            self.index = json.loads(index_path.read_text())
            image_size = image_path.stat().st_size
        except (OSError, ValueError) as exc:
            raise FeedbackInputError(f"expected verified RomFS index and image under {self.root}") from exc
        if self.index.get("status") != "pass":
            raise FeedbackInputError("RomFS index has not passed verification")
        if self.index.get("romfs_sha256") != ROMFS_SHA256:
            raise FeedbackInputError("feedback generation supports only verified base 1.0.0 RomFS")
        if self.index.get("romfs_size") != image_size:
            raise FeedbackInputError("RomFS image size does not match its verified index")
        rows = self.index.get("files")
        if not isinstance(rows, list):
            raise FeedbackInputError("RomFS index has no file table")
        self.entries: dict[str, dict] = {}
        for row in rows:
            if not isinstance(row, dict):
                raise FeedbackInputError("invalid RomFS file row")
            path, offset, size = row.get("path"), row.get("offset"), row.get("size")
            if (not isinstance(path, str) or path in self.entries or not isinstance(offset, int) or
                    not isinstance(size, int) or offset < 0 or size < 0 or offset > image_size or
                    size > image_size - offset):
                raise FeedbackInputError("invalid or duplicate RomFS file row")
            self.entries[path] = row
        self.image = image_path
        self._dictionaries = None

    def verify_rewritten_main(self) -> Path:
        """Bind runtime ABI-dependent tables to the exact verified rewritten base executable."""
        try:
            game_tree = self.root.resolve().parents[2]
        except IndexError as exc:
            raise FeedbackInputError("cannot derive game work tree from RomFS path") from exc
        main = game_tree / "rewritten/v0/main.patched.bin"
        try:
            actual = file_sha256(main)
        except OSError as exc:
            raise FeedbackInputError("feedback generation requires rewritten/v0/main.patched.bin") from exc
        if actual != REWRITTEN_MAIN_SHA256:
            raise FeedbackInputError("feedback generation supports only the verified base-1.0.0 rewritten main")
        return main

    def raw(self, path: str) -> bytes:
        try:
            row = self.entries[path]
        except KeyError as exc:
            raise FeedbackInputError(f"RomFS does not contain {path}") from exc
        with self.image.open("rb") as stream:
            stream.seek(row["offset"])
            data = stream.read(row["size"])
        if len(data) != row["size"]:
            raise FeedbackInputError(f"short RomFS read for {path}")
        return data

    @staticmethod
    def _zstd():
        if sys.version_info < (3, 14):
            raise FeedbackInputError("feedback generation requires Python 3.14 or newer")
        try:
            import compression.zstd as zstd
        except ImportError as exc:  # pragma: no cover - guarded by the version check
            raise FeedbackInputError("Python was built without compression.zstd") from exc
        return zstd

    def _load_dictionaries(self):
        zstd = self._zstd()
        packed = self.raw("Pack/ZsDic.pack.zs")
        info = zstd.get_frame_info(packed)
        if info.dictionary_id or info.decompressed_size is None or info.decompressed_size > 64 << 20:
            raise FeedbackInputError("unsupported Zstandard dictionary pack")
        members = sarc_members(zstd.decompress(packed))
        dictionaries = {}
        for name, data in members.items():
            dictionary = zstd.ZstdDict(data)
            if not dictionary.dict_id or dictionary.dict_id in dictionaries:
                raise FeedbackInputError(f"invalid or duplicate Zstandard dictionary {name}")
            dictionaries[dictionary.dict_id] = dictionary
        if not dictionaries:
            raise FeedbackInputError("empty Zstandard dictionary pack")
        self._dictionaries = dictionaries

    def decode(self, data: bytes, path: str = "resource") -> bytes:
        if data[:4] != ZSTD_MAGIC:
            return data
        zstd = self._zstd()
        info = zstd.get_frame_info(data)
        if info.decompressed_size is None or info.decompressed_size > 1 << 30:
            raise FeedbackInputError(f"unbounded Zstandard resource {path}")
        if self._dictionaries is None:
            self._load_dictionaries()
        dictionary = None
        if info.dictionary_id:
            try:
                dictionary = self._dictionaries[info.dictionary_id]
            except KeyError as exc:
                raise FeedbackInputError(f"unknown Zstandard dictionary for {path}") from exc
        result = zstd.decompress(data, zstd_dict=dictionary)
        if len(result) != info.decompressed_size:
            raise FeedbackInputError(f"wrong decompressed size for {path}")
        return result

    def read(self, path: str) -> bytes:
        return self.decode(self.raw(path), path)

    def read_checked(self, path: str, compressed_sha256: str, decompressed_sha256: str) -> bytes:
        raw = self.raw(path)
        if sha256(raw) != compressed_sha256:
            raise FeedbackInputError(f"unsupported source bytes for {path}")
        data = self.decode(raw, path)
        if sha256(data) != decompressed_sha256:
            raise FeedbackInputError(f"unsupported decoded bytes for {path}")
        return data


def channel_info(data: bytes, base: int = 0) -> list[dict]:
    if _checked(data, base, 16, "BWAV header")[:8] != b"BWAV\xff\xfe\x01\x00":
        raise FeedbackInputError("unsupported BWAV header")
    count = struct.unpack_from("<H", data, base + 14)[0]
    if not 1 <= count <= 8:
        raise FeedbackInputError("unsupported BWAV channel count")
    _checked(data, base + 16, count * 0x4C, "BWAV channel table")
    result = []
    for channel in range(count):
        offset = base + 16 + channel * 0x4C
        codec, layout, rate, full, stored = struct.unpack_from("<HHIII", data, offset)
        result.append({
            "channel": channel, "codec": codec, "layout": layout, "rate": rate,
            "full_samples": full, "stored_samples": stored,
            "offset": base + struct.unpack_from("<I", data, offset + 0x34)[0],
            "coefficients": struct.unpack_from("<16h", data, offset + 0x10),
            "history": struct.unpack_from("<2h", data, offset + 0x46),
        })
    return result


def bars_container(data: bytes) -> bytes:
    """Accept only BARS v1.2's declared container or its verified one-byte outer pad."""
    if data[:4] != b"BARS" or len(data) < 16:
        raise FeedbackInputError("expected BARS")
    declared = struct.unpack_from("<I", data, 4)[0]
    if len(data) == declared + 1 and data[-1:] == b"\0":
        data = data[:declared]
    if len(data) != declared or data[8:12] != b"\xff\xfe\x02\x01":
        raise FeedbackInputError("unsupported BARS container")
    return data


def bars_members(data: bytes):
    """Yield BARS v1.2 / AMTA v5 names and their structurally paired BWAVs."""
    data = bars_container(data)
    count = struct.unpack_from("<I", data, 12)[0]
    if not 0 < count < 65536 or 16 + count * 12 > len(data):
        raise FeedbackInputError("invalid BARS pair table")
    for index in range(count):
        amta, bwav = struct.unpack_from("<II", data, 16 + count * 4 + index * 8)
        if _checked(data, amta, 12, "AMTA header")[:8] != b"AMTA\xff\xfe\x00\x05":
            raise FeedbackInputError("unsupported AMTA header")
        size = struct.unpack_from("<I", data, amta + 8)[0]
        if size < 0x28 or amta + size > len(data):
            raise FeedbackInputError("invalid AMTA size")
        name_at = amta + 0x24 + struct.unpack_from("<I", data, amta + 0x24)[0]
        if not amta + 0x28 <= name_at < amta + size:
            raise FeedbackInputError("invalid AMTA name offset")
        try:
            name_end = data.index(0, name_at, amta + size)
        except ValueError as exc:
            raise FeedbackInputError("unterminated AMTA name") from exc
        try:
            name = data[name_at:name_end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise FeedbackInputError("non-ASCII AMTA name") from exc
        yield name, bwav, channel_info(data, bwav)


def _byml_string_table(data: bytes, offset: int, label: str) -> list[str]:
    header = struct.unpack_from("<I", _checked(data, offset, 4, label))[0]
    if header & 0xFF != 0xC2:
        raise FeedbackInputError(f"{label} is not a BYML string table")
    count = header >> 8
    raw_offsets = _checked(data, offset + 4, (count + 1) * 4, label + " offsets")
    offsets = struct.unpack_from(f"<{count + 1}I", raw_offsets)
    if list(offsets) != sorted(offsets) or offsets[-1] > len(data) - offset:
        raise FeedbackInputError(f"invalid {label} offsets")
    values = []
    for index in range(count):
        start, limit = offset + offsets[index], offset + offsets[index + 1]
        try:
            end = data.index(0, start, limit)
            values.append(data[start:end].decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise FeedbackInputError(f"invalid {label} string") from exc
    return values


def eventsetting_rows(data: bytes) -> list[dict[str, str]]:
    """Project the four runtime fields from the verified little-endian BYML v7 table."""
    if len(data) < 16 or data[:2] != b"YB" or struct.unpack_from("<H", data, 2)[0] != 7:
        raise FeedbackInputError("EventSetting is not little-endian BYML v7")
    key_offset, string_offset, root_offset = struct.unpack_from("<III", data, 4)
    keys = _byml_string_table(data, key_offset, "BYML key table")
    strings = _byml_string_table(data, string_offset, "BYML string table")
    root_header = struct.unpack_from("<I", _checked(data, root_offset, 4, "BYML root"))[0]
    if root_header & 0xFF != 0xC0:
        raise FeedbackInputError("EventSetting BYML root is not an array")
    count = root_header >> 8
    types = _checked(data, root_offset + 4, count, "BYML root types")
    if any(kind != 0xC1 for kind in types):
        raise FeedbackInputError("EventSetting BYML contains a non-dictionary row")
    values_at = root_offset + 4 + ((count + 3) & ~3)
    values = struct.unpack_from(f"<{count}I", _checked(data, values_at, count * 4, "BYML root values"))
    wanted = ("__RowId", "FlowchartFileName", "EntryPointName", "EventSettingComponentList")
    rows = []
    for row_offset in values:
        row_header = struct.unpack_from("<I", _checked(data, row_offset, 4, "BYML row"))[0]
        if row_header & 0xFF != 0xC1:
            raise FeedbackInputError("EventSetting row is not a dictionary")
        fields = {name: "" for name in wanted}
        seen = set()
        for index in range(row_header >> 8):
            entry = _checked(data, row_offset + 4 + index * 8, 8, "BYML row entry")
            key_index = int.from_bytes(entry[:3], "little")
            if key_index >= len(keys):
                raise FeedbackInputError("BYML key index is out of range")
            name = keys[key_index]
            if name not in fields:
                continue
            if name in seen or entry[3] != 0xA0:
                raise FeedbackInputError(f"unsupported or duplicate EventSetting field {name}")
            value_index = struct.unpack_from("<I", entry, 4)[0]
            if value_index >= len(strings):
                raise FeedbackInputError("BYML string index is out of range")
            fields[name] = strings[value_index]
            seen.add(name)
        if not fields["__RowId"] or not fields["EventSettingComponentList"]:
            raise FeedbackInputError("EventSetting row lacks its required identity fields")
        rows.append(fields)
    return rows


def evfl_tree_digest(package: Path) -> str:
    digest = hashlib.sha256()
    files = sorted(package.glob("*.py"))
    if not files:
        raise FeedbackInputError("EVFL package contains no Python sources")
    for path in files:
        digest.update(path.name.encode() + b"\0" + path.read_bytes() + b"\0")
    return digest.hexdigest()


def load_evfl(source: Path | str | None = None):
    """Import only the exact pinned EVFL source tree, installed or explicitly supplied."""
    if source is not None:
        source = Path(source).resolve()
        package = source / "evfl" if (source / "evfl").is_dir() else source
        if not package.is_dir():
            raise FeedbackInputError("--evfl-source must contain the evfl package")
        sys.path.insert(0, str(package.parent))
    importlib.invalidate_caches()
    try:
        evfl = importlib.import_module("evfl")
    except ImportError as exc:
        raise FeedbackInputError(
            "install tools/requirements-feedback.txt or pass --evfl-source at the pinned commit"
        ) from exc
    package = Path(evfl.__file__).resolve().parent
    actual = evfl_tree_digest(package)
    if actual != EVFL_SOURCE_TREE_SHA256:
        raise FeedbackInputError(
            f"unsupported EVFL parser source {actual}; expected commit {EVFL_COMMIT}"
        )
    return evfl, {
        "repository": EVFL_REPOSITORY,
        "commit": EVFL_COMMIT,
        "source_tree_sha256": actual,
    }
