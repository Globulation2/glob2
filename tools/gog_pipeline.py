#!/usr/bin/env python3
"""Render GOG projects and invoke the official Pipeline Builder."""

import argparse
import json
import os
import re
import subprocess
from pathlib import Path

from gog_release import verify_manifest


ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = {
    "windows": ("windows", "Globulation 2", "glob2.exe"),
    "macos": ("osx", "Glob2.app", "Contents/MacOS/glob2"),
    "linux": ("linux", "Globulation 2", "start.sh"),
}


def render(platform, depot, output, build_version, product_id, base_product_id):
    if not re.fullmatch(r"[A-Za-z0-9.+_-]+", build_version):
        raise ValueError("unsafe GOG build version")
    if not product_id.isdecimal() or not base_product_id.isdecimal():
        raise ValueError("GOG product IDs must be numeric")
    info = verify_manifest(depot, platform)
    if not build_version.startswith(info["version"] + "-"):
        raise ValueError("GOG build version does not match game version")
    gog_platform, directory, launch = PLATFORMS[platform]
    depot_folder = depot / "Glob2.app" if platform == "macos" else depot
    replacements = {
        "@BASE_PRODUCT_ID@": base_product_id,
        "@PRODUCT_ID@": product_id,
        "@INSTALL_DIRECTORY@": directory,
        "@PLATFORM@": gog_platform,
        "@BUILD_VERSION@": build_version,
        "@DEPOT@": str(depot_folder.resolve()),
        "@LAUNCH_PATH@": launch,
    }
    template = (ROOT / "gog/project-template.json").read_text()
    for token, value in replacements.items():
        template = template.replace(token, value.replace("\\", "\\\\"))
    if re.search(r"@[A-Z_]+@", template):
        raise ValueError("unrendered template placeholder")
    project = json.loads(template)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(project, sort_keys=True, indent=2) + "\n")
    return project


def run_builder(builder, project, branch, offline):
    if not builder.is_file():
        raise ValueError(f"Pipeline Builder missing: {builder}")
    command = [str(builder), "build-game", str(project)]
    if offline:
        command.extend(("--offline", "--output", str(project.parent / f"offline-{project.stem}")))
    else:
        if branch != "Staging":
            raise ValueError("only the private Staging branch is allowed")
        username = os.environ.get("GOG_USERNAME")
        password = os.environ.get("GOG_PASSWORD")
        branch_password = os.environ.get("GOG_STAGING_PASSWORD")
        if not all((username, password, branch_password)):
            raise ValueError("GOG upload credentials are incomplete")
        command.extend(("--branch", "Staging", "--branch_password", branch_password,
                        "--username", username, "--password", password))
    # Do not print the command: the official CLI accepts passwords as arguments.
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=PLATFORMS, required=True)
    parser.add_argument("--depot", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--build-version", required=True)
    parser.add_argument("--product-id", required=True)
    parser.add_argument("--base-product-id", required=True)
    parser.add_argument("--builder", type=Path)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--upload-staging", action="store_true")
    args = parser.parse_args()
    if args.offline and args.upload_staging:
        parser.error("choose offline validation or Staging upload")
    render(args.platform, args.depot.resolve(), args.project.resolve(),
           args.build_version, args.product_id, args.base_product_id)
    if args.offline or args.upload_staging:
        if args.builder is None:
            parser.error("Pipeline Builder path is required")
        run_builder(args.builder.resolve(), args.project.resolve(), "Staging", args.offline)


if __name__ == "__main__":
    main()
