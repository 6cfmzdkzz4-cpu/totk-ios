"""Local user-image icon catalog, with game paths refused and no source image copied."""
import json
import subprocess
import sys
import tempfile
import unittest
import zlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'games/totk/ios/tools/prepare_app_icon.py'


def png(width, height, rgb):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')


class AppIcon(unittest.TestCase):
    def test_square_png_becomes_required_sizes_without_copying_the_source(self):
        with tempfile.TemporaryDirectory(prefix='totk-icon-') as directory:
            root = Path(directory)
            source = root / 'user.png'
            rgb = bytearray()
            for y in range(16):
                for x in range(16):
                    rgb += b'\xff\x00\x00' if x < 8 else b'\x00\x00\xff'
            source.write_bytes(png(16, 16, bytes(rgb)))
            output = root / 'Assets.xcassets'
            result = subprocess.run([sys.executable, str(TOOL), '--image', str(source), '--output', str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(report['status'], 'pass')
            self.assertTrue(report['upscaled_marketing_icon'])
            catalog = json.loads((output / 'AppIcon.appiconset' / 'Contents.json').read_text())
            sizes = {(image['idiom'], image['size'], image['scale']) for image in catalog['images']}
            self.assertIn(('iphone', '60x60', '3x'), sizes)
            self.assertIn(('ipad', '83.5x83.5', '2x'), sizes)
            self.assertIn(('ios-marketing', '1024x1024', '1x'), sizes)
            self.assertFalse(any(path.suffix.lower() in ('.jpg', '.jpeg') for path in output.rglob('*')))
            marketing = output / 'AppIcon.appiconset' / 'icon-ios-marketing-1024x1024@1x.png'
            self.assertGreater(marketing.stat().st_size, 1000)

    def test_game_paths_and_nonsquare_images_are_refused(self):
        with tempfile.TemporaryDirectory(prefix='totk-icon-refuse-') as directory:
            root = Path(directory)
            output = root / 'out'
            game = root / 'romfs' / 'icon.jpg'
            game.parent.mkdir()
            game.write_bytes(b'\xff\xd8\xff\xd9')
            refused = subprocess.run([sys.executable, str(TOOL), '--image', str(game), '--output', str(output)],
                                     capture_output=True, text=True)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn('refusing', refused.stderr + refused.stdout)
            wide = root / 'wide.png'
            wide.write_bytes(png(8, 4, b'\x00\x00\x00' * 32))
            shape = subprocess.run([sys.executable, str(TOOL), '--image', str(wide), '--output', str(output)],
                                   capture_output=True, text=True)
            self.assertNotEqual(shape.returncode, 0)
            self.assertIn('square', shape.stderr + shape.stdout)


if __name__ == '__main__':
    unittest.main()
