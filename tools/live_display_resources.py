"""Strict UI metadata readers and exact symbolic TOTK-Ultrawide rules.

SPDX-License-Identifier: GPL-3.0-only
The rule interpreter consumes the pinned upstream build-time script. Generated
title metadata stays in the caller's private work tree; no assets are exported.
"""
from __future__ import annotations

import ast
from dataclasses import dataclass
from fractions import Fraction
import struct


class ResourceError(ValueError):
    """An input does not satisfy the exact supported UI format."""


def require(condition: bool, reason: str) -> None:
    if not condition:
        raise ResourceError(reason)


def c_string(data: bytes, start: int, limit: int) -> str:
    require(0 <= start < limit <= len(data), "string range")
    end = data.find(b"\0", start, limit)
    require(end >= start, "unterminated identifier")
    raw = data[start:end]
    require(raw and all(0x20 <= value <= 0x7e for value in raw), "non-ASCII identifier")
    return raw.decode("ascii")


def sarc_members(data: bytes) -> dict[str, bytes]:
    """Validate little-endian SARC member bounds and unique relative paths."""
    require(len(data) >= 32 and data[:4] == b"SARC" and data[6:8] == b"\xff\xfe", "SARC header")
    header, size, start = struct.unpack_from("<HxxII", data, 4)
    require(size == len(data) and 20 <= header < start <= size, "SARC size")
    require(data[header:header+4] == b"SFAT", "SFAT header")
    count = struct.unpack_from("<H", data, header+6)[0]
    names = header + 12 + count*16
    require(names+8 <= start and data[names:names+4] == b"SFNT", "SFNT header")
    names += 8
    result: dict[str, bytes] = {}
    ranges: list[tuple[int, int]] = []
    for index in range(count):
        _, attribute, lo, hi = struct.unpack_from("<4I", data, header+12+index*16)
        require(attribute >> 24 == 1 and lo <= hi <= size-start, "SARC member bounds")
        name = c_string(data, names+(attribute & 0xffffff)*4, start)
        require(not name.startswith("/") and ".." not in name.split("/") and name not in result, "SARC member identity")
        require(all(hi <= a or lo >= b for a, b in ranges), "overlapping SARC members")
        ranges.append((lo, hi))
        result[name] = data[start+lo:start+hi]
    return result


def sections(data: bytes, magic: bytes):
    """Read exact version-9 FLYT/FLAN sections without accepting trailing bytes."""
    require(len(data) >= 20 and data[:4] == magic and data[4:6] == b"\xff\xfe", "UI file header")
    header, version, size, count = struct.unpack_from("<HIIH", data, 6)
    require(header == 20 and version == 0x09000000 and size == len(data), "UI file version/size")
    at = header
    for _ in range(count):
        require(at+8 <= size, "UI section header")
        length = struct.unpack_from("<I", data, at+4)[0]
        require(length >= 8 and length % 4 == 0 and at+length <= size, "UI section bounds")
        yield at, data[at:at+4], length
        at += length
    require(at == size, "UI trailing bytes")


PANE_TAGS = {b"pan1", b"pic1", b"txt1", b"bnd1", b"prt1", b"wnd1", b"ali1", b"scr1"}


def fixed_identifier(data: bytes, start: int, width: int) -> str:
    """UI fixed-width names may fill all bytes without an on-disk terminator."""
    require(0 <= start and start+width <= len(data), "fixed identifier range")
    raw = data[start:start+width].split(b"\0", 1)[0]
    require(raw and all(0x20 <= value <= 0x7e for value in raw), "fixed identifier encoding")
    return raw.decode("ascii")


