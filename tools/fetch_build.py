#!/usr/bin/env python3
"""Download a ready-to-run export of the game from CI into a folder.

    fetch_build.py [--platform windows|linux|macos] [--branch BRANCH]
                   [--repo OWNER/NAME] [--force] DEST

CI (.github/workflows/ci.yml) exports the game for every push and uploads
each platform as a workflow artifact.  This script finds the newest
successful CI run for BRANCH (default: the branch checked out here), then
downloads that platform's artifact into DEST (default platform: windows,
which gives DEST/NetHackGodot.exe with its .pck and GDExtension .dll).
Files already in DEST are left alone unless --force is given.

It needs the GitHub CLI (gh), logged in with read access to the repository.
Without --repo, gh uses the repository of the current directory.

Exit status 0 on success, 1 when no build is found or the download fails,
2 on a usage error.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

WORKFLOW = "CI"

ARTIFACTS = {
    "windows": "NetHackGodot-windows-x86_64",
    "linux": "NetHackGodot-linux-x86_64",
    "macos": "NetHackGodot-macos-universal",
}

# Files the export step in ci.yml writes; the download must contain them.
REQUIRED = {
    "windows": ("NetHackGodot.exe", "NetHackGodot.pck"),
    "linux": ("NetHackGodot.x86_64", "NetHackGodot.pck"),
    "macos": ("NetHackGodot.zip",),
}


class FetchError(Exception):
    pass


def repo_args(repo):
    return ["-R", repo] if repo else []


def list_runs_command(branch, repo):
    return ["gh", "run", "list", *repo_args(repo), "--workflow", WORKFLOW,
            "--branch", branch, "--status", "success", "--limit", "1",
            "--json", "databaseId,headSha"]


def download_command(run_id, platform, dest, repo):
    return ["gh", "run", "download", str(run_id), *repo_args(repo),
            "-n", ARTIFACTS[platform], "-D", dest]


def pick_run(list_output, branch):
    runs = json.loads(list_output or "[]")
    if not runs:
        raise FetchError("no successful %s run found for branch %r" % (WORKFLOW, branch))
    return runs[0]["databaseId"], runs[0]["headSha"]


def missing_files(platform, dest):
    return [n for n in REQUIRED[platform] if not os.path.isfile(os.path.join(dest, n))]


def current_branch():
    out = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"],
                         capture_output=True, text=True)
    branch = out.stdout.strip()
    return branch if out.returncode == 0 and branch and branch != "HEAD" else "main"


def run(cmd):
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        raise FetchError("%s failed: %s" % (" ".join(cmd), out.stderr.strip()))
    return out.stdout


def remove(path):
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path)
    else:
        os.remove(path)


def fetch(platform, branch, repo, dest, force, runner=run):
    """Download into a staging folder inside DEST, check it, then move the
    files into DEST.  Existing files are only replaced with force."""
    run_id, sha = pick_run(runner(list_runs_command(branch, repo)), branch)
    os.makedirs(dest, exist_ok=True)
    staging = tempfile.mkdtemp(prefix=".fetch-build-", dir=dest)
    try:
        runner(download_command(run_id, platform, staging, repo))
        missing = missing_files(platform, staging)
        if missing:
            raise FetchError("the download is missing %s" % ", ".join(missing))
        names = sorted(os.listdir(staging))
        clash = [n for n in names if os.path.lexists(os.path.join(dest, n))]
        if clash and not force:
            raise FetchError("%s already holds %s; pass --force to replace them"
                             % (dest, ", ".join(clash)))
        for name in names:
            target = os.path.join(dest, name)
            if os.path.lexists(target):
                remove(target)
            os.replace(os.path.join(staging, name), target)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    return run_id, sha, names


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    parser.add_argument("dest")
    parser.add_argument("--platform", choices=sorted(ARTIFACTS), default="windows")
    parser.add_argument("--branch")
    parser.add_argument("--repo")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv[1:])
    branch = args.branch or current_branch()
    try:
        run_id, sha, names = fetch(args.platform, branch, args.repo, args.dest, args.force)
    except FetchError as e:
        print("FAIL: %s" % e)
        return 1
    print("%s build from CI run %s (%s, commit %s) is in %s:"
          % (args.platform, run_id, branch, sha[:7], os.path.abspath(args.dest)))
    for name in names:
        print("  " + name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
