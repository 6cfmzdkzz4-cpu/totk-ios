"""Guard the publication boundary, including payloads removed in later commits."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("publication_audit", ROOT / "tools/audit_publication.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class PublicationAuditTests(unittest.TestCase):
    def test_private_payload_and_generated_header_paths_are_rejected(self):
        for name in ("game.nsp", "app/embedded.mobileprovision", "work/gameplay_extra_audio.h", "app/TOTK.app/Info.plist"):
            with self.subTest(name=name):
                self.assertTrue(audit.violations(name, b"synthetic"))

    def test_source_hash_and_numeric_metadata_are_allowed(self):
        self.assertEqual(audit.violations("assets/expected.json", b'{"sha256":"' + b"a" * 64 + b'"}'), [])
        self.assertEqual(audit.violations("source.c", b"int main(void) { return 0; }"), [])

    def test_secret_detection_reports_only_categories(self):
        token = b"ghp_" + b"A" * 36
        result = audit.violations("settings.txt", token)
        self.assertEqual(result, ["GitHub credential"])
        self.assertNotIn(token.decode(), repr(result))

    def test_history_catches_a_payload_deleted_from_head(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def git(*args):
                return subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=True)
            git("init", "-q", "-b", "main")
            (root / "private.nsp").write_bytes(b"synthetic payload, never game data")
            git("add", "private.nsp")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
            git("rm", "private.nsp")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false", "commit", "-qm", "remove fixture")
            with patch.object(audit, "ROOT", root):
                self.assertEqual(audit.audit_current()[1], [])
                self.assertIn(("private.nsp", "forbidden payload/signing/output extension"), audit.audit_history()[1])


if __name__ == "__main__":
    unittest.main()
