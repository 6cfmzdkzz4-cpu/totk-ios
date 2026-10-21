"""Feedback generation format and source gates, using synthetic bytes only."""

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import feedback_common as common  # noqa: E402
import prepare_feedback  # noqa: E402
from prepare_haptic_cues import classify  # noqa: E402


def string_table(values):
    payload = bytearray()
    offsets = []
    header_size = 4 + 4 * (len(values) + 1)
    for value in values:
        offsets.append(header_size + len(payload))
        payload += value.encode() + b"\0"
    offsets.append(header_size + len(payload))
    return (struct.pack("<I", 0xC2 | len(values) << 8) +
            struct.pack(f"<{len(offsets)}I", *offsets) + payload)


def eventsetting_fixture(rows):
    keys = ["__RowId", "FlowchartFileName", "EntryPointName", "EventSettingComponentList"]
    strings = sorted({value for row in rows for value in row.values()})
    key_blob, string_blob = string_table(keys), string_table(strings)
    string_index = {value: index for index, value in enumerate(strings)}
    dictionaries = []
    for row in rows:
        entries = []
        for key_index, key in enumerate(keys):
            if key not in row:
                continue
            entries.append(key_index.to_bytes(3, "little") + b"\xa0" +
                           struct.pack("<I", string_index[row[key]]))
        dictionaries.append(struct.pack("<I", 0xC1 | len(entries) << 8) + b"".join(entries))
    key_at = 16
    string_at = key_at + len(key_blob)
    root_at = string_at + len(string_blob)
    type_bytes = bytes([0xC1]) * len(rows)
    root_prefix = struct.pack("<I", 0xC0 | len(rows) << 8) + type_bytes
    root_prefix += bytes((-len(root_prefix)) & 3)
    dictionaries_at = root_at + len(root_prefix) + len(rows) * 4
    offsets, cursor = [], dictionaries_at
    for dictionary in dictionaries:
        offsets.append(cursor)
        cursor += len(dictionary)
    root = root_prefix + struct.pack(f"<{len(offsets)}I", *offsets)
    header = b"YB" + struct.pack("<HIII", 7, key_at, string_at, root_at)
    return header + key_blob + string_blob + root + b"".join(dictionaries)


def bars_fixture(name="Variable_Name"):
    amta, amta_size = 0x20, 0x60
    bwav = amta + amta_size
    total = bwav + 0x5C + 128
    data = bytearray(total)
    data[:4] = b"BARS"
    struct.pack_into("<I", data, 4, total)
    data[8:12] = b"\xff\xfe\x02\x01"
    struct.pack_into("<I", data, 12, 1)
    struct.pack_into("<I", data, 16, 0x12345678)  # name hash table
    struct.pack_into("<II", data, 20, amta, bwav)
    data[amta:amta + 8] = b"AMTA\xff\xfe\x00\x05"
    struct.pack_into("<I", data, amta + 8, amta_size)
    struct.pack_into("<I", data, amta + 0x24, 0x10)  # name begins at +0x34, not legacy +0x4c
    data[amta + 0x34:amta + 0x34 + len(name) + 1] = name.encode() + b"\0"
    data[bwav:bwav + 8] = b"BWAV\xff\xfe\x01\x00"
    struct.pack_into("<H", data, bwav + 14, 1)
    struct.pack_into("<HHIII", data, bwav + 16, 1, 0, 48000, 224, 224)
    struct.pack_into("<I", data, bwav + 16 + 0x34, 0x5C)
    return bytes(data)


class FeedbackGeneration(unittest.TestCase):
    def test_eventsetting_projection_uses_structured_byml_and_empty_defaults(self):
        rows = [
            {"__RowId": "Talk_A", "FlowchartFileName": "Work/Event/EventFlow/Npc_A.evfc",
             "EntryPointName": "Talk", "EventSettingComponentList": "Work/NPCTalk.gyml"},
            {"__RowId": "Near_A", "FlowchartFileName": "Work/Event/EventFlow/Npc_A.evfc",
             "EventSettingComponentList": "Work/NearObstacle.gyml"},
        ]
        parsed = common.eventsetting_rows(eventsetting_fixture(rows))
        self.assertEqual(parsed[0], rows[0])
        self.assertEqual(parsed[1]["EntryPointName"], "")
        self.assertEqual(parsed[1]["__RowId"], "Near_A")

    def test_eventsetting_rejects_an_unverified_schema_version(self):
        data = bytearray(eventsetting_fixture([{
            "__RowId": "Talk_A", "EventSettingComponentList": "Work/NPCTalk.gyml"
        }]))
        struct.pack_into("<H", data, 2, 8)
        with self.assertRaisesRegex(common.FeedbackInputError, "BYML v7"):
            common.eventsetting_rows(bytes(data))

    def test_bars_name_uses_amta_relative_offset(self):
        members = list(common.bars_members(bars_fixture()))
        self.assertEqual([item[0] for item in members], ["Variable_Name"])
        self.assertEqual(members[0][2][0]["rate"], 48000)

    def test_wrong_romfs_identity_fails_before_resource_reads(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "romfs.bin").write_bytes(b"")
            (root / "index.json").write_text(json.dumps({
                "status": "pass", "romfs_sha256": "0" * 64, "romfs_size": 0, "files": []
            }))
            with self.assertRaisesRegex(common.FeedbackInputError, "base 1.0.0"):
                common.VerifiedRomFS(root)

    def test_runtime_categories_remain_source_bounded(self):
        self.assertEqual(classify("Sys_GameStart_Renewal")[:2], ("START", 3))
        self.assertEqual(classify("NSword_Charge_Lv1")[:2], ("CHARGE", 3))
        self.assertEqual(classify("Spear_Whoosh_Sharp_S_00")[:2], ("THRUST", 3))
        self.assertIsNone(classify("Enemy_Whoosh_Sharp_S_00"))

    def test_all_runtime_headers_are_hash_locked(self):
        self.assertEqual(set(prepare_feedback.EXPECTED_HEADER_SHA256), {
            "gameplay_audio_fingerprints.h", "gameplay_extra_audio.h",
            "gameplay_haptic_patterns.h", "gameplay_dialogue_flows.h",
        })
        self.assertTrue(all(len(value) == 64 for value in prepare_feedback.EXPECTED_HEADER_SHA256.values()))
        requirement = (ROOT / "tools/requirements-feedback.txt").read_text()
        self.assertIn(common.EVFL_COMMIT, requirement)


if __name__ == "__main__":
    unittest.main()
