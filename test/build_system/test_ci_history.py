"""History completion boundaries, retries, pagination and trusted bundle restore."""
import copy
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import urllib.parse
import zipfile

SCRIPT = Path(__file__).resolve().parents[2] / ".github/scripts/ci_history.py"
SPEC = importlib.util.spec_from_file_location("ci_history", SCRIPT)
history = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(history)
BASE = "https://api.github.com/repos/Globulation2/glob2"
SHA = "a" * 40


def empty():
    return {"schema": 1, "records": [], "updated_at": 1000}


def job(name="native tests", failed=False, end=200):
    return {"name": name, "conclusion": "failure" if failed else "success", "started_at": 150, "completed_at": end}


class FakeAPI:
    def __init__(self):
        self.info = {"id": 12, "full_name": "Globulation2/glob2", "default_branch": "master"}
        self.run = {"id": 9, "run_attempt": 1, "repository": self.info, "head_repository": self.info, "head_branch": "master", "head_sha": SHA, "event": "push", "status": "completed", "conclusion": "failure", "path": ".github/workflows/build.yml", "created_at": 100}
        self.jobs = [job()]
        self.files = [{"filename": "src/main.cpp"}]
        self.calls = []
        self.artifacts = []

    def __call__(self, url):
        self.calls.append(url)
        path = urllib.parse.urlparse(url).path
        page = int(urllib.parse.parse_qs(urllib.parse.urlparse(url).query).get("page", [1])[0])
        if url == BASE:
            return self.info
        if "/commits/" in path:
            return {"files": self.files[(page - 1) * 100:page * 100]}
        if path.endswith("/jobs"):
            return {"total_count": len(self.jobs), "jobs": self.jobs[(page - 1) * 100:page * 100]}
        if path.endswith("/artifacts"):
            return {"total_count": len(self.artifacts), "artifacts": self.artifacts[(page - 1) * 100:page * 100]}
        if "/workflows/" in path:
            return {"total_count": 1, "workflow_runs": [{"id": 9}]}
        if path.endswith("/actions/runs/9"):
            return self.run
        raise AssertionError(url)


def bundle(api, corrupt=False, extra=None, bad_history=False):
    api.run.update(event="workflow_run", conclusion="success", path=".github/workflows/ci-model.yml")
    files = {"model.json.gz": gzip.compress(json.dumps({"schema": 1, "trees": []}).encode()), "history.json.gz": gzip.compress(json.dumps(empty()).encode())}
    if bad_history:
        files["history.json.gz"] = b"\x1f\x8b\x08\x00" + b"\x00" * 6 + b"\x06" + b"\x00" * 8
    manifest = {"schema": 1, "files": {n: hashlib.sha256(v).hexdigest() for n, v in files.items()}}
    if corrupt:
        manifest["files"]["model.json.gz"] = "0" * 64
    files["manifest.json"] = json.dumps(manifest).encode()
    if extra:
        files.update(extra)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    payload = buffer.getvalue()
    api.artifacts = [{"id": 42, "name": "ci-predictor", "expired": False, "size_in_bytes": len(payload), "digest": "sha256:" + hashlib.sha256(payload).hexdigest(), "archive_download_url": BASE + "/actions/artifacts/42/zip", "workflow_run": {"id": 9, "repository_id": 12, "head_repository_id": 12, "head_branch": "master", "head_sha": SHA}}]
    return payload


