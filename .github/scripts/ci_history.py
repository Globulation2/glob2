#!/usr/bin/env python3
"""Collect completed CI outcomes and restore model bundles from trusted master CI.

Only stdlib is needed on a PR runner. Download failures leave the bundled seed
untouched. A checksum verifies integrity; trusted workflow/run identity establishes
where a bundle came from.
"""
from __future__ import annotations

import argparse
import datetime as dt
import gzip
import hashlib
import http.client
import io
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
import zlib

SCHEMA = 1
MAX_DOWNLOAD_BYTES = 64 * 1024 * 1024
MAX_MEMBER_BYTES = 128 * 1024 * 1024
COLLECT_EVENTS = {"push", "schedule", "workflow_dispatch"}
ARTIFACT_EVENTS = COLLECT_EVENTS | {"workflow_run"}
OMIT_NAMES = {
    "Relevant checks passed", "Select relevant checks", "linux-build",
    "linux-map-generators", "Cheap contracts", "Predict PR checks",
    "Refresh CI predictor", "Train CI predictor",
}
OMIT_PARTS = ("Audit native shard coverage", "Reuse primary Linux build")


def epoch(value):
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        result = float(value)
    elif isinstance(value, str):
        result = dt.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
    else:
        raise ValueError("Invalid timestamp")
    if not math.isfinite(result) or result < 0:
        raise ValueError("Invalid timestamp")
    return result


def validate_history(value):
    if not isinstance(value, dict) or value.get("schema") != SCHEMA:
        raise ValueError("Unsupported CI history schema")
    epoch(value.get("updated_at"))
    if not isinstance(value.get("records"), list):
        raise ValueError("History records must be a list")
    for record in value["records"]:
        if not isinstance(record, dict):
            raise ValueError("Invalid history record")
        for field in ("name", "branch", "event", "sha"):
            if not isinstance(record.get(field), str) or not record[field]:
                raise ValueError(f"Invalid history {field}")
        if not re.fullmatch(r"[0-9a-f]{40,64}", record["sha"]):
            raise ValueError("Invalid source SHA")
        for field in ("run_id", "attempt"):
            if type(record.get(field)) is not int or record[field] < 1:
                raise ValueError(f"Invalid history {field}")
        start, end = epoch(record.get("created_at")), epoch(record.get("end"))
        if end < start or type(record.get("failed")) is not int or record["failed"] not in (0, 1):
            raise ValueError("Invalid history outcome")
        epoch(record.get("seconds"))
        if not isinstance(record.get("paths"), list) or not all(isinstance(p, str) for p in record["paths"]):
            raise ValueError("Invalid changed paths")
        if "paths_unknown" in record and type(record["paths_unknown"]) is not bool:
            raise ValueError("Invalid paths_unknown flag")
    return value


def load_history(path):
    """Read and validate JSON or gzip history; malformed input is never usable."""
    path = Path(path)
    if path.stat().st_size > MAX_MEMBER_BYTES:
        raise ValueError("History file is too large")
    data = path.read_bytes()
    if data.startswith(b"\x1f\x8b"):
        try:
            with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:
                data = stream.read(MAX_MEMBER_BYTES + 1)
        except (OSError, EOFError, zlib.error) as exc:
            raise ValueError("Invalid compressed CI history") from exc
    if len(data) > MAX_MEMBER_BYTES:
        raise ValueError("History expands beyond size limit")
    return validate_history(json.loads(data))


def save_history(history, path):
    validate_history(history)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(history, separators=(",", ":"), sort_keys=True).encode()
    if path.suffix == ".gz":
        data = gzip.compress(data, mtime=0)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


class _SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, newurl):
        target = urllib.parse.urlparse(newurl)
        if target.scheme != "https":
            raise ValueError("Refusing non-HTTPS artifact redirect")
        redirected = super().redirect_request(request, fp, code, msg, headers, newurl)
        if redirected and target.netloc != urllib.parse.urlparse(request.full_url).netloc:
            redirected.remove_header("Authorization")
        return redirected


class GitHub:
    def __init__(self, token=None):
        self.token = token if token is not None else os.environ.get("GH_TOKEN", os.environ.get("GITHUB_TOKEN", ""))
        self.opener = urllib.request.build_opener(_SafeRedirect())

    def download(self, url):
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https" or parsed.netloc != "api.github.com":
            raise ValueError("GitHub requests must begin at api.github.com")
        headers = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "glob2-ci-predictor"}
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        for attempt in range(3):
            try:
                with self.opener.open(urllib.request.Request(url, headers=headers), timeout=30) as response:
                    data = response.read(MAX_DOWNLOAD_BYTES + 1)
                if len(data) > MAX_DOWNLOAD_BYTES:
                    raise ValueError("GitHub response exceeds size limit")
                return data
            except urllib.error.HTTPError as exc:
                if exc.code not in (429, 500, 502, 503, 504) or attempt == 2:
                    raise
            except (TimeoutError, urllib.error.URLError):
                if attempt == 2:
                    raise
            time.sleep(attempt + 1)
        raise RuntimeError("GitHub request failed")

    def json(self, url):
        return json.loads(self.download(url))


