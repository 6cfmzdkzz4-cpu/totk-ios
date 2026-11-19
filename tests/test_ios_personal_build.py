"""Regression checks for the personal build's identity and exact metadata boundary."""
import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("personal_ios_build", ROOT / "tools/ios_build.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class PersonalBuildTests(unittest.TestCase):
    def test_install_requires_explicit_signing_identity(self):
        args = builder.parse_args(["game.nsp", "--keys", "prod.keys", "--bundle-id", "com.example.totk", "--install"])
        with self.assertRaisesRegex(builder.upstream.BuildError, "requires --team"):
            builder.validate_options(args)

    def test_prepare_only_cannot_trigger_signing(self):
        args = builder.parse_args(["game.nsp", "--keys", "prod.keys", "--bundle-id", "com.example.totk", "--prepare-only", "--team", "EXAMPLE", "--device", "EXAMPLE"])
        with self.assertRaisesRegex(builder.upstream.BuildError, "cannot sign"):
            builder.validate_options(args)

    def test_cloud_container_does_not_accept_plist_or_yaml_injection(self):
        args = builder.parse_args(["game.nsp", "--keys", "prod.keys", "--bundle-id", "com.example.totk", "--cloud-container", "iCloud.example\nBad: true"])
        with self.assertRaisesRegex(builder.upstream.BuildError, "exact iCloud"):
            builder.validate_options(args)

    def metadata_fixture(self, root):
        folder = root / "extracted/v0/exefs"
        folder.mkdir(parents=True)
        data = bytearray(0xA0)
        data[:4] = b"META"
        struct.pack_into("<I", data, 0x70, 0x80)
        struct.pack_into("<Q", data, 0x90, int("0100F2C0115B6000", 16))
        (folder / "main.npdm").write_bytes(data)
        for name in ("main", "sdk", "subsdk0"):
            (folder / name).write_bytes(("synthetic-" + name).encode())
        expected = {"title_id": "0100F2C0115B6000", "exefs_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir()}}
        return folder, expected

    def test_metadata_exception_never_changes_executable_gates(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder, expected = self.metadata_fixture(root)
            before = dict(expected["exefs_sha256"])
            digest = before["main.npdm"]
            with patch.object(builder, "KNOWN_UNUSED_NPDM", digest), patch.object(builder.upstream, "EXPECTED", expected), patch.object(builder.upstream, "source_matches", return_value=True):
                self.assertTrue(builder.accept_known_unused_metadata(root, Path("synthetic.nsp")))
                self.assertEqual(before, expected["exefs_sha256"])
                (folder / "main").write_bytes(b"unexpected executable")
                with self.assertRaises(builder.upstream.BuildError):
                    builder.accept_known_unused_metadata(root, Path("synthetic.nsp"))

    def test_cached_romfs_requires_12_gib_and_uncached_stays_at_the_full_extract(self):
        self.assertEqual(builder.free_requirement_gib(True, True, None), 12)
        self.assertEqual(builder.free_requirement_gib(True, False, None), 12)
        self.assertEqual(builder.free_requirement_gib(False, False, None), 45)
        self.assertEqual(builder.free_requirement_gib(False, True, None), 65)
        self.assertEqual(builder.free_requirement_gib(False, True, 12), 12)

    def test_preflight_names_the_shortfall(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            low = type("Usage", (), {"free": 11 * 2**30})()
            with patch.object(builder.shutil, "disk_usage", return_value=low):
                with self.assertRaisesRegex(builder.upstream.BuildError, r"cached RomFS build: 11\.0 GiB free"):
                    builder.assert_free_space(work, builder.free_requirement_gib(True, False, None),
                                              "allow at least 12 GiB free on the work volume for a cached RomFS build")

    def test_min_free_override_uses_the_flag_before_the_environment(self):
        base = ["game.nsp", "--keys", "prod.keys", "--bundle-id", "com.example.totk"]
        with patch.dict(os.environ, {"TOTK_MIN_FREE_GIB": "30"}):
            self.assertEqual(builder.resolve_min_free(builder.parse_args(base + ["--min-free-gib", "14"])), 14)
        with patch.dict(os.environ, {"TOTK_MIN_FREE_GIB": "16"}):
            self.assertEqual(builder.resolve_min_free(builder.parse_args(base)), 16)
        with patch.dict(os.environ, {"TOTK_MIN_FREE_GIB": "lots"}):
            with self.assertRaisesRegex(builder.upstream.BuildError, "TOTK_MIN_FREE_GIB must be a number"):
                builder.resolve_min_free(builder.parse_args(base))
        with patch.dict(os.environ, {"TOTK_MIN_FREE_GIB": "7"}):
            with self.assertRaisesRegex(builder.upstream.BuildError, "at least 8 GiB"):
                builder.resolve_min_free(builder.parse_args(base))

    def test_packaging_aborts_under_8_gib(self):
        low = type("Usage", (), {"free": 7 * 2**30})()
        with tempfile.TemporaryDirectory() as temp, patch.object(builder.shutil, "disk_usage", return_value=low):
            with self.assertRaisesRegex(builder.upstream.BuildError, r"packaging stopped: 7\.0 GiB free"):
                builder.assert_free_space(Path(temp), builder.PACK_MIN_FREE_GIB, "packaging stopped")

    def test_metadata_exception_rejects_unbound_source_or_title(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder, expected = self.metadata_fixture(root)
            with patch.object(builder, "KNOWN_UNUSED_NPDM", expected["exefs_sha256"]["main.npdm"]), patch.object(builder.upstream, "EXPECTED", expected), patch.object(builder.upstream, "source_matches", return_value=False):
                self.assertFalse(builder.accept_known_unused_metadata(root, Path("other.nsp")))
            data = bytearray((folder / "main.npdm").read_bytes())
            struct.pack_into("<Q", data, 0x90, 0)
            (folder / "main.npdm").write_bytes(data)
            with patch.object(builder, "KNOWN_UNUSED_NPDM", hashlib.sha256(data).hexdigest()), patch.object(builder.upstream, "EXPECTED", expected), patch.object(builder.upstream, "source_matches", return_value=True):
                self.assertFalse(builder.accept_known_unused_metadata(root, Path("synthetic.nsp")))


if __name__ == "__main__":
    unittest.main()