class HistoryTest(unittest.TestCase):
    def collect(self, api, value=None):
        return history.collect_history("Globulation2/glob2", 9, value or empty(), request_json=api, now=1000)

    def test_paginate_jobs_and_commit_files_and_rename(self):
        api = FakeAPI()
        api.jobs = [job(f"job {i}") for i in range(101)]
        api.files = [{"filename": f"src/{i}.cpp"} for i in range(101)]
        api.files[-1]["previous_filename"] = "src/old.cpp"
        result = self.collect(api)
        self.assertEqual(len(result["records"]), 101)
        self.assertEqual(len(result["records"][0]["paths"]), 102)
        self.assertTrue(any("page=2" in u and "/jobs?" in u for u in api.calls))
        self.assertTrue(any("page=2" in u and "/commits/" in u for u in api.calls))

    def test_omit_pending_future_skipped_and_support_jobs(self):
        api = FakeAPI()
        pending = job("pending")
        pending["completed_at"] = None
        skipped = job("skipped")
        skipped["conclusion"] = "skipped"
        api.jobs = [job(), pending, skipped, job("future", end=1001), job("Select relevant checks"), job("Linux / Reuse primary Linux build"), job("Linux / Audit native shard coverage")]
        result = self.collect(api)
        self.assertEqual([r["name"] for r in result["records"]], ["native tests"])

    def test_linux_aggregate_results_are_not_independent_training_labels(self):
        api = FakeAPI()
        leaves = [
            "Primary Linux programs / linux build (ubuntu:24.04, g++-13)",
            "Linux GCC 13 build and tests / Linux tests (ubuntu-24.04, shard 2/4)",
            "Linux GCC 11 build and tests / Map generators (ubuntu-22.04, telemetry)",
            "linux variants (ubuntu:22.04)",
            "linux variants (ubuntu:24.04)",
            # Newly named substantive checks must survive a narrow helper filter.
            "linux (ubuntu:26.04)",
            "Linux integration (ubuntu:22.04)",
        ]
        api.jobs = [job(name, failed=True) for name in leaves]
        api.jobs += [job("linux (ubuntu:22.04)", failed=True),
                     job("linux (ubuntu:24.04)")]
        result = self.collect(api)
        self.assertEqual({record["name"] for record in result["records"]}, set(leaves))
        self.assertEqual(len(result["records"]), len(leaves))
        self.assertTrue(all(record["failed"] == 1 for record in result["records"]))

    def test_retry_dedup_keeps_attempt_identity(self):
        api = FakeAPI()
        first = self.collect(api)
        self.assertEqual(len(self.collect(api, first)["records"]), 1)
        api.run["run_attempt"] = 2
        api.jobs = [job(failed=True)]
        second = self.collect(api, first)
        self.assertEqual([(r["attempt"], r["failed"]) for r in second["records"]], [(1, 0), (2, 1)])

    def test_existing_sha_paths_avoid_commit_lookup(self):
        api = FakeAPI()
        seed = self.collect(api)
        api.calls.clear()
        self.collect(api, seed)
        self.assertFalse(any("/commits/" in u for u in api.calls))

    def test_unknown_paths_are_explicit_on_api_failure(self):
        api = FakeAPI()
        def request(url):
            if "/commits/" in url:
                raise OSError("offline")
            return api(url)
        result = history.collect_history("Globulation2/glob2", 9, empty(), request_json=request, now=1000)
        self.assertEqual(result["records"][0]["paths"], [])
        self.assertTrue(result["records"][0]["paths_unknown"])

    def test_reject_pr_fork_branch_and_incomplete_runs(self):
        for mutation in ({"event": "pull_request"}, {"head_branch": "feature"}, {"head_repository": {"id": 99, "full_name": "fork/glob2"}}, {"status": "in_progress"}, {"path": ".github/workflows/unrelated.yml"}):
            with self.subTest(mutation=mutation):
                api = FakeAPI()
                api.run.update(mutation)
                with self.assertRaises(ValueError):
                    self.collect(api)

    def test_load_save_and_invalid_schema(self):
        api = FakeAPI()
        value = self.collect(api)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "history.json.gz"
            history.save_history(value, path)
            self.assertEqual(history.load_history(path), value)
            history.save_history(value, path)
            with self.assertRaises(ValueError):
                history.validate_history({"schema": 2, "records": [], "updated_at": 0})

    def test_corrupt_deflate_is_normalized_for_offline_fallback(self):
        invalid = b"\x1f\x8b\x08\x00" + b"\x00" * 6 + b"\x06" + b"\x00" * 8
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "history.json.gz"
            path.write_bytes(invalid)
            with self.assertRaisesRegex(ValueError, "Invalid compressed CI history"):
                history.load_history(path)

    def test_invalid_record_values_rejected(self):
        value = self.collect(FakeAPI())
        for mutation in ({"failed": True}, {"failed": 2}, {"end": -1}, {"end": 90}, {"seconds": float("nan")}, {"paths": "src"}, {"run_id": -1}, {"sha": "not-a-sha"}):
            with self.subTest(mutation=mutation):
                changed = copy.deepcopy(value)
                changed["records"][0].update(mutation)
                with self.assertRaises(ValueError):
                    history.validate_history(changed)

    def test_seed_is_valid_completed_actual_history(self):
        value = history.load_history(SCRIPT.parent.parent / "ci/history.json.gz")
        self.assertGreater(len(value["records"]), 20000)
        self.assertTrue(all(r["end"] <= value["updated_at"] for r in value["records"]))
        self.assertTrue(all(isinstance(r["run_id"], int) for r in value["records"]))


