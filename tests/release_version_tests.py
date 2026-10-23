from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from release_version import parse_version, select_version


class VersionSelectionTests(unittest.TestCase):
    def test_increments(self):
        for bump, expected in (("patch", (1, 2, 4)), ("minor", (1, 3, 0)),
                               ("major", (2, 0, 0))):
            with self.subTest(bump=bump):
                self.assertEqual(select_version("0.1.0", ["v1.2.3"], [], bump), expected)

    def test_versions_sort_numerically(self):
        self.assertEqual(select_version("0.1.0", ["v0.9.9", "v0.10.10", "v0.10.2"], []),
                         (0, 10, 11))

    def test_initial_version_uses_baseline(self):
        self.assertEqual(select_version("0.1.0", [], []), (0, 1, 1))

    def test_baseline_does_not_go_backwards(self):
        self.assertEqual(select_version("2.0.0", ["v1.2.3"], []), (2, 0, 1))

    def test_ignores_prereleases_and_unrelated_tags(self):
        tags = ["v1.2.3", "v99.0.0-beta.1", "v99.0.0+build", "other", "v01.0.0"]
        self.assertEqual(select_version("0.1.0", tags, []), (1, 2, 4))

    def test_retries_reuse_tag_on_same_commit(self):
        self.assertEqual(select_version("0.1.0", ["v1.2.3"], ["v1.2.3"]), (1, 2, 3))

    def test_pushed_or_manually_selected_tag_is_authoritative(self):
        self.assertEqual(select_version("0.1.0", ["v2.0.0"], [], "major", "v1.2.3"),
                         (1, 2, 3))

    def test_rejects_malformed_versions(self):
        for version in ("1.2", "01.2.3", "1.2.3.4", "1.2.3-beta.1", "1.2.3\n", "v1.2.3"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                parse_version(version)

    def test_rejects_malformed_release_tags(self):
        for tag in ("1.2.3", "v1.2", "v01.2.3", "v1.2.3-beta.1"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                select_version("0.1.0", [], [], tag=tag)


class VersionCommandTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="spacewing-version-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "scripts").mkdir()
        shutil.copy(Path(__file__).resolve().parents[1] / "scripts/release_version.py",
                    self.root / "scripts/release_version.py")
        (self.root / "VERSION").write_text("0.1.0\n")
        self.git("init", "--initial-branch=main", "--quiet")
        self.commit()

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root),
                                        "-c", "user.name=Release Tests",
                                        "-c", "user.email=release-tests@example.invalid", *args],
                                       text=True).strip()

    def commit(self):
        self.git("commit", "--allow-empty", "--quiet", "-m", "Test source")

    def run_command(self, *args):
        return subprocess.run([sys.executable, str(self.root / "scripts/release_version.py"),
                               *args], text=True, capture_output=True)

    def test_selects_version_without_changing_source_or_tags(self):
        self.git("tag", "v0.1.0")
        self.commit()
        before = (self.git("rev-parse", "HEAD"), self.git("status", "--porcelain"),
                  self.git("tag", "--list"))
        result = self.run_command()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "0.1.1\n")
        self.assertEqual(before, (self.git("rev-parse", "HEAD"),
                                  self.git("status", "--porcelain"), self.git("tag", "--list")))

    def test_reuses_annotated_tag_for_retry(self):
        self.git("tag", "-a", "v1.2.3", "-m", "Prepared release")
        result = self.run_command()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "1.2.3\n")

    def test_tag_run_does_not_increment(self):
        result = self.run_command("--tag", "v1.2.3", "--bump", "major")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "1.2.3\n")

    def test_invalid_tag_fails_without_a_version(self):
        result = self.run_command("--tag", "v1.2")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
