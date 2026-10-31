"""Reject a missing, malformed or failing GDK Submission Validator report."""

import argparse
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
        ids = ", ".join(item.get("Id", "unknown") for item in failures)
        raise RuntimeError(f"Submission Validator did not pass: {ids or 'missing/incomplete results'}")
    print(f"Submission Validator passed: 0 failures, {len(warnings)} warnings")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    require_passing_report(parser.parse_args().report)
