#!/usr/bin/env python3
"""Prepare exact base-1.0.0 live UI metadata in a private work tree.

SPDX-License-Identifier: GPL-3.0-only
Only pane identifiers, tree structure and authored aspect formulas are emitted.
"""
from __future__ import annotations

import argparse
import compression.zstd as zstd
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct

from live_display_resources import (
    Polynomial, parse_animation, parse_layout, require, sarc_members, symbolic_vendor_rules,
)

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = "UI/LayoutArchive/Common.Product.100.Nin_NX_NVN.blarc.zs"
ARCHIVE_SHA256 = "81c2c5baabb92a18b789fed2892a80bdb1c3e359ce5a55ce63a650323d0f8481"
FIELDS = {"shift_x": 0x30, "shift_y": 0x34, "scale_x": 0x48, "scale_y": 0x4c}
SOURCE_FIELDS = {"shift_x": 0x20, "shift_y": 0x24, "scale_x": 0x38, "scale_y": 0x3c}
ANIMATION_FIELDS = {0: "shift_x", 1: "shift_y", 6: "scale_x", 7: "scale_y"}
BASELINE = Fraction(4, 3)


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def signature(panes: list[dict]) -> int:
    """Fingerprint authored names, types and topology, excluding animated floats."""
    value = 14695981039346656037
    for pane in panes:
        data = struct.pack("<I", pane["depth"])+pane["name"].encode()+b"\0"+pane["kind"].encode()
        for byte in data:
            value = ((value ^ byte)*1099511628211) & ((1 << 64)-1)
    return value