class ArtifactTest(unittest.TestCase):
    def restore(self, api, payload, directory):
        return history.latest_artifact("Globulation2/glob2", directory, request_json=api, download=lambda url: payload)

    def test_verified_master_bundle(self):
        api = FakeAPI()
        payload = bundle(api)
        with tempfile.TemporaryDirectory() as directory:
            self.assertTrue(self.restore(api, payload, directory))
            self.assertEqual(history.load_history(Path(directory) / "history.json.gz"), empty())
            self.assertTrue((Path(directory) / "manifest.json").exists())

    def test_untrusted_artifact_repo_branch_run_and_digest_rejected(self):
        for field, value in (("repository_id", 99), ("head_repository_id", 99), ("head_branch", "feature"), ("id", 123), ("head_sha", "b" * 40)):
            with self.subTest(field=field):
                api = FakeAPI()
                payload = bundle(api)
                api.artifacts[0]["workflow_run"][field] = value
                with tempfile.TemporaryDirectory() as directory:
                    self.assertFalse(self.restore(api, payload, directory))
                    self.assertEqual(list(Path(directory).iterdir()), [])

    def test_bad_member_checksum_does_not_replace_fallback(self):
        api = FakeAPI()
        payload = bundle(api, corrupt=True)
        with tempfile.TemporaryDirectory() as directory:
            fallback = Path(directory) / "history.json.gz"
            fallback.write_bytes(b"existing seed")
            self.assertFalse(self.restore(api, payload, directory))
            self.assertEqual(fallback.read_bytes(), b"existing seed")

    def test_corrupt_history_with_valid_manifest_keeps_fallback(self):
        api = FakeAPI()
        payload = bundle(api, bad_history=True)
        with tempfile.TemporaryDirectory() as directory:
            fallback = Path(directory) / "history.json.gz"
            fallback.write_bytes(b"existing seed")
            self.assertFalse(self.restore(api, payload, directory))
            self.assertEqual(fallback.read_bytes(), b"existing seed")
            self.assertFalse((Path(directory) / "model.json.gz").exists())

    def test_untrusted_source_run(self):
        for mutation in ({"event": "pull_request"}, {"head_branch": "feature"}, {"conclusion": "failure"}, {"path": ".github/workflows/other.yml"}, {"head_repository": {"id": 99, "full_name": "fork/glob2"}}):
            with self.subTest(mutation=mutation):
                api = FakeAPI()
                payload = bundle(api)
                api.run.update(mutation)
                with tempfile.TemporaryDirectory() as directory:
                    self.assertFalse(self.restore(api, payload, directory))

    def test_offline_missing_and_expired_use_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            def offline(url):
                raise OSError("offline")
            self.assertFalse(history.latest_artifact("Globulation2/glob2", directory, request_json=offline))
            api = FakeAPI()
            payload = bundle(api)
            api.artifacts[0]["expired"] = True
            self.assertFalse(self.restore(api, payload, directory))

    def test_cross_host_redirect_drops_token_and_rejects_http(self):
        request = history.urllib.request.Request(BASE, headers={"Authorization": "Bearer secret"})
        redirect = history._SafeRedirect()
        target = redirect.redirect_request(request, None, 302, "Found", {}, "https://storage.example.test/archive")
        self.assertIsNone(target.get_header("Authorization"))
        with self.assertRaises(ValueError):
            redirect.redirect_request(request, None, 302, "Found", {}, "http://storage.example.test/archive")

    def test_archive_traversal_and_archive_digest_rejected(self):
        api = FakeAPI()
        payload = bundle(api, extra={"../escape": b"bad"})
        with tempfile.TemporaryDirectory() as directory:
            self.assertFalse(self.restore(api, payload, directory))
            payload = bundle(api)
            api.artifacts[0]["digest"] = "sha256:" + "0" * 64
            self.assertFalse(self.restore(api, payload, directory))


if __name__ == "__main__":
    unittest.main()
