#!/usr/bin/env python3
"""Compile verified private layout metadata to a private native lookup header.

SPDX-License-Identifier: GPL-3.0-only
The header contains identifiers and authored transforms, never resource bytes.
"""
from __future__ import annotations

import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path


def emit(metadata: Path, output: Path) -> dict:
    """Keep Parts namespaces explicit and preserve exact rational coefficients."""
    raw = metadata.read_bytes()
    value = json.loads(raw)
    assert value["build_id"] == "082ce09b06e33a123cb1e2770f5f9147709033db"
    assert value["baseline"] == "4:3"
    layouts = value["layouts"]
    names = {row["name"]: i for i, row in enumerate(layouts)}
    assert len(names) == len(layouts)
    panes, rules, owners = [], [], []
    unresolved = set()
    def number(text: str) -> str:
        return float(Fraction(text)).hex()+"f"
    def quote(text: str) -> str:
        return json.dumps(text, ensure_ascii=True)
    for layout in layouts:
        start = len(panes)
        by_pane: dict[int, list[dict]] = {}
        for row in layout["rules"]:
            by_pane.setdefault(row["pane"], []).append(row)
        for i, pane in enumerate(layout["panes"]):
            first = len(rules)
            for row in by_pane.get(i, []):
                assert row["field"] in [0x30, 0x34, 0x48, 0x4c]
                coefficients = ["{"+",".join(number(c) for c in row[key])+"}" for key in ["multiplier", "addition"]]
                # An inverse scale is a fill candidate, not permission to
                # stretch descendants. Receiving code must qualify its role.
                inverse = row["multiplier"] == ["0", "0", "4/3"]
                rules.append("{%d,%s,%s,%d}" % (row["field"], *coefficients, inverse))
            part = pane.get("parts_layout")
            if part and part not in names:
                unresolved.add(part)
            parent = start+pane["parent"] if pane["parent"] >= 0 else 65535
            descendants = []
            for child in layout["panes"][i+1:]:
                if child["depth"] <= pane["depth"]:break
                descendants.append(child)
            drawing = [p for p in [pane, *descendants] if p["kind"] in {"pic1", "wnd1"}]
            no_foreground = all(p["kind"] in {"pan1", "pic1", "wnd1", "bnd1"} for p in [pane, *descendants])
            named_background = pane["name"] in {"N_BG_00", "N_SystemBG_00"} or pane["name"].startswith("N_BlurInOut_0")
            qualified_fill = bool(drawing and no_foreground and (named_background or pane["kind"] in {"pic1", "wnd1"}))
            panes.append("{%s,%s,%d,%d,%d,%d,%d}" % (
                quote(pane["name"]), quote(pane["kind"]), parent,
                names.get(part, 65535), first, len(rules)-first, qualified_fill))
        owners.append("{%s,%d,%d,%d,%s,%d}" % (quote(layout["name"]), start, len(panes)-start,
                       layout["resource_size"], quote(layout["resource_sha256"]), bool(layout["resource_qualified"])))
    assert len(panes) < 65535 and len(rules) < 65535
    text = ["/* Generated privately by emit_live_display.py; GPL-3.0-only. */", "#pragma once",
            "#include <stdint.h>",
            "typedef struct { uint8_t field; float multiplier[3],addition[3]; uint8_t fill_candidate; } TotkLiveRule;",
            "typedef struct { const char *name,*kind; uint16_t parent,parts_layout,first_rule,rule_count; uint8_t simple_fill; } TotkLivePaneSpec;",
            "typedef struct { const char *name; uint16_t first_pane,pane_count; uint32_t resource_size; const char *resource_sha256; uint8_t resource_qualified; } TotkLiveLayoutSpec;",
            "static const TotkLiveRule totk_live_rules[]={"+",\n".join(rules)+"};",
            "static const TotkLivePaneSpec totk_live_panes[]={"+",\n".join(panes)+"};",
            "static const TotkLiveLayoutSpec totk_live_layouts[]={"+",\n".join(owners)+"};",
            "#define TOTK_LIVE_METADATA_SHA256 "+quote(hashlib.sha256(raw).hexdigest())]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(text)+"\n")
    return dict(layouts=len(owners), panes=len(panes), rules=len(rules),
                unresolved_parts=sorted(unresolved), output=str(output),
                metadata_sha256=hashlib.sha256(raw).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(json.dumps(emit(args.metadata, args.output), indent=2))