def _repo(repo):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo) or ".." in repo:
        raise ValueError("Expected an owner/repository name")
    return f"https://api.github.com/repos/{repo}"


def _pages(request_json, url, key=None):
    page = 1
    while True:
        data = request_json(f"{url}{'&' if '?' in url else '?'}per_page=100&page={page}")
        items = data[key] if key else data
        if not isinstance(items, list):
            raise ValueError("Invalid paginated GitHub response")
        yield from items
        total = data.get("total_count") if key else None
        if len(items) < 100 and (total is None or page * 100 >= total):
            break
        if not items:
            raise ValueError("Incomplete GitHub pagination")
        page += 1
        if page > 1000:
            raise ValueError("GitHub pagination exceeds limit")


def _trusted_run(run, repo_info, repo, events):
    for field in ("repository", "head_repository"):
        value = run.get(field) or {}
        if value.get("full_name", "").lower() != repo.lower() or value.get("id") != repo_info["id"]:
            raise ValueError("Run repository does not match trusted base repository")
    if run.get("head_branch") != repo_info["default_branch"] or repo_info["default_branch"] != "master":
        raise ValueError("Only master-sourced CI is trusted")
    if run.get("event") not in events:
        raise ValueError("Untrusted CI trigger")
    if run.get("status") != "completed" or run.get("conclusion") not in ("success", "failure"):
        raise ValueError("CI run has not completed")


def substantive_job(job):
    return (
        job.get("conclusion") in ("success", "failure")
        and bool(job.get("started_at")) and bool(job.get("completed_at"))
        and job.get("name") not in OMIT_NAMES
        and not any(part in job.get("name", "") for part in OMIT_PARTS)
    )


def collect_history(repo, run_id, history, *, token=None, request_json=None, now=None):
    """Merge one completed master build, preserving separate workflow attempts."""
    validate_history(history)
    base = _repo(repo)
    request_json = request_json or GitHub(token).json
    now = time.time() if now is None else epoch(now)
    repo_info = request_json(base)
    if repo_info.get("full_name", "").lower() != repo.lower():
        raise ValueError("Repository identity mismatch")
    run = request_json(f"{base}/actions/runs/{int(run_id)}")
    _trusted_run(run, repo_info, repo, COLLECT_EVENTS)
    if run.get("path", "").split("@", 1)[0] != ".github/workflows/build.yml":
        raise ValueError("Only the full build workflow supplies history")
    # Query this exact attempt, so a retried run cannot silently mix attempts.
    attempt = int(run.get("run_attempt", 1))
    if attempt < 1 or run.get("id") != int(run_id):
        raise ValueError("Invalid workflow attempt identity")
    sha = run["head_sha"]
    cached = next((r for r in history["records"] if r["sha"] == sha and not r.get("paths_unknown", False)), None)
    if cached is not None:
        paths, unknown = cached["paths"], False
    else:
        paths, unknown = [], False
        try:
            page = 1
            while True:
                commit = request_json(f"{base}/commits/{sha}?per_page=100&page={page}")
                files = commit.get("files")
                if not isinstance(files, list):
                    raise ValueError("Missing commit files")
                for file in files:
                    paths.append(file["filename"])
                    if file.get("previous_filename"):
                        paths.append(file["previous_filename"])
                if len(files) < 100:
                    break
                page += 1
                if page > 30:
                    raise ValueError("Commit paths are truncated")
        except (OSError, ValueError, KeyError):
            # Unknown source information is represented explicitly; never infer
            # an empty change from a denied or truncated API response.
            paths, unknown = [], True
    paths = sorted(set(paths))
    merged = {(r["run_id"], r["attempt"], r["name"]): r for r in history["records"] if r["end"] <= now}
    created = epoch(run["created_at"])
    jobs_url = f"{base}/actions/runs/{int(run_id)}/attempts/{attempt}/jobs"
    for job in _pages(request_json, jobs_url, "jobs"):
        if not substantive_job(job):
            continue
        end, started = epoch(job["completed_at"]), epoch(job["started_at"])
        if end > now or end < started or end < created:
            continue
        record = {"name": job["name"], "branch": run["head_branch"], "event": run["event"], "sha": sha, "run_id": int(run_id), "attempt": attempt, "created_at": created, "end": end, "seconds": end - started, "failed": int(job["conclusion"] == "failure"), "paths": paths, "paths_unknown": unknown}
        merged[int(run_id), attempt, job["name"]] = record
    value = {"schema": SCHEMA, "records": sorted(merged.values(), key=lambda r: (r["end"], r["run_id"], r["attempt"], r["name"])), "updated_at": now}
    return validate_history(value)


