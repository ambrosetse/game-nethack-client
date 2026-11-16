#!/usr/bin/env python3
"""Unit tests for fetch_build.py; gh is replaced by a fake runner.

    python3 -m unittest discover -s tools -p "test_*.py"
"""

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fetch_build  # noqa: E402
from fetch_build import FetchError, fetch  # noqa: E402

RUNS = json.dumps([{"databaseId": 123, "headSha": "abcdef0123456789"}])


class FakeGh:
    """Answers gh run list with RUNS and gh run download by writing FILES."""

    def __init__(self, runs=RUNS, files=("NetHackGodot.exe", "NetHackGodot.pck")):
        self.runs, self.files, self.calls = runs, files, []

    def __call__(self, cmd):
        self.calls.append(cmd)
        if cmd[:3] == ["gh", "run", "list"]:
            return self.runs
        dest = cmd[cmd.index("-D") + 1]
        for name in self.files:
            with open(os.path.join(dest, name), "w") as f:
                f.write("new")
        return ""


class FetchBuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dest = os.path.join(self.tmp.name, "game")

    def tearDown(self):
        self.tmp.cleanup()

    def test_downloads_the_windows_artifact_of_the_newest_green_run(self):
        gh = FakeGh()
        self.assertEqual(fetch("windows", "feature", None, self.dest, False, gh),
                         (123, "abcdef0123456789", ["NetHackGodot.exe", "NetHackGodot.pck"]))
        self.assertEqual(sorted(os.listdir(self.dest)), ["NetHackGodot.exe", "NetHackGodot.pck"])
        listing, download = gh.calls
        self.assertEqual(listing[listing.index("--branch") + 1], "feature")
        self.assertEqual(listing[listing.index("--status") + 1], "success")
        self.assertEqual(listing[listing.index("--workflow") + 1], "CI")
        self.assertEqual(download[:4], ["gh", "run", "download", "123"])
        self.assertEqual(download[download.index("-n") + 1], "NetHackGodot-windows-x86_64")
        self.assertNotIn("-R", download)
        self.assertTrue(os.path.isfile(os.path.join(self.dest, "NetHackGodot.exe")))

    def test_repo_is_passed_to_every_gh_call(self):
        gh = FakeGh()
        fetch("windows", "main", "owner/name", self.dest, False, gh)
        for cmd in gh.calls:
            self.assertEqual(cmd[cmd.index("-R") + 1], "owner/name")

    def test_no_green_run_fails(self):
        with self.assertRaisesRegex(FetchError, "no successful CI run"):
            fetch("windows", "main", None, self.dest, False, FakeGh(runs="[]"))

    def test_incomplete_download_fails(self):
        with self.assertRaisesRegex(FetchError, "missing NetHackGodot.pck"):
            fetch("windows", "main", None, self.dest, False, FakeGh(files=("NetHackGodot.exe",)))
        self.assertEqual(os.listdir(self.dest), [])

    def test_existing_build_is_kept_without_force(self):
        os.makedirs(self.dest)
        exe = os.path.join(self.dest, "NetHackGodot.exe")
        with open(exe, "w") as f:
            f.write("old")
        gh = FakeGh()
        with self.assertRaisesRegex(FetchError, "--force"):
            fetch("windows", "main", None, self.dest, False, gh)
        self.assertEqual(os.listdir(self.dest), ["NetHackGodot.exe"])
        with open(exe) as f:
            self.assertEqual(f.read(), "old")

    def test_force_replaces_an_existing_build(self):
        os.makedirs(self.dest)
        exe = os.path.join(self.dest, "NetHackGodot.exe")
        with open(exe, "w") as f:
            f.write("old")
        fetch("windows", "main", None, self.dest, True, FakeGh())
        with open(exe) as f:
            self.assertEqual(f.read(), "new")

    def test_every_platform_matches_an_artifact_uploaded_by_ci(self):
        ci = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                          ".github", "workflows", "ci.yml")
        with open(ci, encoding="utf-8") as f:
            workflow = f.read()
        self.assertEqual(set(fetch_build.ARTIFACTS), set(fetch_build.REQUIRED))
        for platform, artifact in fetch_build.ARTIFACTS.items():
            self.assertIn("name: %s\n" % artifact, workflow)
            # The exported executable (or macOS zip); the .pck sits beside it.
            self.assertIn("/%s\n" % fetch_build.REQUIRED[platform][0], workflow)


if __name__ == "__main__":
    unittest.main()
