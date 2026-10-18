#!/usr/bin/env python3
"""Dependency-free CI check ranking from completed historical outcomes.

The model is inert JSON, never Python pickle. Selection is a count budget over
logical checks; dependency closure and policy exceptions belong to the caller.
"""

import collections
import gzip
import json
import math
from pathlib import Path
import re
import struct
import time
import zlib

MODEL_FEATURE_SCHEMA = "history54-v1"
_MAX_MODEL_BYTES = 64 * 1024 * 1024
_STOP = {"tests", "test", "build", "src", "cpp", "hpp", "the", "and", "for", "with", "browser"}
_EXTENSIONS = (".cpp", ".h", ".hpp", ".py", ".yml", ".yaml", ".js", ".ts", ".sh", ".md", ".txt")


def _words(value):
    value = re.sub(r"([a-z])([A-Z])", r"\1 \2", value)
    return {word for word in re.findall("[a-z]+", value.lower()) if len(word) > 2 and word not in _STOP}


def _finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _records(history, prediction_time):
    if not isinstance(history, dict) or history.get("schema") != 1 or not isinstance(history.get("records"), list):
        return [], ["missing_or_invalid_history"]
    valid = []
    invalid = 0
    seen = set()
    for record in history["records"]:
        if not isinstance(record, dict) or not isinstance(record.get("name"), str) or not record["name"]:
            invalid += 1
            continue
        end = record.get("end")
        created = record.get("created_at")
        if (not _finite(end) or not _finite(created) or end < created
                or record.get("failed") not in (0, 1)
                or not isinstance(record.get("branch"), str)
                or not isinstance(record.get("event"), str)):
            invalid += 1
            continue
        if end >= prediction_time:
            continue
        key = (str(record.get("run_id", "")), str(record.get("attempt", "")), record["name"], end, record["failed"])
        if key not in seen:
            seen.add(key)
            valid.append(record)
    valid.sort(key=lambda record: (record["end"], record["created_at"], str(record.get("run_id", "")), str(record.get("attempt", "")), record["name"]))
    return valid, (["ignored_invalid_history_records"] if invalid else [])


def _matching(check, records):
    if isinstance(check, str):
        check = {"id": check, "name": check}
    aliases = check.get("history_names", [])
    if not isinstance(aliases, list) or not all(isinstance(alias, str) and alias for alias in aliases):
        raise ValueError("history_names must contain nonempty raw job names")
    aliases = set(aliases) | {check.get("name", check["id"])}
    return [record for record in records if record["name"] in aliases
            or any(record["name"].endswith(" / " + alias) for alias in aliases)]


def _features(check, paths, records, event, branch, prediction_time):
    if isinstance(check, str):
        check = {"id": check, "name": check}
    history = _matching(check, records)
    recent = history[-50:]
    changed = paths if paths is not None else []
    if not isinstance(changed, (list, tuple)) or not all(isinstance(path, str) for path in changed):
        raise ValueError("paths must be a list of strings, or None when unavailable")
    lexical = _words(" ".join(changed))
    tokens = _words(check.get("name", check["id"]))
    failure_rate = (sum(record["failed"] for record in recent) + .25) / (len(recent) + 5)
    features = {
        "last_failed": history[-1]["failed"] if history else 0,
        "failure_rate": failure_rate,
        "observations_log": math.log1p(len(history)),
        "unknown": int(not history),
        "semantic_overlap": len(tokens & lexical) / math.sqrt(max(1, len(tokens) * len(lexical))),
        "paths_unknown": int(paths is None),
        "changed_files_log": math.log1p(len(changed)),
    }
    for days in (1, 3, 7):
        window = [record for record in history if prediction_time - record["end"] < days * 86400]
        failures = sum(record["failed"] for record in window)
        features[f"failures_{days}d_log"] = math.log1p(failures)
        features[f"observations_{days}d_log"] = math.log1p(len(window))
        features[f"rate_{days}d"] = (failures + .25) / (len(window) + 5)
    for count in (5, 20):
        window = history[-count:]
        features[f"rate_last_{count}"] = (sum(record["failed"] for record in window) + .25) / (len(window) + 5)
    branch_history = [record for record in history if record["branch"] == branch]
    master_history = [record for record in history if record["branch"] == "master" and record["event"] == "push"]
    for prefix, window in (("global", history), ("branch", branch_history), ("master", master_history)):
        features[prefix + "_known"] = int(bool(window))
        features[prefix + "_last_failed"] = window[-1]["failed"] if window else 0
        features[prefix + "_n_log"] = math.log1p(len(window))
        features[prefix + "_rate"] = (sum(record["failed"] for record in window[-20:]) + .25) / (len(window[-20:]) + 5)
        features[prefix + "_age_log"] = math.log1p(min(30, (prediction_time - window[-1]["end"]) / 86400)) if window else math.log1p(30)
    streak = 0
    for record in reversed(history):
        if record["failed"] != history[-1]["failed"]:
            break
        streak += 1
    features["streak_log_signed"] = (1 if history and history[-1]["failed"] else -1) * math.log1p(streak)
    coarse = set()
    extensions = collections.Counter()
    for path in changed:
        dirs = path.split("/")[:-1]
        for depth in (1, 2):
            coarse.add("/".join(dirs[:depth]) if dirs else "<root>")
        extensions[Path(path).suffix.lower() or "<none>"] += 1
    features["path_dirs_log"] = math.log1p(len(coarse))
    for extension, count in extensions.items():
        if extension in _EXTENSIONS:
            features["fraction_ext:" + extension] = count / max(1, len(changed))
    features["event:" + event] = 1
    known = features["branch_known"]
    features["global_branch_disagree"] = int(features["global_last_failed"] != features["branch_last_failed"]) if known else 0
    features["branch_known_global_failed"] = known * features["global_last_failed"]
    features["branch_global_rate_delta"] = features["branch_rate"] - features["global_rate"] if known else 0
    features["master_global_rate_delta"] = features["master_rate"] - features["global_rate"] if features["master_known"] else 0
    return features


