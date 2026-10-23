"""Exercise actual bounded UI diagnostic tables under parallel callers."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


class ReservationTests(unittest.TestCase):
    def test_parallel_capacity_and_duplicate_reservations(self):
        runtime = Path(__file__).resolve().parents[1]/"games/totk/runtime"
        with tempfile.TemporaryDirectory(prefix="totk-live-reservations-") as folder:
            for sanitizer in ["address,undefined", "thread"]:
                executable = Path(folder)/("reservation-test-"+sanitizer.replace(",", "-"))
                subprocess.run(["xcrun", "clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                                "-fsanitize="+sanitizer, "-I"+str(runtime), str(Path(__file__).with_suffix(".c")),
                                "-pthread", "-o", str(executable)], check=True)
                result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
                receipt = json.loads(result.stdout)
                self.assertEqual(receipt["status"], "pass")
                self.assertEqual(receipt["rounds"], 144)


if __name__ == "__main__":
    unittest.main()