def parse_layout(data: bytes) -> list[dict]:
    """Recover the authored pane tree, retaining section positions for rule binding."""
    panes: list[dict] = []
    parents: list[int] = []
    previous: int | None = None
    for at, tag, length in sections(data, b"FLYT"):
        if tag in PANE_TAGS:
            require(length >= 84, "short pane")
            name = fixed_identifier(data, at+12, 24)
            panes.append(dict(name=name, kind=tag.decode(), parent=parents[-1] if parents else -1,
                              depth=len(parents), offset=at,
                              position=struct.unpack_from("<2f", data, at+44),
                              scale=struct.unpack_from("<2f", data, at+68)))
            if tag == b"prt1":
                require(length >= 100, "short Parts namespace")
                count = struct.unpack_from("<I", data, at+84)[0]
                reference_at = at+96+count*40
                require(reference_at < at+length, "Parts override table bounds")
                reference = c_string(data, reference_at, at+length)
                require(reference and all(c.isascii() and (c.isalnum() or c == "_") for c in reference), "Parts resource identifier")
                panes[-1]["parts_layout"] = reference
                panes[-1]["parts_override_names"] = [fixed_identifier(data, at+96+i*40, 24) if data[at+96+i*40] else None for i in range(count)]
            panes[-1]["rotation"] = struct.unpack_from("<3f", data, at+56)
            panes[-1]["size"] = struct.unpack_from("<2f", data, at+76)
            panes[-1]["origin"] = data[at+9]
            previous = len(panes)-1
        elif tag == b"pas1":
            require(length == 8 and previous is not None, "pane child start")
            parents.append(previous)
            previous = None
        elif tag == b"pae1":
            require(length == 8 and parents, "pane child end")
            parents.pop()
            previous = None
    require(not parents and panes and panes[0]["name"] == "RootPane", "layout root/tree")
    require(sum(p["parent"] == -1 for p in panes) == 1, "multiple layout roots")
    return panes


def parse_animation(data: bytes) -> dict[int, dict]:
    """Bind every float-key value to its real content/tag/target and track keys."""
    values: dict[int, dict] = {}
    for at, tag, length in sections(data, b"FLAN"):
        if tag != b"pai1":
            continue
        require(length >= 20, "short animation info")
        count = struct.unpack_from("<H", data, at+14)[0]
        table = struct.unpack_from("<I", data, at+16)[0]
        require(20 <= table and table+count*4 <= length, "animation content table")
        for index in range(count):
            content = at+struct.unpack_from("<I", data, at+table+index*4)[0]
            require(at <= content and content+32 <= at+length, "animation content")
            name = fixed_identifier(data, content, 28)
            tags, kind = struct.unpack_from("<BB", data, content+28)
            require(content+32+tags*4 <= at+length and tags <= 64, "animation tag table")
            for ti in range(tags):
                part = content+struct.unpack_from("<I", data, content+32+ti*4)[0]
                require(content <= part and part+8 <= at+length, "animation tag")
                label = data[part:part+4].decode("ascii")
                entries = data[part+4]
                require(part+8+entries*4 <= at+length, "animation entry table")
                for ei in range(entries):
                    entry = part+struct.unpack_from("<I", data, part+8+ei*4)[0]
                    require(part <= entry and entry+12 <= at+length, "animation entry")
                    subindex, target, curve = struct.unpack_from("<3B", data, entry)
                    keys = struct.unpack_from("<H", data, entry+4)[0]
                    key_start = entry+struct.unpack_from("<I", data, entry+8)[0]
                    if curve != 2:  # discrete integer tracks have no float Hermite value
                        continue
                    require(keys and entry+12 <= key_start and key_start+12*keys <= at+length, "animation key range")
                    offsets = tuple(key_start+12*i+component for i in range(keys) for component in (4, 8))
                    for offset in offsets:
                        require(offset not in values, "overlapping animation key values")
                        values[offset] = dict(name=name, content_kind=kind, tag=label, target=target,
                                              subindex=subindex, track=entry, key_offsets=offsets,
                                              component="value" if (offset-key_start) % 12 == 4 else "tangent")
    return values


