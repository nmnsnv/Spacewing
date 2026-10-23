#!/usr/bin/env python3
"""Select a release version from stable Git tags without changing the source tree."""

import argparse
from pathlib import Path
import re
import subprocess


VERSION_PATTERN = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


def parse_version(value):
    if not VERSION_PATTERN.fullmatch(value):
        raise ValueError(f"Expected a version like 1.2.3, got {value!r}")
    return tuple(map(int, value.split(".")))


def stable_versions(tags):
    return [parse_version(tag[1:]) for tag in tags
            if tag.startswith("v") and VERSION_PATTERN.fullmatch(tag[1:])]


def select_version(baseline, tags, head_tags, bump="patch", tag=""):
    if tag:
        if not tag.startswith("v"):
            raise ValueError("Release tags must have the form v1.2.3")
        return parse_version(tag[1:])

    # Reuse a tag reserved by a previous attempt so retries can finish its draft.
    existing = stable_versions(head_tags)
    if existing:
        return max(existing)

    major, minor, patch = max([parse_version(baseline), *stable_versions(tags)])
    if bump == "major":
        return major + 1, 0, 0
    if bump == "minor":
        return major, minor + 1, 0
    if bump == "patch":
        return major, minor, patch + 1
    raise ValueError(f"Unknown version increment: {bump}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bump", choices=("patch", "minor", "major"), default="patch")
    parser.add_argument("--tag", default="", help="Use an existing release tag without incrementing")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    def git_tags(*options):
        return subprocess.check_output(["git", "-C", str(root), "tag", *options],
                                       text=True).splitlines()

    try:
        version = select_version((root / "VERSION").read_text().strip(),
                                 git_tags("--list"), git_tags("--points-at", "HEAD"),
                                 args.bump, args.tag)
    except ValueError as error:
        parser.error(str(error))
    print(".".join(map(str, version)))


if __name__ == "__main__":
    main()
