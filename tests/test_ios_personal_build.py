"""Regression checks for the personal build's identity and exact metadata boundary."""
import hashlib
import importlib.util
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
