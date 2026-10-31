"""Reject a missing, malformed or failing GDK Submission Validator report."""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


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
    parser.add_argument("report", type=Path)
    sys.exit(check_cli(parser.parse_args().report))
