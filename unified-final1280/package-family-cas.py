"""Package frozen family comparisons, excluding binaries and compiler objects."""
from pathlib import Path
import gzip, hashlib, io, json, tarfile

ROOT = Path(__file__).resolve().parent
CAMPAIGN = ROOT.parent.parent
STUDIES = {
    "prototypes": CAMPAIGN / "final1280/family-prototypes",
    "additional-heldout": CAMPAIGN / "family-quality-heldout-panel",
}
ALLOWED = {".json", ".py", ".h", ".cpp", ".csv", ".log", ".txt", ".patch", ".jsonl", ".gz"}
objects = {}
entries = []
for label, base in STUDIES.items():
    for path in sorted(base.rglob("*")):
        if not path.is_file() or path.suffix not in ALLOWED:
            continue
        if any(part in {"__pycache__", "build", "profile"} for part in path.relative_to(base).parts):
            continue
        if path.suffix == ".gz" and not path.name.endswith((".game.gz", ".map.gz")):
            continue
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        objects.setdefault(digest, data)
        entries.append(dict(path=str(Path(label) / path.relative_to(base)), sha256=digest,
                            bytes=len(data), original_path=str(path)))
for plan_path in [STUDIES["prototypes"] / "cpu/PLAN.json", STUDIES["additional-heldout"] / "PLAN.json"]:
    plan = json.loads(plan_path.read_text())
    for item in plan["maps"]:
        for key, kind in [("map_path", "initial.map.gz"), ("warm_path", "warm.game.gz")]:
            if key not in item:
                continue
            path = Path(item[key])
            data = path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            assert digest == item[key.replace("_path", "_sha256")]
            objects.setdefault(digest, data)
            entries.append(dict(path=f"inputs/{item['group']}/{kind}", sha256=digest,
                                bytes=len(data), original_path=str(path)))
index = dict(schema=1, entries=entries, logical_files=len(entries), unique_objects=len(objects),
             scope="Frozen capped shared12/onehot17/experts prototypes: 24 OFF CPU, six original ON quality, two metadata-selected warmups and four additional ON continuations. No binaries/compiler objects/dependency trees.")
(ROOT / "FAMILY-CAS.json").write_text(json.dumps(index, indent=2) + "\n")
archive = ROOT / "family-cas.tar.gz"
with archive.open("wb") as raw:
    with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as tar:
            for digest, data in sorted(objects.items()):
                info = tarfile.TarInfo("objects/" + digest)
                info.size, info.mode, info.mtime = len(data), 0o644, 0
                tar.addfile(info, io.BytesIO(data))
with tarfile.open(archive) as tar:
    for digest, expected in objects.items():
        actual = tar.extractfile("objects/" + digest).read()
        assert actual == expected and hashlib.sha256(actual).hexdigest() == digest
receipt = dict(status="PASS", archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
               archive_bytes=archive.stat().st_size, index_sha256=hashlib.sha256((ROOT / "FAMILY-CAS.json").read_bytes()).hexdigest(),
               logical_files=len(entries), unique_objects=len(objects))
(ROOT / "FAMILY-CAS-VERIFIED.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt))