@dataclass(frozen=True)
class Polynomial:
    """Exact Laurent polynomial in requested aspect, with rational coefficients."""
    terms: tuple[tuple[int, Fraction], ...]

    @staticmethod
    def make(value) -> Polynomial:
        if isinstance(value, Polynomial):
            return value
        return Polynomial.normalized({0: Fraction(str(value))})

    @staticmethod
    def normalized(terms: dict[int, Fraction]) -> Polynomial:
        return Polynomial(tuple(sorted((power, value) for power, value in terms.items() if value)))

    def __add__(self, other):
        result = dict(self.terms)
        for power, value in Polynomial.make(other).terms:
            result[power] = result.get(power, Fraction()) + value
        return Polynomial.normalized(result)

    __radd__ = __add__

    def __neg__(self):
        return Polynomial.normalized({power: -value for power, value in self.terms})

    def __sub__(self, other):
        return self + -Polynomial.make(other)

    def __rsub__(self, other):
        return Polynomial.make(other) + -self

    def __mul__(self, other):
        result: dict[int, Fraction] = {}
        for a, av in self.terms:
            for b, bv in Polynomial.make(other).terms:
                result[a+b] = result.get(a+b, Fraction()) + av*bv
        return Polynomial.normalized(result)

    __rmul__ = __mul__

    def __truediv__(self, other):
        terms = Polynomial.make(other).terms
        require(len(terms) == 1 and terms[0][1] != 0, "non-monomial aspect divisor")
        power, divisor = terms[0]
        return Polynomial.normalized({p-power: v/divisor for p, v in self.terms})

    def __rtruediv__(self, other):
        return Polynomial.make(other) / self

    def evaluate(self, aspect: Fraction) -> Fraction:
        return sum(value * aspect**power for power, value in self.terms)


def symbolic_vendor_rules(source: str, names: list[str], wide: bool) -> list[tuple]:
    """Interpret only the pinned script's rule grammar, without executing its I/O.

    Unlike numeric fitting, each expression retains its exact rational formula.
    Unsupported statements fail rather than silently omitting an upstream rule.
    """
    function = next(n for n in ast.parse(source).body if isinstance(n, ast.FunctionDef) and n.name == "patch_romfs")
    aspect = Polynomial(((1, Fraction(1)),))
    env = dict(aspect_ratio=aspect, HUD_pos="corner", file_names_stripped=names)
    rules: list[tuple] = []

    def expression(node):
        if isinstance(node, ast.Constant):
            return Fraction(str(node.value)) if isinstance(node.value, (int, float)) else node.value
        if isinstance(node, ast.Name):
            return env[node.id]
        if isinstance(node, (ast.List, ast.Tuple)):
            return [expression(n) for n in node.elts]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -expression(node.operand)
        if isinstance(node, ast.BinOp):
            a, b = expression(node.left), expression(node.right)
            if isinstance(node.op, ast.Add):return a+b
            if isinstance(node.op, ast.Sub):return a-b
            if isinstance(node.op, ast.Mult):return a*b
            if isinstance(node.op, ast.Div):return a/b
        if isinstance(node, ast.Compare) and len(node.ops) == 1:
            a, b = expression(node.left), expression(node.comparators[0])
            op = node.ops[0]
            if isinstance(op, ast.GtE) and a == aspect:
                require(b == Fraction(16, 9), "unknown vendor aspect boundary")
                return wide
            if isinstance(op, ast.NotIn):return a not in b
            if isinstance(op, ast.Eq):return a == b
        raise ResourceError("unsupported vendor expression: " + ast.dump(node))

    def statements(nodes):
        for node in nodes:
            if isinstance(node, ast.FunctionDef):
                require(node.name in {"patch_blyt", "patch_anim"}, "unknown vendor helper")
            elif isinstance(node, ast.Assign):
                require(len(node.targets) == 1 and isinstance(node.targets[0], ast.Name), "vendor assignment")
                name = node.targets[0].id
                if name not in {"blyt_folder", "file_names", "file_names_stripped"}:
                    env[name] = expression(node.value)
            elif isinstance(node, ast.If):
                statements(node.body if expression(node.test) else node.orelse)
            elif isinstance(node, ast.For):
                require(isinstance(node.target, ast.Name) and not node.orelse, "vendor loop")
                for value in expression(node.iter):
                    env[node.target.id] = value
                    statements(node.body)
            elif isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
                call = node.value
                require(isinstance(call.func, ast.Name) and call.func.id in {"patch_blyt", "patch_anim"} and not call.keywords, "vendor rule call")
                args = [expression(a) for a in call.args]
                args[-1] = Polynomial.make(args[-1])
                rules.append((call.func.id, *args))
            else:
                raise ResourceError("unsupported vendor statement: " + ast.dump(node))
    statements(function.body)
    return rules