def make_features(check, paths, history, event="pull_request", branch="", prediction_time=None):
    """Build sparse history54-v1 inputs; absent indicator features mean zero.

    ``paths=None`` explicitly means unavailable source information. Historical
    completions at or after prediction_time never influence any feature.
    """
    prediction_time = time.time() if prediction_time is None else prediction_time
    if not _finite(prediction_time):
        raise ValueError("prediction_time must be a finite epoch timestamp")
    records, _ = _records(history, prediction_time)
    return _features(check, paths, records, event, branch, prediction_time)


def previous_outcome(check, history, prediction_time=None):
    """Return the last completed aliased outcome, or None without known history."""
    prediction_time = time.time() if prediction_time is None else prediction_time
    records, _ = _records(history, prediction_time)
    matched = _matching(check, records)
    return matched[-1]["failed"] if matched else None


def _allowed_features():
    sample = _features({"id": "sample", "name": "sample"}, ["a" + extension for extension in _EXTENSIONS], [], "pull_request", "", 0)
    return set(sample) | {"event:" + event for event in ("push", "workflow_dispatch", "schedule")}


def validate_model(model):
    """Validate inert tree arrays, rejecting malformed nodes and cycles up front."""
    if not isinstance(model, dict) or model.get("schema") != 1 or model.get("feature_schema") != MODEL_FEATURE_SCHEMA:
        raise ValueError("unsupported model schema")
    if not isinstance(model.get("model_id"), str) or not model["model_id"]:
        raise ValueError("missing model_id")
    names, scales, trees = (model.get(key) for key in ("feature_names", "scales", "trees"))
    if (not isinstance(names, list) or not names or len(names) > 128
            or not all(isinstance(name, str) for name in names) or len(set(names)) != len(names)
            or not set(names) <= _allowed_features()):
        raise ValueError("invalid feature names")
    if not isinstance(scales, list) or len(scales) != len(names) or not all(_finite(scale) and scale > 0 for scale in scales):
        raise ValueError("invalid feature scales")
    if not isinstance(trees, list) or not 1 <= len(trees) <= 1000:
        raise ValueError("invalid tree count")
    total = 0
    for tree in trees:
        if not isinstance(tree, dict) or any(not isinstance(tree.get(key), list) for key in ("left", "right", "feature", "threshold", "probability")):
            raise ValueError("invalid tree arrays")
        count = len(tree["left"])
        total += count
        if not 1 <= count <= 100000 or total > 1000000 or any(len(tree[key]) != count for key in ("right", "feature", "threshold", "probability")):
            raise ValueError("invalid tree shape")
        for node in range(count):
            left, right, feature = (tree[key][node] for key in ("left", "right", "feature"))
            if not all(type(value) is int for value in (left, right, feature)):
                raise ValueError("invalid node indices")
            if left == right == -1:
                if feature != -2:
                    raise ValueError("invalid leaf feature")
            elif not (0 <= left < count and 0 <= right < count and left != right and 0 <= feature < len(names)):
                raise ValueError("invalid internal node")
            if not _finite(tree["threshold"][node]) or not _finite(tree["probability"][node]) or not 0 <= tree["probability"][node] <= 1:
                raise ValueError("invalid node values")
        # Iterative traversal also rejects unreachable model garbage and DAGs.
        visited = set()
        pending = [0]
        while pending:
            node = pending.pop()
            if node in visited:
                raise ValueError("cycle or shared child in tree")
            visited.add(node)
            if tree["left"][node] != -1:
                pending.extend((tree["left"][node], tree["right"][node]))
        if len(visited) != count:
            raise ValueError("unreachable tree nodes")
    return model


