"""Keep source metadata checks strict even without external submodule checkout."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from CheckDeclaredSources import source_reference_is_valid


class DeclaredSourcesTest(unittest.TestCase):
    def test_existing_owned_file_passes_and_missing_owned_file_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "src").mkdir()
            (root / "src/Owned.cpp").write_text("// valid\n", encoding="utf-8")
            self.assertTrue(source_reference_is_valid(root, "src/Owned.cpp"))
            self.assertFalse(source_reference_is_valid(root, "src/Missing.cpp"))

    def test_only_declared_and_git_tracked_submodule_files_can_be_absent(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            (root / ".gitmodules").write_text(
                '[submodule "lib/submodules/imgui"]\n'
                '\tpath = lib/submodules/imgui\n'
                '\turl = https://example.invalid/imgui.git\n',
                encoding="utf-8")
            subprocess.run(["git", "add", ".gitmodules"], cwd=root,
                           check=True, stdout=subprocess.DEVNULL)
            subprocess.run([
                "git", "update-index", "--add", "--cacheinfo", "160000",
                "0123456789abcdef0123456789abcdef01234567",
                "lib/submodules/imgui",
            ], cwd=root, check=True, stdout=subprocess.DEVNULL)
            self.assertTrue(source_reference_is_valid(
                root, "lib/submodules/imgui/imgui.cpp"))
            self.assertFalse(source_reference_is_valid(
                root, "lib/submodules/missing/imgui.cpp"))
            self.assertFalse(source_reference_is_valid(
                root, "lib/src/missing.cpp"))
            self.assertFalse(source_reference_is_valid(
                root, "lib/submodules/imgui/../../src/missing.cpp"))


if __name__ == "__main__":
    unittest.main()
