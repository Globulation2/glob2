#!/usr/bin/env python3
"""Fail-closed gate for the selected CI inventory and its exact run attempt."""

import json
import os
import re
import sys
import time
import urllib.request

import ci_check_inventory as inventory


def _ids(value, field, errors):
    if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
        errors.append(f"Invalid plan {field}")
        return []
    if len(set(value)) != len(value):
        errors.append(f"Duplicate plan {field}")
    return value


def verify(needs, plan, jobs=None):
    """Return gate evidence; missing or ambiguous selected leaves never pass.

    jobs is the jobs API's list for the current attempt. If run_attempt is in the
    plan, older-attempt entries are excluded before resolving aliases.
    """
    errors = []
    verified = []
    result = {"ok": False, "errors": errors, "verified_ids": verified,
              "mode": plan.get("mode", "unspecified") if isinstance(plan, dict) else "unspecified"}
    if not isinstance(plan, dict) or type(plan.get("schema")) is not int or plan["schema"] != 1:
        errors.append("Missing or unsupported CI execution plan")
        return result
    if not isinstance(needs, dict):
        errors.append("Missing needs results")
        return result
    changes = needs.get("changes", {})
    if not isinstance(changes, dict) or changes.get("result") != "success":
        errors.append("Selection job changes must succeed")
    ranked = _ids(plan.get("ranked_ids"), "ranked_ids", errors)
    executed = _ids(plan.get("executed_ids"), "executed_ids", errors)
    dependencies = _ids(plan.get("dependency_ids"), "dependency_ids", errors)
    if not set(ranked) <= set(executed) or set(dependencies) != set(executed) - set(ranked):
        errors.append("Inconsistent ranked, dependency and executed check IDs")
    flags = plan.get("jobs")
    if not isinstance(flags, dict) or set(flags) != set(inventory.TOP_JOBS) or any(type(value) is not bool for value in flags.values()):
        errors.append("Plan must declare boolean flags for every workflow job")
        flags = {}
    for job, expected in flags.items():
        actual = needs.get(job, {})
        wanted = "success" if expected else "skipped"
        if not isinstance(actual, dict) or actual.get("result") != wanted:
            errors.append(f"Workflow job {job}: expected {wanted}, got {actual.get('result') if isinstance(actual, dict) else 'invalid result'}")
    expected_checks = plan.get("expected_checks")
    if not isinstance(expected_checks, list):
        errors.append("Missing expected check inventory")
        expected_checks = []
    by_id = {}
    for check in expected_checks:
        if (not isinstance(check, dict) or not isinstance(check.get("id"), str)
                or not isinstance(check.get("name"), str) or not check["name"]
                or not isinstance(check.get("history_names"), list)
                or not all(isinstance(alias, str) and alias for alias in check["history_names"])):
            errors.append("Invalid expected check entry")
            continue
        if check["id"] in by_id:
            errors.append("Duplicate expected check ID: " + check["id"])
        by_id[check["id"]] = check
    if set(by_id) != set(executed):
        errors.append("Expected check IDs differ from executed IDs")
    # Reconstruct independently so a missing dependency, enabled job, or matrix
    # option cannot be hidden by making two fields of a malformed plan agree.
    try:
        canonical = inventory.execution_plan(executed)
        if canonical["executed_ids"] != sorted(executed):
            errors.append("Executed inventory is missing required dependencies")
        if canonical["jobs"] != flags:
            errors.append("Job flags differ from executed inventory")
        if canonical["options"] != plan.get("options"):
            errors.append("Matrix options differ from executed inventory")
        for check in canonical["expected_checks"]:
            actual = by_id.get(check["id"])
            if actual and (actual["name"] != check["name"] or set(actual["history_names"]) != set(check["history_names"])):
                errors.append("Unexpected job aliases for " + check["id"])
    except (ValueError, KeyError, TypeError) as error:
        errors.append("Invalid executed inventory: " + str(error))
    if errors:
        return result
    if not expected_checks:
        result["ok"] = True
        return result
    if not isinstance(jobs, list):
        errors.append("Missing current-attempt jobs API evidence")
        return result
    attempt = plan.get("run_attempt")
    if attempt is not None and (type(attempt) is not int or attempt < 1):
        errors.append("Invalid current run attempt")
        return result
    current = []
    ids = set()
    for job in jobs:
        if not isinstance(job, dict) or not isinstance(job.get("name"), str):
            errors.append("Invalid jobs API entry")
            continue
        if attempt is not None and job.get("run_attempt", attempt) != attempt:
            continue
        if job.get("id") is not None:
            if type(job["id"]) is not int:
                errors.append("Invalid jobs API identity")
                continue
            if job["id"] in ids:
                errors.append("Duplicate jobs API identity: " + str(job["id"]))
                continue
            ids.add(job["id"])
        current.append(job)
    claimed = {}
    for cid, check in by_id.items():
        aliases = set(check["history_names"]) | {check["name"]}
        matches = [job for job in current if job["name"] in aliases
                   or any(job["name"].endswith(" / " + alias) for alias in aliases)]
        if len(matches) != 1:
            errors.append(f"Selected check {cid}: expected exactly one current-attempt job, found {len(matches)}")
            continue
        job = matches[0]
        identity = job.get("id", id(job))
        if identity in claimed:
            errors.append(f"Job {job['name']} ambiguously satisfies {claimed[identity]} and {cid}")
            continue
        claimed[identity] = cid
        if job.get("status") != "completed" or job.get("conclusion") != "success":
            errors.append(f"Selected check {cid}: expected completed success, got {job.get('status')}/{job.get('conclusion')}")
        else:
            verified.append(cid)
    result["ok"] = not errors
    return result