def prepare(tree: Path, output: Path) -> dict:
    """Verify source identity, parse the real archive and bind every supported rule."""
    tree = tree.resolve()
    vendor = ROOT/"vendor/totk-ultrawide"
    pins = {row["path"]: row["sha"] for row in json.loads((vendor/"tree.json").read_text())["tree"] if row["type"] == "blob"}
    source = vendor/"romfs_script.py"
    raw_source = source.read_bytes()
    require(hashlib.sha1(b"blob "+str(len(raw_source)).encode()+b"\0"+raw_source).hexdigest() == pins["romfs_script.py"], "upstream rule source identity")
    expected = json.loads((tree/"assets/expected-v1.0.0.json").read_text())
    require(digest(tree/"rewritten/v0/main.patched.bin") == expected["rewritten_sha256"]["main.patched.bin"], "rewritten main identity")
    romfs = tree/"extracted/v0/romfs"
    index = json.loads((romfs/"index.json").read_text())
    require(index["status"] == "pass" and index["romfs_sha256"] == expected["romfs"]["sha256"], "verified RomFS index identity")
    require((romfs/"romfs.bin").stat().st_size == expected["romfs"]["size"] and digest(romfs/"romfs.bin") == expected["romfs"]["sha256"], "RomFS byte identity")
    files = {row["path"]: row for row in index["files"]}
    with (romfs/"romfs.bin").open("rb") as game:
        item = files["Pack/ZsDic.pack.zs"]
        game.seek(item["offset"])
        dictionaries = {dictionary.dict_id: dictionary for value in sarc_members(zstd.decompress(game.read(item["size"]))).values()
                        for dictionary in [zstd.ZstdDict(value)]}
        item = files[ARCHIVE]
        game.seek(item["offset"])
        compressed = game.read(item["size"])
    require(hashlib.sha256(compressed).hexdigest() == ARCHIVE_SHA256, "exact Common archive identity")
    info = zstd.get_frame_info(compressed)
    members = sarc_members(zstd.decompress(compressed, zstd_dict=dictionaries[info.dictionary_id]))
    layouts = {name[5:-6]: parse_layout(data) for name, data in members.items() if name.startswith("blyt/") and name.endswith(".bflyt")}
    baseline_layouts = {name: bytearray(members["blyt/"+name+".bflyt"]) for name in layouts}
    overlay = tree/"extracted/v0/romfs-patches-43"/ARCHIVE
    overlay_bytes = overlay.read_bytes()
    overlay_manifest = dict((parts[0], parts) for line in (overlay.parents[2]/"manifest.tsv").read_text().splitlines() if len(parts := line.split("\t")) >= 3)
    overlay_row = overlay_manifest[ARCHIVE]
    require(len(overlay_bytes) == int(overlay_row[1]) and hashlib.sha256(overlay_bytes).hexdigest() == overlay_row[2], "prepared baseline overlay manifest identity")
    overlay_info = zstd.get_frame_info(overlay_bytes)
    prepared_members = sarc_members(zstd.decompress(overlay_bytes, zstd_dict=dictionaries[overlay_info.dictionary_id]))
    rules: dict[str, dict[tuple[int, str], tuple[Polynomial, Polynomial]]] = {name: {} for name in layouts}
    animations: dict[tuple[str, int], list[tuple]] = {}
    unsupported: list[dict] = []
    for call, *arguments in symbolic_vendor_rules(raw_source.decode(), sorted(layouts), wide=False):
        if call == "patch_blyt":
            name, pane, field, polynomial = arguments
            data = members["blyt/"+name+".bflyt"]
            start = data.index(b"RootPane")
            at = data.index(pane.encode(), start)
            matches = [i for i, p in enumerate(layouts[name]) if p["offset"]+12 == at]
            require(len(matches) == 1, "vendor pane identifier does not bind a unique section start")
            pane_index = matches[0]
            expected_offset = layouts[name][pane_index]["offset"] + {"shift_x": 44, "shift_y": 48, "scale_x": 68, "scale_y": 72}[field]
            require(at+SOURCE_FIELDS[field] == expected_offset, "vendor pane field offset")
            struct.pack_into("<f", baseline_layouts[name], expected_offset, float(polynomial.evaluate(BASELINE)))
            if field.startswith("scale"):
                transform = polynomial/polynomial.evaluate(BASELINE), Polynomial.make(0)
            else:
                transform = Polynomial.make(1), polynomial-polynomial.evaluate(BASELINE)
            rules[name][pane_index, field] = transform
        else:
            name, hex_offset, polynomial = arguments
            require(hex_offset.denominator == 1 and int(hex_offset) % 2 == 0, "vendor animation hex offset")
            offset = int(hex_offset)//2
            animation = parse_animation(members["anim/"+name+".bflan"])
            require(offset in animation, "vendor animation offset is not a float key value")
            entry = animation[offset]
            animations.setdefault((name, entry["track"]), []).append((offset, entry, polynomial))
    for (name, track), rows in animations.items():
        first = rows[0][1]
        owners = [layout for layout in layouts if name.startswith(layout+"_")]
        require(owners, "animation has no receiving layout")
        owner = max(owners, key=len)
        if first["content_kind"] != 0 or first["tag"] != "FLPA" or first["target"] not in ANIMATION_FIELDS:
            unsupported.append(dict(animation=name, pane_or_material=first["name"], tag=first["tag"], target=first["target"], reason="material/UV tracks require their own exact animation hook"))
            continue
        field = ANIMATION_FIELDS[first["target"]]
        matches = [i for i, pane in enumerate(layouts[owner]) if pane["name"] == first["name"]]
        require(len(matches) == 1, "animated pane identity")
        key = matches[0], field
        data = members["anim/"+name+".bflan"]
        track_fields = parse_animation(data)
        changes = {offset: polynomial for offset, _, polynomial in rows}
        components = [(spec["component"], changes.get(offset, Polynomial.make(struct.unpack_from("<f", data, offset)[0])))
                      for offset, spec in track_fields.items() if spec["track"] == track]
        values = [polynomial for component, polynomial in components if component == "value"]
        tangents = [polynomial for component, polynomial in components if component == "tangent"]
        multiplier = None
        for polynomial in tangents:
            baseline = polynomial.evaluate(BASELINE)
            if baseline:
                multiplier = polynomial/baseline
                break
        if multiplier is None:
            for polynomial in values[1:]:
                difference = polynomial.evaluate(BASELINE)-values[0].evaluate(BASELINE)
                if difference:
                    multiplier = (polynomial-values[0])/difference
                    break
        if multiplier is None:
            multiplier = Polynomial.make(1)
        addition = values[0]-multiplier*values[0].evaluate(BASELINE)
        require(all(p == multiplier*p.evaluate(BASELINE)+addition for p in values), "animation values lack a single affine aspect transform")
        require(all(p == multiplier*p.evaluate(BASELINE) for p in tangents), "animation tangents lack the same affine aspect transform")
        transform = multiplier, addition
        require(key not in rules[owner] or rules[owner][key] == transform, "resource/animation aspect transforms disagree")
        rules[owner][key] = transform
    compiled = []
    for name, panes in layouts.items():
        entries = []
        for (pane, field), transform in sorted(rules[name].items()):
            multiplier, addition = transform
            require(all(power in {-1, 0, 1} for polynomial in transform for power, _ in polynomial.terms), "unsupported aspect polynomial")
            require(multiplier.evaluate(BASELINE) == 1 and addition.evaluate(BASELINE) == 0, "baseline must be an identity transform")
            entries.append(dict(pane=pane, field=FIELDS[field],
                                multiplier=[str(dict(multiplier.terms).get(p, Fraction())) for p in [0, 1, -1]],
                                addition=[str(dict(addition.terms).get(p, Fraction())) for p in [0, 1, -1]]))
        receiving_bytes = prepared_members["blyt/"+name+".bflyt"]
        compiled.append(dict(name=name, signature=f"{signature(panes):016x}", panes=panes, rules=entries,
                             resource_size=len(receiving_bytes), resource_sha256=hashlib.sha256(receiving_bytes).hexdigest(),
                             resource_qualified=receiving_bytes == baseline_layouts[name]))
    groups: dict[str, list[dict]] = {}
    for layout in compiled:
        groups.setdefault(layout["signature"], []).append(layout)
    ambiguous = []
    for key, group in groups.items():
        if any(layout["rules"] != group[0]["rules"] for layout in group[1:]):
            ambiguous.append(dict(signature=key, layouts=[layout["name"] for layout in group], reason="same names/types/tree but different aspect semantics; explicit owner identity required"))
    result = dict(format=1, game="TOTK", version="1.0.0", build_id="082ce09b06e33a123cb1e2770f5f9147709033db",
                  baseline="4:3", target_range="1:1 through 16:9", source_sha256=hashlib.sha256(raw_source).hexdigest(),
                  archive_sha256=ARCHIVE_SHA256, prepared_overlay_sha256=hashlib.sha256(overlay_bytes).hexdigest(), layouts=compiled, ambiguous_owners=ambiguous,
                  unsupported_animation_tracks=unsupported,
                  status="metadata_prepared_with_explicit_qualification_gaps" if ambiguous or unsupported else "metadata_prepared")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2)+"\n")
    return dict(status=result["status"], layouts=len(layouts), panes=sum(len(p) for p in layouts.values()),
                rules=sum(len(p) for p in rules.values()), qualified_receiving_layouts=sum(x["resource_qualified"] for x in compiled),
                ambiguous_owners=ambiguous,
                unsupported_animation_tracks=unsupported, output=str(output))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tree", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare(args.tree, args.output), indent=2))
