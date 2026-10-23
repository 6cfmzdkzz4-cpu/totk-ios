"""Synthetic archive/tree/animation regressions; no private game assets needed."""
from fractions import Fraction
from pathlib import Path
import struct
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[1]/"tools"
sys.path.insert(0, str(TOOLS))
from live_display_resources import (  # noqa: E402
    Polynomial, ResourceError, parse_animation, parse_layout, sarc_members,
    symbolic_vendor_rules,
)


def section(tag, payload):
    return tag+struct.pack("<I", len(payload)+8)+payload


def ui_file(magic, parts):
    body = b"".join(parts)
    return magic+b"\xff\xfe"+struct.pack("<HIIHH", 20, 0x09000000, len(body)+20, len(parts), 0)+body


def pane(name, tag=b"pan1"):
    encoded = name.encode("ascii")
    assert len(encoded) <= 24
    return section(tag, b"\x01\0\xff\0"+encoded.ljust(24, b"\0")+bytes(8)+struct.pack("<10f", 0, 0, 0, 0, 0, 0, 1, 1, 10, 10))


def animation():
    keys = struct.pack("<6f", 0, 0, -40, 1, -40, 0)
    entry = bytes([0, 1, 2, 0])+struct.pack("<HHI", 2, 0, 12)+keys
    tag = b"FLPA"+bytes([1, 0, 0, 0])+struct.pack("<I", 12)+entry
    content = b"MovingPane".ljust(28, b"\0")+bytes([1, 0, 0, 0])+struct.pack("<I", 36)+tag
    return ui_file(b"FLAN", [section(b"pai1", struct.pack("<HBBHHII", 1, 0, 0, 0, 1, 20, 24)+content)])


def sarc(names):
    identifiers = bytearray()
    nodes = []
    for index, name in enumerate(names):
        offset = len(identifiers)//4
        encoded = name.encode()+b"\0"
        identifiers.extend(encoded+bytes((-len(encoded)) % 4))
        nodes.append(struct.pack("<4I", 0, 0x01000000 | offset, index*4, index*4+4))
    body = b"SFAT"+struct.pack("<HHI", 12, len(names), 0x65)+b"".join(nodes)+b"SFNT"+struct.pack("<HH", 8, 0)+identifiers
    start = 20+len(body)
    payload = b"test"*len(names)
    return b"SARC"+struct.pack("<HHIIHH", 20, 0xfeff, start+len(payload), start, 0x100, 0)+body+payload


class LiveResourceTests(unittest.TestCase):
    def test_tree_and_full_width_names(self):
        name = "A"*24
        value = ui_file(b"FLYT", [pane("RootPane"), section(b"pas1", b""), pane(name, b"ali1"), section(b"pae1", b"")])
        result = parse_layout(value)
        self.assertEqual([(p["name"], p["parent"], p["depth"]) for p in result], [("RootPane", -1, 0), (name, 0, 1)])
        self.assertEqual(result[1]["kind"], "ali1")

    def test_tree_rejects_missing_child_owner(self):
        with self.assertRaises(ResourceError):
            parse_layout(ui_file(b"FLYT", [section(b"pas1", b""), pane("RootPane")]))

    def test_tree_rejects_unclosed_children(self):
        with self.assertRaises(ResourceError):
            parse_layout(ui_file(b"FLYT", [pane("RootPane"), section(b"pas1", b""), pane("Child")]))

    def test_file_rejects_wrong_version_and_trailing_data(self):
        valid = ui_file(b"FLYT", [pane("RootPane")])
        wrong = bytearray(valid)
        struct.pack_into("<I", wrong, 8, 0x08000000)
        for value in [wrong, valid+b"extra"]:
            with self.assertRaises(ResourceError):parse_layout(value)

    def test_animation_values_tangents_and_frames_are_distinct(self):
        fields = parse_animation(animation())
        self.assertEqual({offset: f["component"] for offset, f in fields.items()}, {108: "value", 112: "tangent", 120: "value", 124: "tangent"})
        self.assertNotIn(104, fields)
        self.assertEqual(fields[112]["target"], 1)
        self.assertEqual(fields[112]["name"], "MovingPane")

    def test_animation_rejects_key_pointer_escape(self):
        bad = bytearray(animation())
        struct.pack_into("<I", bad, 100, 0xfffffffc)
        with self.assertRaises(ResourceError):parse_animation(bad)

    def test_sarc_member_validation(self):
        self.assertEqual(sarc_members(sarc(["blyt/a.bflyt"])), {"blyt/a.bflyt": b"test"})
        for names in [["../secret"], ["same", "same"]]:
            with self.assertRaises(ResourceError):sarc_members(sarc(names))

    def test_sarc_rejects_overlapping_members(self):
        value = bytearray(sarc(["one", "two"]))
        struct.pack_into("<II", value, 20+12+16+8, 0, 4)
        with self.assertRaises(ResourceError):sarc_members(value)

    def test_exact_symbolic_arithmetic(self):
        aspect = Polynomial(((1, Fraction(1)),))
        shifted = 540*((Fraction(16, 9)/aspect)-1)
        self.assertEqual(shifted.evaluate(Fraction(4, 3)), 180)
        self.assertEqual((aspect*shifted).evaluate(Fraction(4, 3)), 240)
        self.assertEqual(Polynomial.make(0), -Polynomial.make(0))
        with self.assertRaises(ResourceError):_ = aspect/(aspect+1)

    def test_upstream_rules_are_interpreted_without_io(self):
        source = (TOOLS.parent/"vendor/totk-ultrawide/romfs_script.py").read_text()
        rules = symbolic_vendor_rules(source, ["PlayerStatus_00"], wide=False)
        root = next(row[-1] for row in rules if row[:4] == ("patch_blyt", "PlayerStatus_00", "RootPane", "scale_y"))
        self.assertEqual(root.evaluate(Fraction(4, 3)), Fraction(3, 4))
        movement = next(row[-1] for row in rules if row[:3] == ("patch_anim", "AppMap_00_MiniMap", Fraction(1816)))
        self.assertEqual(movement.evaluate(Fraction(4, 3)), -540)
        with self.assertRaises(ResourceError):
            symbolic_vendor_rules("def patch_romfs(a,b,c):\n    dangerous_call()\n", [], False)


if __name__ == "__main__":
    unittest.main()