def _request_json(url, token):
    request = urllib.request.Request(url, headers={"Authorization": "Bearer " + token,
                                                  "Accept": "application/vnd.github+json",
                                                  "X-GitHub-Api-Version": "2022-11-28"})
    with urllib.request.urlopen(request, timeout=30) as response:
        payload = response.read(16 * 1024 * 1024 + 1)
    if len(payload) > 16 * 1024 * 1024:
        raise ValueError("Jobs API page is too large")
    return json.loads(payload)


def fetch_jobs(repository, run_id, attempt, token, request_json=None):
    """Fetch every page from the exact attempt endpoint, never latest-attempt."""
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid GitHub repository")
    if type(run_id) is not int or run_id < 1 or type(attempt) is not int or attempt < 1 or not token:
        raise ValueError("Missing GitHub run credentials")
    request_json = request_json or _request_json
    found = []
    identities = set()
    total = None
    for page in range(1, 101):
        url = f"https://api.github.com/repos/{repository}/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100&page={page}"
        data = request_json(url, token)
        if (not isinstance(data, dict) or type(data.get("total_count")) is not int
                or data["total_count"] < 0 or not isinstance(data.get("jobs"), list)):
            raise ValueError("Invalid jobs API response")
        total = max(total or 0, data["total_count"])
        for job in data["jobs"]:
            if not isinstance(job, dict) or type(job.get("id")) is not int:
                raise ValueError("Invalid jobs API identity")
            if job["id"] in identities:
                raise ValueError("Jobs API pages repeat a job identity")
            identities.add(job["id"])
            if job.get("run_attempt", attempt) != attempt:
                continue
            found.append(dict(job, run_attempt=attempt))
        if len(identities) >= total:
            return found
        if not data["jobs"]:
            raise ValueError("Incomplete jobs API pagination")
    raise ValueError("Jobs API exceeded pagination limit")


def main():
    try:
        needs = json.loads(os.environ.get("NEEDS_JSON", "null"))
        plan = json.loads(os.environ.get("CI_JOB_PLAN", "null"))
        jobs = None
        if isinstance(plan, dict) and plan.get("expected_checks"):
            attempt = int(os.environ["GITHUB_RUN_ATTEMPT"])
            plan = dict(plan, run_attempt=attempt)
            # Other prerequisite jobs have finished, but their API records may
            # appear shortly afterward. Retry three times, then fail closed.
            last = None
            result = None
            for retry in range(3):
                try:
                    jobs = fetch_jobs(os.environ["GITHUB_REPOSITORY"], int(os.environ["GITHUB_RUN_ID"]), attempt, os.environ["GH_TOKEN"])
                    result = verify(needs, plan, jobs)
                    if result["ok"]:
                        break
                    last = result
                except (OSError, ValueError, KeyError) as error:
                    last = {"ok": False, "errors": ["Unable to verify current-attempt jobs: " + str(error)], "verified_ids": []}
                if retry < 2:
                    time.sleep(2)
            result = result if result is not None and result.get("ok", False) else last
        else:
            result = verify(needs, plan)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {"ok": False, "errors": ["Invalid gate input: " + str(error)], "verified_ids": []}
    print(json.dumps(result, sort_keys=True))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
