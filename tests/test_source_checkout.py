"""Immutable dependency checkout and dirty-cache regression checks."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ios_source_checkout", ROOT / "games/totk/ios/tools/source_checkout.py")
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class SourceCheckoutTests(unittest.TestCase):
    def test_mutable_tag_is_not_an_accepted_pin(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "immutable"):
                source.checkout("unused", "v1.0", Path(temp) / "cache")

    def test_exact_checkout_is_reused_and_modified_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            original, cache = root / "original", root / "cache"
            original.mkdir()
            def git(*args):
                return subprocess.check_output(["git", "-C", str(original), *args], stderr=subprocess.DEVNULL)
            git("init", "-q", "-b", "main")
            (original / "source.c").write_text("int value = 1;\n")
            git("add", "source.c")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
            commit = git("rev-parse", "HEAD").decode().strip()
            self.assertEqual(source.checkout(str(original), commit, cache), commit)
            self.assertEqual(source.checkout("unreachable-url", commit, cache), commit)
            (cache / "source.c").write_text("modified source\n")
            with self.assertRaisesRegex(RuntimeError, "modified"):
                source.checkout(str(original), commit, cache)
            self.assertEqual((cache / "source.c").read_text(), "modified source\n")

    def test_existing_wrong_revision_is_rejected_without_reset(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(["git", "init", "-q", "-b", "main", str(root)], check=True)
            (root / "source.c").write_text("original\n")
            subprocess.run(["git", "-C", str(root), "add", "."], check=True)
            subprocess.run(["git", "-C", str(root), "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false", "commit", "-qm", "fixture"], check=True)
            with self.assertRaisesRegex(RuntimeError, "wrong commit"):
                source.checkout("unused", "0" * 40, root)
            self.assertEqual((root / "source.c").read_text(), "original\n")


if __name__ == "__main__":
    unittest.main()
