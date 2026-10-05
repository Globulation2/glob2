# SPDX-License-Identifier: GPL-3.0-or-later
"""Data boundary between untrusted Python compositions and trusted rendering."""

import dataclasses
import hashlib
import json
import math
import re
from types import SimpleNamespace

from ..score import Note, Part, Score
from ..spec import MOODS

MAX_JSON = 4 * 1024 * 1024
MAX_NOTES = 24000


def export(composition):
    score = dataclasses.asdict(composition.SCORE)
    # Callable performance hooks never cross the data boundary.
    score.pop("accent", None)
    return {
        "score": score,
        "parts": {
            m: [dataclasses.asdict(p) for p in composition.arrange(m)] for m in MOODS
        },
        "mix": getattr(composition, "MIX_ADJUST", {}),
    }


def number(value, low, high):
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
        or not low <= value <= high
    ):
        raise ValueError(f"Expected a finite number in {low}..{high}")
    return value


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,64}", value):
        raise ValueError("Invalid instrument, part or role identifier")
    return value


def load(text, instruments):
    if len(text.encode()) > MAX_JSON:
        raise ValueError("Score exceeds size limit")
    data = json.loads(
        text,
        parse_constant=lambda _: (_ for _ in ()).throw(ValueError("Non-finite JSON")),
    )
    raw = data["score"]
    if (
        not isinstance(raw, dict)
        or set(raw) - {f.name for f in dataclasses.fields(Score)}
        or "accent" in raw
    ):
        raise ValueError("Unsupported score fields")
    bars = number(raw["bars"], 8, 96)
    bpb = number(raw["beats_per_bar"], 2, 7)
    if int(bars) != bars or int(bpb) != bpb:
        raise ValueError("Bar and beat counts must be integers")
    number(raw["bpm"], 40, 180)
    total = bars * bpb
    count = 0

    def notes(values):
        nonlocal count
        if not isinstance(values, list):
            raise ValueError("Notes must be an array")
        count += len(values)
        if count > MAX_NOTES:
            raise ValueError("Too many notes")
        result = []
        for n in values:
            start, dur = number(n["start"], 0, total), number(n["dur"], 0.03125, total)
            if start + dur > total + 1 or len(n["pitches"]) > 12:
                raise ValueError(
                    "Note extends beyond the timeline or has too many pitches"
                )
            pitches = [number(p, 0, 127) for p in n["pitches"]]
            if any(int(p) != p for p in pitches):
                raise ValueError("Pitches must be integers")
            if not isinstance(n["artic"], list) or set(n["artic"]) - set(
                ["!", "-", ">", "^", "~", "*", "roll"]
            ):
                raise ValueError("Invalid articulation")
            result.append(Note(start, dur, pitches, set(n["artic"])))
        return result

    for voice in ("melody", "counter", "bass"):
        raw[voice] = notes(raw[voice])
    raw["title"] = identifier(raw["title"])
    if len(raw["intensity"]) != bars:
        raise ValueError("One intensity value is required per bar")
    for value in raw["intensity"]:
        number(value, 0, 1)
    for bar, beat, factor in raw.get("rubato", []):
        number(bar, 1, bars - 1)
        number(beat, 0, bpb - 1)
        number(factor, 0.75, 1.25)
    if len(raw.get("rubato", [])) > total:
        raise ValueError("Too many tempo changes")
    for key in raw.get("sections", {}):
        if (
            not isinstance(key, str)
            or not 1 <= len(key) <= 64
            or any(ord(c) < 32 for c in key)
        ):
            raise ValueError("Invalid section label")
    for spans in (raw["phrases"], list(raw.get("sections", {}).values())):
        if len(spans) > bars:
            raise ValueError("Too many sections")
        for a, b in spans:
            number(a, 1, bars)
            number(b, a, bars)
    from ..score.notation import chord_pcs

    if not raw["harmony"] or len(raw["harmony"]) > total * 4:
        raise ValueError("A bounded harmonic timeline is required")
    for start, duration, symbol in raw["harmony"]:
        number(start, 0, total)
        number(duration, 0.03125, total - start)
        if not isinstance(symbol, str) or len(symbol) > 32:
            raise ValueError("Invalid chord")
        chord_pcs(symbol)
    sig = raw.get("time_signature") or [bpb, 4]
    if len(sig) != 2 or sig[1] not in [4, 8] or sig[0] * 4 / sig[1] != bpb:
        raise ValueError("Time signature and beat grid disagree")
    counter_above = raw.get("counter_above", [])
    if counter_above is not True:
        if not isinstance(counter_above, list) or len(counter_above) > bars:
            raise ValueError("Invalid descant sections")
        for a, b in counter_above:
            number(a, 1, bars)
            number(b, a, bars)
    score = Score(**raw)
    if not 50 <= score.seconds <= 120:
        raise ValueError("Compose 50–120 seconds of music")
    parts = {}
    from ..score.perform import ROLES

    if set(data["parts"]) != set(MOODS):
        raise ValueError("All three moods are required")
    for mood in MOODS:
        entries = data["parts"][mood]
        if not 1 <= len(entries) <= 32:
            raise ValueError("Each mood needs 1–32 parts")
        seen = set()
        parts[mood] = []
        for p in entries:
            name, inst, role = (
                identifier(p["name"]),
                identifier(p["instrument"]),
                identifier(p["role"]),
            )
            if name in seen or inst not in instruments or role not in ROLES:
                raise ValueError(f"Unknown or duplicate part: {name}, {inst}, {role}")
            seen.add(name)
            pan = p.get("pan")
            if pan is not None:
                number(pan, -1, 1)
            parts[mood].append(Part(name, inst, notes(p["notes"]), role, pan))
    mix = data.get("mix", {})
    if not isinstance(mix, dict) or set(mix) - set(MOODS):
        raise ValueError("Invalid mix moods")
    for mood, gains in mix.items():
        allowed = {p.name for p in parts[mood]} | set(ROLES)
        if not isinstance(gains, dict) or set(gains) - allowed:
            raise ValueError("Unknown mix part")
        for value in gains.values():
            number(value, -18, 6)
    return SimpleNamespace(
        SCORE=score, arrange=lambda mood: parts[mood], MIX_ADJUST=mix
    )


def timeline_id(composition):
    score = composition.SCORE
    timing = {
        key: getattr(score, key)
        for key in (
            "bpm",
            "bars",
            "beats_per_bar",
            "time_signature",
            "rubato",
            "harmony",
            "phrases",
            "sections",
        )
    }
    return hashlib.sha256(
        json.dumps(timing, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