def fetch_latest_bundle(repo, destination, *, workflow="ci-model.yml", artifact="ci-predictor", token=None, request_json=None, download=None):
    """Restore only a verified successful master training artifact.

    Return False on missing/offline/invalid artifacts, leaving existing seed files
    untouched. Extract named regular files only, never arbitrary ZIP paths.
    """
    try:
        base = _repo(repo)
        api = GitHub(token)
        request_json = request_json or api.json
        download = download or api.download
        repo_info = request_json(base)
        if repo_info.get("full_name", "").lower() != repo.lower():
            raise ValueError("Repository identity mismatch")
        quoted = urllib.parse.quote(workflow, safe="")
        url = f"{base}/actions/workflows/{quoted}/runs?branch=master&status=success"
        candidate = None
        # Runs are returned newest first; a deleted/expired artifact can be
        # skipped in favor of an older, still valid successful master run.
        for index, listed in enumerate(_pages(request_json, url, "workflow_runs")):
            if index >= 20:
                break
            run = request_json(f"{base}/actions/runs/{int(listed['id'])}")
            _trusted_run(run, repo_info, repo, ARTIFACT_EVENTS)
            if run.get("conclusion") != "success" or run.get("path", "").split("@", 1)[0] != ".github/workflows/" + workflow:
                raise ValueError("Untrusted model workflow")
            for item in _pages(request_json, f"{base}/actions/runs/{run['id']}/artifacts", "artifacts"):
                if item.get("name") != artifact or item.get("expired"):
                    continue
                origin = item.get("workflow_run") or {}
                if any((origin.get("id") != run["id"], origin.get("repository_id") != repo_info["id"], origin.get("head_repository_id") != repo_info["id"], origin.get("head_branch") != "master", origin.get("head_sha") != run["head_sha"])):
                    raise ValueError("Artifact does not belong to trusted master run")
                if item.get("size_in_bytes", MAX_DOWNLOAD_BYTES + 1) > MAX_DOWNLOAD_BYTES:
                    raise ValueError("Artifact exceeds size limit")
                archive_url = f"{base}/actions/artifacts/{int(item['id'])}/zip"
                if item.get("archive_download_url") != archive_url:
                    raise ValueError("Unexpected artifact download URL")
                payload = download(archive_url)
                digest = item.get("digest")
                if digest and digest != "sha256:" + hashlib.sha256(payload).hexdigest():
                    raise ValueError("Artifact archive checksum mismatch")
                if len(payload) > MAX_DOWNLOAD_BYTES:
                    raise ValueError("Artifact exceeds size limit")
                candidate = payload
                break
            if candidate is not None:
                break
        if candidate is None:
            return False
        names = {"manifest.json", "model.json.gz", "history.json.gz"}
        with zipfile.ZipFile(io.BytesIO(candidate)) as archive:
            entries = archive.infolist()
            if len(entries) != 3 or {e.filename for e in entries} != names:
                raise ValueError("Unexpected model bundle members")
            if any(e.file_size > MAX_MEMBER_BYTES or e.is_dir() or (e.external_attr >> 16) & 0o170000 == 0o120000 for e in entries):
                raise ValueError("Invalid model bundle member")
            contents = {name: archive.read(name) for name in names}
        manifest = json.loads(contents["manifest.json"])
        if manifest.get("schema") != SCHEMA or set(manifest.get("files", {})) != names - {"manifest.json"}:
            raise ValueError("Invalid model manifest")
        for name, digest in manifest["files"].items():
            if digest != hashlib.sha256(contents[name]).hexdigest():
                raise ValueError("Model member checksum mismatch")
        with tempfile.TemporaryDirectory() as staging:
            staging = Path(staging)
            for name, content in contents.items():
                (staging / name).write_bytes(content)
            load_history(staging / "history.json.gz")
            # Check the model envelope here; predictor validates trees/features.
            with gzip.open(staging / "model.json.gz", "rb") as stream:
                model_data = stream.read(MAX_MEMBER_BYTES + 1)
            if len(model_data) > MAX_MEMBER_BYTES:
                raise ValueError("Model expands beyond size limit")
            model = json.loads(model_data)
            if model.get("schema") != SCHEMA:
                raise ValueError("Unsupported model schema")
            destination = Path(destination)
            destination.mkdir(parents=True, exist_ok=True)
            for name in sorted(names):
                temporary = destination / (name + ".download")
                temporary.write_bytes(contents[name])
                os.replace(temporary, destination / name)
        return True
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile, EOFError, zlib.error, http.client.HTTPException) as exc:
        print(f"CI predictor artifact unavailable; using bundled fallback ({type(exc).__name__})", file=sys.stderr)
        return False


latest_artifact = fetch_latest_bundle


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    collect = sub.add_parser("collect")
    collect.add_argument("--repo", required=True)
    collect.add_argument("--run-id", required=True, type=int)
    collect.add_argument("--history", required=True)
    collect.add_argument("--output", required=True)
    bundle = sub.add_parser("bundle")
    bundle.add_argument("--repo", required=True)
    bundle.add_argument("--directory", required=True)
    bundle.add_argument("--workflow", default="ci-model.yml")
    args = parser.parse_args(argv)
    if args.command == "collect":
        save_history(collect_history(args.repo, args.run_id, load_history(args.history)), args.output)
    else:
        restored = fetch_latest_bundle(args.repo, args.directory, workflow=args.workflow)
        print(json.dumps({"restored": restored}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
