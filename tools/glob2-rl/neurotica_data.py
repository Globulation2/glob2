# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual teacher orders, with whole-game/seed validation splits.
NAC records are written before engine execution. A completed game sidecar is
mandatory; incomplete or failed games never silently become training examples.
"""

import json
import struct
import zlib
from pathlib import Path
import numpy as np
from neurotica_actions import parse_context, unpack_action, validate_action


class OrderCorpus:
    def __init__(self, root):
        self.records = []
        self.groups = {}
        self.files = []
        for path in sorted(Path(root).glob("*.p*.nac")):
            meta_path = Path(str(path).rsplit(".p", 1)[0] + ".json")
            if not meta_path.exists():
                raise ValueError(f"missing completion metadata: {meta_path}")
            meta = json.loads(meta_path.read_text())
            if meta.get("status") != "complete":
                continue
            group = json.dumps(
                [meta["generator"], meta["map_seed"]], separators=(",", ":")
            )
            self.files.append(str(path))
            previous = None
            indices = []
            with path.open("rb") as f:
                while True:
                    off = f.tell()
                    length = f.read(4)
                    if not length:
                        break
                    if len(length) != 4:
                        raise ValueError(f"truncated corpus {path}")
                    size = struct.unpack("<I", length)[0]
                    if size > 32 * 1024 * 1024:
                        raise ValueError("oversized record")
                    blob = f.read(size)
                    if len(blob) != size:
                        raise ValueError(f"truncated corpus {path}")
                    c, a = self.decode(blob)
                    validate_action(c, a)
                    if (
                        previous is not None
                        and c.tick <= self.records[previous]["tick"]
                    ):
                        raise ValueError(
                            "non-increasing teacher ticks (duplicate recorder/run?)"
                        )
                    index = len(self.records)
                    self.records.append(
                        dict(
                            path=str(path),
                            offset=off,
                            size=size,
                            tick=c.tick,
                            previous=previous,
                            delay=1,
                            op=a["op"],
                        )
                    )
                    if previous is not None:
                        self.records[previous]["delay"] = min(
                            25, c.tick - self.records[previous]["tick"]
                        )
                    previous = index
                    indices.append(index)
            if indices:
                last = self.records[indices[-1]]
                last["delay"] = max(1, min(25, meta["ticks"] - last["tick"]))
            self.groups.setdefault(group, []).extend(indices)
        if not self.records:
            raise ValueError(
                "no completed order corpus; legacy .aob/.atr files are not action labels"
            )

    @staticmethod
    def decode(blob):
        raw = zlib.decompress(blob)
        n = struct.unpack_from("<I", raw)[0]
        return parse_context(raw[4 : 4 + n]), unpack_action(raw[4 + n :])

    def read(self, i):
        r = self.records[i]
        with open(r["path"], "rb") as f:
            f.seek(r["offset"] + 4)
            c, a = self.decode(f.read(r["size"]))
        a["delay"] = r["delay"]
        return c, a

    def sample(self, i):
        c, a = self.read(i)
        p = self.records[i]["previous"]
        return c, a, (self.read(p)[0] if p is not None else None)

    def split(self, fraction=0.2, seed=0):
        keys = sorted(self.groups)
        if len(keys) < 2:
            raise ValueError(
                "at least two independent map seeds required for validation"
            )
        np.random.default_rng(seed).shuffle(keys)
        n = max(1, min(len(keys) - 1, round(len(keys) * fraction)))
        val_keys = set(keys[:n])
        train = []
        val = []
        for k, idx in self.groups.items():
            (val if k in val_keys else train).extend(idx)
        return (
            train,
            val,
            dict(train=sorted(set(keys) - val_keys), validation=sorted(val_keys)),
        )