def load_model(path):
    """Load a size-limited gzip JSON artifact and validate before traversal."""
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rb") as handle:
        payload = handle.read(_MAX_MODEL_BYTES + 1)
    if len(payload) > _MAX_MODEL_BYTES:
        raise ValueError("model exceeds decompressed size limit")
    return validate_model(json.loads(payload))


def _probability(features, model):
    # sklearn's tree traversal casts its transformed inputs to float32.
    scaled = [features.get(name, 0) / scale for name, scale in zip(model["feature_names"], model["scales"])]
    if any(not math.isfinite(value) or abs(value) > 3.4028234663852886e38 for value in scaled):
        raise ValueError("scaled features exceed finite float32 range")
    values = [struct.unpack("f", struct.pack("f", value))[0] for value in scaled]
    result = 0.0
    for tree in model["trees"]:
        node = 0
        while tree["left"][node] != -1:
            node = tree["left"][node] if values[tree["feature"][node]] <= tree["threshold"][node] else tree["right"][node]
        result += tree["probability"][node]
    return result / len(model["trees"])


def select_checks(inventory, paths, history, model=None, budget=.12, event="pull_request", branch="", prediction_time=None):
    """Rank logical checks, choosing ceil(budget*N), with a one-check minimum.

    Unknown checks have deterministic priority. The caller can mandate every
    unknown check and add dependency closure separately from this count budget.
    """
    if not _finite(budget) or not 0 < budget <= 1:
        raise ValueError("budget must be greater than zero and at most one")
    prediction_time = time.time() if prediction_time is None else prediction_time
    if not _finite(prediction_time):
        raise ValueError("prediction_time must be a finite epoch timestamp")
    if not isinstance(inventory, list) or not all(isinstance(check, dict) and isinstance(check.get("id"), str) and check["id"] for check in inventory):
        raise ValueError("inventory must contain nonempty check IDs")
    if len({check["id"] for check in inventory}) != len(inventory):
        raise ValueError("duplicate check IDs")
    records, fallback = _records(history, prediction_time)
    active_model = None
    if model is None:
        fallback.append("missing_model")
    else:
        try:
            active_model = load_model(model) if isinstance(model, (str, Path)) else validate_model(model)
        except (OSError, ValueError, TypeError, KeyError, OverflowError, EOFError, RecursionError, zlib.error) as error:
            fallback.append("invalid_model: " + str(error))
    feature_rows = {check["id"]: _features(check, paths, records, event, branch, prediction_time) for check in inventory}
    model_probabilities = {}
    if active_model is not None:
        try:
            model_probabilities = {cid: _probability(features, active_model) for cid, features in feature_rows.items()}
        except (OverflowError, ValueError, struct.error) as error:
            fallback.append("invalid_model_prediction: " + str(error))
            active_model = None
    scores, probabilities, unknown, previous, reasons = {}, {}, [], {}, {}
    for check in inventory:
        cid = check["id"]
        features = feature_rows[cid]
        prior = None if features["unknown"] else features["global_last_failed"]
        previous[cid] = prior
        if prior is None:
            unknown.append(cid)
            reasons[cid] = ["unknown_history_priority"]
        else:
            reasons[cid] = []
        if active_model is not None:
            probability = model_probabilities[cid]
            probabilities[cid] = probability
            scores[cid] = probability * (.5 if prior == 1 else 1)
            reasons[cid].append("learned_failure_score")
            if prior == 1:
                reasons[cid].append("previous_failure_discount_0.5")
        else:
            probabilities[cid] = None
            if features["branch_known"]:
                scores[cid] = 2 * features["branch_last_failed"] + features["branch_rate"]
                reasons[cid].append("branch_recent_failure_fallback")
            else:
                scores[cid] = 2 * features["last_failed"] + features["failure_rate"]
                reasons[cid].append("global_recent_failure_fallback")
    ranking = sorted(scores, key=lambda cid: (previous[cid] is not None, -scores[cid] if previous[cid] is not None else 0, cid))
    count = max(1, math.ceil(budget * len(inventory))) if inventory else 0
    return {
        "selected_ids": ranking[:count], "rankings": ranking, "scores": scores,
        "probabilities": probabilities, "previous": previous, "unknown_ids": sorted(unknown),
        "model_id": active_model["model_id"] if active_model else "branch-recent-failure-fallback",
        "feature_schema": MODEL_FEATURE_SCHEMA, "fallback_reasons": fallback,
        "reasons": reasons, "budget": budget, "selected_count": count,
        "inventory_count": len(inventory), "actual_budget": count / len(inventory) if inventory else 0,
        "prediction_time": prediction_time,
    }
