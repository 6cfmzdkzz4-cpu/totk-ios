#!/usr/bin/env python3
"""Build an iOS AppIcon catalog from a local square JPEG or PNG.

The source image stays where the caller put it. This writes only generated PNG
sizes into the output catalog. It refuses game, key, firmware and font paths.
macOS uses sips. Other hosts decode a non-interlaced 8-bit PNG.
"""
import argparse, hashlib, json, shutil, struct, subprocess, sys, zlib
from pathlib import Path

ICONS = (
    ("iphone", "20x20", "2x", 40),
    ("iphone", "20x20", "3x", 60),
    ("iphone", "29x29", "2x", 58),
    ("iphone", "29x29", "3x", 87),
    ("iphone", "40x40", "2x", 80),
    ("iphone", "40x40", "3x", 120),
    ("iphone", "60x60", "2x", 120),
    ("iphone", "60x60", "3x", 180),
    ("ipad", "20x20", "1x", 20),
    ("ipad", "20x20", "2x", 40),
    ("ipad", "29x29", "1x", 29),
    ("ipad", "29x29", "2x", 58),
    ("ipad", "40x40", "1x", 40),
    ("ipad", "40x40", "2x", 80),
    ("ipad", "76x76", "1x", 76),
    ("ipad", "76x76", "2x", 152),
    ("ipad", "83.5x83.5", "2x", 167),
    ("ios-marketing", "1024x1024", "1x", 1024),
)
BANNED = ("romfs", "exefs", "prod.keys", ".nsp", ".xci", ".nca", "shared-fonts", "shader-cache", ".bfntt", ".bfttf")


def refuse(path):
    text = path.as_posix().lower()
    if any(part in text for part in BANNED):
        raise SystemExit(f"refusing a game, key, firmware, font or cache path as an app icon: {path}")


def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, rgb):
    raw = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def paeth(a, b, c):
    estimate = a + b - c
    pa, pb, pc = abs(estimate - a), abs(estimate - b), abs(estimate - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("this host decodes PNG; use macOS sips for JPEG")
    pos, width = 8, None
    height = color = None
    idat = b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag, payload = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, color, interlace = struct.unpack(">IIBBBBB", payload)[:5]
            if depth != 8 or color not in (2, 6) or interlace:
                raise SystemExit("PNG must be non-interlaced 8-bit RGB or RGBA")
        elif tag == b"IDAT":
            idat += payload
        elif tag == b"IEND":
            break
    channels = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    offset = 0
    for _ in range(height):
        filt, row = raw[offset], bytearray(raw[offset + 1:offset + 1 + stride])
        offset += stride + 1
        for i, value in enumerate(row):
            left = row[i - channels] if i >= channels else 0
            up, ul = previous[i], previous[i - channels] if i >= channels else 0
            if filt == 1:
                value = (value + left) & 255
            elif filt == 2:
                value = (value + up) & 255
            elif filt == 3:
                value = (value + ((left + up) // 2)) & 255
            elif filt == 4:
                value = (value + paeth(left, up, ul)) & 255
            elif filt:
                raise SystemExit("unsupported PNG filter")
            row[i] = value
        previous = row
        rgb = bytearray()
        for i in range(0, stride, channels):
            rgb += row[i:i + 3]
        rows.append(bytes(rgb))
    return width, height, b"".join(rows)


def scale_nearest(width, height, rgb, side):
    out = bytearray(side * side * 3)
    for y in range(side):
        sy = min(height - 1, y * height // side)
        for x in range(side):
            sx = min(width - 1, x * width // side)
            pixel = (sy * width + sx) * 3
            dest = (y * side + x) * 3
            out[dest:dest + 3] = rgb[pixel:pixel + 3]
    return bytes(out)


def source_size(path):
    if shutil.which("sips"):
        info = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(path)],
                              check=True, capture_output=True, text=True).stdout
        values = [int(line.rsplit(None, 1)[-1]) for line in info.splitlines() if "pixel" in line]
        if len(values) != 2:
            raise SystemExit("sips did not report the image size")
        return values[0], values[1]
    if path.suffix.lower() not in (".png",):
        raise SystemExit("JPEG icons need macOS sips; this host accepts a square PNG")
    return read_png(path)[:2]


def write_square(src, dst, side, cached):
    if shutil.which("sips"):
        subprocess.run(["sips", "-s", "format", "png", "-z", str(side), str(side), str(src), "--out", str(dst)],
                       check=True, stdout=subprocess.DEVNULL)
        return
    width, height, rgb = cached
    write_png(dst, side, side, scale_nearest(width, height, rgb, side))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, output = args.image.expanduser().resolve(), args.output.expanduser().resolve()
    refuse(source)
    if not source.is_file() or source.stat().st_size <= 0:
        raise SystemExit("the app icon must be a local non-empty image file")
    if source.suffix.lower() not in (".png", ".jpg", ".jpeg"):
        raise SystemExit("the app icon must be a JPEG or PNG file")
    width, height = source_size(source)
    if width != height or width < 16:
        raise SystemExit(f"the app icon must be square and at least 16 pixels, got {width}x{height}")
    catalog = output / "AppIcon.appiconset"
    if catalog.exists():
        shutil.rmtree(catalog)
    catalog.mkdir(parents=True)
    (output / "Contents.json").write_text(json.dumps({"info": {"author": "xcode", "version": 1}}) + "\n")
    cached = None if shutil.which("sips") else read_png(source)
    images = []
    for idiom, size, scale, pixels in ICONS:
        name = f"icon-{idiom}-{size}@{scale}.png"
        write_square(source, catalog / name, pixels, cached)
        images.append({"filename": name, "idiom": idiom, "scale": scale, "size": size})
    (catalog / "Contents.json").write_text(json.dumps({"images": images, "info": {"author": "xcode", "version": 1}}, indent=2) + "\n")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    print(json.dumps({"status": "pass", "width": width, "height": height, "icons": len(images),
                      "source_sha256": digest, "upscaled_marketing_icon": width < 1024}))


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, ValueError, zlib.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
