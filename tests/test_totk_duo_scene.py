"""Compile the real private relay ingress against bounded fixture services."""
from pathlib import Path
import json
import subprocess
import tempfile
import unittest


class DuoSceneTests(unittest.TestCase):
    def test_freshness_geometry_lifecycle_and_short_presses(self):
        runtime=Path(__file__).resolve().parents[1]/"games/totk/runtime"
        with tempfile.TemporaryDirectory(prefix="totk-duo-scene-") as folder:
            root=Path(folder)
            executable=root/"fixture"
            subprocess.run(["xcrun","clang","-std=c11","-fobjc-arc","-Wall","-Wextra","-Werror",
                            "-I"+str(runtime),"-I"+str(runtime.parent/"third_party/d2r_gfx/gfx/nvn/include"),
                            str(Path(__file__).with_suffix(".m")),str(runtime/"totk_duo_scene.m"),
                            "-framework","Foundation","-pthread","-o",str(executable)],check=True)
            result=subprocess.run([str(executable),folder],capture_output=True,text=True,check=True,timeout=10)
            receipt=json.loads(result.stdout)
            self.assertEqual(receipt["status"],"pass")
            self.assertEqual(receipt["fresh_publications"],10)


if __name__=="__main__":
    unittest.main()
