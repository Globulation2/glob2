"""Reject a missing, malformed or failing GDK Submission Validator report."""

import argparse
import hashlib
import tempfile
import urllib.request
from zipfile import ZipFile
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


VALIDATOR_VERSION = "2610.309.15000.0"
VALIDATOR_SHA256 = "cbe4a3657ab7a3833106c8f164e05e394e2f29db93653f8c3013d03b5d53beed"
VALIDATOR_URL = ("https://developer.microsoft.com/content/dam/microsoftgamedev/subval/"
                 f"SubmissionValidator{VALIDATOR_VERSION}.zip")
VALIDATOR_FILES = {"SubmissionValidator.dll", "ReleaseNotes_submissionvalidator.txt", "ThirdPartyNotices.txt"}


def stage_validator(archive, destination):
    with open(archive, "rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != VALIDATOR_SHA256:
        raise RuntimeError("Microsoft validator ZIP digest mismatch")
    with ZipFile(archive) as bundle:
        if len(bundle.infolist()) != len(VALIDATOR_FILES) or set(bundle.namelist()) != VALIDATOR_FILES:
            raise RuntimeError("Unexpected Microsoft validator ZIP layout")
        # Fixed basenames prevent archive traversal; read and CRC-check before writes.
        content = {name: bundle.read(name) for name in VALIDATOR_FILES}
    destination.mkdir(parents=True, exist_ok=True)
    for name, data in content.items():
        (destination / name).write_bytes(data)
    print(f"Microsoft validator version={VALIDATOR_VERSION} ZIP SHA256={digest}")


def prepare_validator(destination):
    with tempfile.TemporaryDirectory() as temporary:
        archive = Path(temporary) / "validator.zip"
        urllib.request.urlretrieve(VALIDATOR_URL, archive)
        stage_validator(archive, destination)


def require_passing_report(path):
    root = ET.parse(path).getroot()
    summary = root.find("ValidationSummary")
    if root.tag != "XboxOneSubmissionValidator" or summary is None:
        raise RuntimeError("Missing Submission Validator summary")
    failures = root.findall(".//failure")
    warnings = root.findall(".//warning")
    try:
        declared_failures = int(summary.findtext("TotalFailures", ""))
        declared_warnings = int(summary.findtext("TotalWarnings", ""))
    except ValueError as error:
        raise RuntimeError("Invalid Submission Validator result counts") from error
    if (declared_failures != len(failures) or declared_warnings != len(warnings)
            or declared_failures < 0 or declared_warnings < 0):
        raise RuntimeError("Submission Validator result counts disagree with report")
    if (summary.findtext("Result") != "Pass" or root.findtext("validatorrun/result") != "Pass"
            or failures or not root.findall("testresults/testresult")):
        ids = sorted({item.get("Id", "") for item in failures})
        ids = [value if re.fullmatch(r"[A-Za-z0-9_]{1,128}", value) else "invalid-id"
               for value in ids]
        raise RuntimeError(f"Submission Validator did not pass: failures={declared_failures}, "
                           f"warnings={declared_warnings}, IDs={','.join(ids) or 'none'}")
    if root.findtext("engineversion") != VALIDATOR_VERSION:
        raise RuntimeError("Submission Validator engine does not match the reviewed version pin")
    print(f"Submission Validator passed: 0 failures, {len(warnings)} warnings")


def annotation_escape(value):
    return value.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def check_cli(path):
    try:
        require_passing_report(path)
    except (OSError, ET.ParseError):
        print("::error::Submission Validator XML report is missing or malformed.")
        return 1
    except RuntimeError as error:
        # Messages contain fixed diagnostics, numeric counts and validated IDs only.
        # Never expose XML failure prose, file paths, environment values or keys.
        print("::error::" + annotation_escape(str(error)))
        return 1
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, nargs="?")
    parser.add_argument("--prepare", type=Path)
    args = parser.parse_args()
    if bool(args.report) == bool(args.prepare):
        parser.error("provide a report or --prepare directory")
    if args.prepare:
        prepare_validator(args.prepare)
    else:
        sys.exit(check_cli(args.report))
