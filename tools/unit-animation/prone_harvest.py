#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render/install the approved floating harvest without renumbering saved actions.

Run prone_harvest_author.py under Blender 3.6.23 first. This host tool needs
NumPy/Pillow and the pinned legacy Blender 2.34 with its private library path.
Every render is bound to its scene, renderer and output hashes before install.
"""
import argparse, concurrent.futures, hashlib, json, math, os, struct, subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from render import schema_and_scene
from turns import destination_metadata, validate_work

ROOT = Path(__file__).resolve().parents[2]
BASE = 2016
NAMES = ("worker-prone-harvest", "worker-prone-harvest-mid")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def checked_motion(work):
    receipt = json.loads((work / "author-receipt.json").read_text())
    for path, sha in receipt["sources"].items():
        if digest((ROOT / path).read_bytes()) != sha:
            raise ValueError("Stale motion source: " + path)
    if digest((work / "motion.npz").read_bytes()) != receipt["motion"]:
        raise ValueError("Stale motion data")
    return np.load(work / "motion.npz"), receipt


def render(work, blender, libraries, workers):
    motion, receipt = checked_motion(work)
    source = ROOT / "content-src/gfx/originals/units/glob-worker-swim.blend"
    template = source.read_bytes()
    schema, blocks, r = schema_and_scene(template)
    off = schema["Object"]["id"][0] + schema["ID"]["name"][0]
    objects = {
        template[p + off : p + off + 24].split(b"\0")[0].decode()[2:]: p
        for code, p, n in blocks
        if code == b"OB\0\0"
    }
    env = os.environ.copy()
    if libraries:
        env["LD_LIBRARY_PATH"] = libraries
    renderer = digest(blender.read_bytes())
    jobs = []
    for mode, size in [("native", 38), ("hd", 128)]:
        folder = work / mode
        folder.mkdir(exist_ok=True)
        for direction in range(8):
            a = -direction * math.pi / 4
            rot = np.array(
                [
                    [math.cos(a), -math.sin(a), 0],
                    [math.sin(a), math.cos(a), 0],
                    [0, 0, 1],
                ]
            )
            for phase in range(64):
                ident = f"unit{BASE+direction*64+phase}"
                data = bytearray(template)
                m = motion["components"][phase]
                for j in range(13):
                    p = objects["Mball" + (f".{j:03d}" if j else "")]
                    for field in ["parent", "ipo"]:
                        struct.pack_into("<I", data, p + schema["Object"][field][0], 0)
                    for field, v in [
                        ("loc", rot @ m[j, :3, 3]),
                        ("rot", [0, 0, 0]),
                        ("size", np.linalg.norm(m[j, :3, :3], axis=0)),
                        ("dloc", [0, 0, 0]),
                        ("drot", [0, 0, 0]),
                    ]:
                        struct.pack_into(
                            "<3f", data, p + schema["Object"][field][0], *v
                        )
                for field, v in [
                    ("xsch", size),
                    ("ysch", size),
                    ("sfra", 1),
                    ("efra", 1),
                ]:
                    struct.pack_into("<h", data, r + schema["RenderData"][field][0], v)
                po, n = schema["RenderData"]["pic"]
                path = str(folder / ident).encode() + b"\0"
                if len(path) > n:
                    raise ValueError("Render path too long")
                data[r + po : r + po + n] = path.ljust(n, b"\0")
                scene = folder / (ident + ".blend")
                scene.write_bytes(data)
                jobs.append((scene, folder / (ident + "0001.png"), size, digest(data)))

    def run(job):
        scene, output, size, scene_hash = job
        record = output.with_suffix(".receipt.json")
        expected = {
            "scene": scene_hash,
            "renderer": renderer,
            "motion": receipt["motion"],
        }
        if record.exists() and output.exists():
            old = json.loads(record.read_text())
            if all(old.get(k) == v for k, v in expected.items()) and old.get(
                "output"
            ) == digest(output.read_bytes()):
                return
        output.unlink(missing_ok=True)
        with scene.with_suffix(".log").open("w") as log:
            subprocess.run(
                [str(blender), "-b", str(scene), "-f", "1"],
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
            )
        with Image.open(output) as im:
            if im.size != (size, size) or im.mode != "RGBA":
                raise ValueError("Invalid rendered image")
            im.load()
        write_json(record, {**expected, "output": digest(output.read_bytes())})

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for i, _ in enumerate(pool.map(run, jobs), 1):
            if i % 128 == 0:
                print(f"Rendered {i}/{len(jobs)}", flush=True)


def export_meshes(work):
    import sys

    sys.path.insert(0, str(ROOT / "tools/skins"))
    from fit_unit_shapes import pca, quantize, encode
    from skin_assets import read_gsk

    d, receipt = checked_motion(work)
    uv, indices, _, size = read_gsk(
        ROOT / "content/data/skins/colony-v1/worker-swim.gsk"
    )
    if not np.array_equal(uv, d["uv"].astype("f4")) or not np.array_equal(
        indices, d["triangles"]
    ):
        raise ValueError("Paint topology changed")
    pos = d["vertices"][:64]
    norm = d["normals"][:64]
    mean, b, c = pca(pos, 32)
    nmean, nb, nc = pca(norm, 24)
    sc, di, rounded = quantize(b)
    nsc, ndi, nrounded = quantize(nb)
    rebuilt = mean + np.tensordot(c.astype("f4"), rounded, axes=1)
    error = np.linalg.norm(rebuilt - pos, axis=2)
    if error.max() > 0.01:
        raise ValueError("Unacceptable blend-shape fit")
    cam = d["camera"]
    transform = np.diag([2 / float(d["scale"]), 2 / float(d["scale"]), -0.01, 1]) @ cam
    for parity, name in enumerate(NAMES):
        frames = []
        for heading in range(8):
            a = -heading * math.pi / 4
            rot = np.array(
                [
                    [math.cos(a), -math.sin(a), 0],
                    [math.sin(a), math.cos(a), 0],
                    [0, 0, 1],
                ]
            )
            for phase in range(parity, 64, 2):
                points = pos[phase] @ rot.T
                projected = np.c_[points, np.ones(len(points))] @ transform.T
                n = norm[phase] @ rot.T @ cam[:3, :3].T
                frames.append(np.c_[projected[:, :3], n].astype("<f4"))
        baked = (
            struct.pack("<4sIIII", b"GSK1", len(uv), indices.size, 256, 38)
            + uv.astype("<f4").tobytes()
            + indices.astype("<u4").tobytes()
            + np.array(frames).tobytes()
        )
        (work / (name + ".gsk")).write_bytes(baked)
        clip = {
            "id": parity,
            "modelToClip": transform.ravel().tolist(),
            "normalToCamera": cam[:3, :3].ravel().tolist(),
            "headings": np.repeat(-np.arange(8) * np.pi / 4, 32).tolist(),
            "coefficients": np.tile(c[parity::2], (8, 1)),
            "normalCoefficients": np.tile(nc[parity::2], (8, 1)),
        }
        (work / (name + ".gsb")).write_bytes(
            encode(uv, indices.ravel(), mean, sc, di, nmean, nsc, ndi, [clip], 38)
        )
        pivot = (pos[parity].min(axis=0) + pos[parity].max(axis=0)) / 2
        write_json(
            work / (name + ".view.json"),
            dict(
                version=1,
                meshSha256=digest(baked),
                clipToModel=np.linalg.inv(transform).ravel().tolist(),
                modelToClip=transform.ravel().tolist(),
                normalToModel=cam[:3, :3].T.ravel().tolist(),
                pivot=pivot.tolist(),
                radius=float(np.linalg.norm(pos[parity] - pivot, axis=1).max()),
            ),
        )
        (work / (name + ".guv")).write_bytes(
            (ROOT / "content/data/skins/colony-v1/worker-swim.guv").read_bytes()
        )
    write_json(
        work / "export-receipt.json",
        dict(
            sources={
                **receipt["sources"],
                **{
                    p: digest((ROOT / p).read_bytes())
                    for p in (
                        "tools/unit-animation/prone_harvest.py",
                        "tools/skins/fit_unit_shapes.py",
                        "tools/skins/skin_assets.py",
                        "content/data/skins/colony-v1/worker-swim.gsk",
                        "content/data/skins/colony-v1/worker-swim.guv",
                    )
                },
            },
            files={
                name + ext: digest((work / (name + ext)).read_bytes())
                for name in NAMES
                for ext in (".gsk", ".gsb", ".guv", ".view.json")
            },
            maxFitError=float(error.max()),
        ),
    )


def install(work):
    _, author = checked_motion(work)
    exported = json.loads((work / "export-receipt.json").read_text())
    for path, sha in exported["sources"].items():
        if digest((ROOT / path).read_bytes()) != sha:
            raise ValueError("Stale export source: " + path)
    for file, sha in exported["files"].items():
        if digest((work / file).read_bytes()) != sha:
            raise ValueError("Stale export: " + file)
    hd = ROOT / "content/data/highres/v1"
    manifest, rows = destination_metadata(hd)
    records = []
    images = []
    renderer = None
    for direction in range(8):
        for phase in range(64):
            ident = f"unit{BASE+direction*64+phase}"
            for mode, size in [("native", 38), ("hd", 128)]:
                output = work / mode / (ident + "0001.png")
                scene = work / mode / (ident + ".blend")
                receipt = json.loads(output.with_suffix(".receipt.json").read_text())
                data = output.read_bytes()
                if renderer is None:
                    renderer = receipt["renderer"]
                if receipt != {
                    "scene": digest(scene.read_bytes()),
                    "renderer": renderer,
                    "motion": author["motion"],
                    "output": digest(data),
                }:
                    raise ValueError("Stale render " + ident)
                with Image.open(output) as im:
                    if im.size != (size, size) or im.mode != "RGBA":
                        raise ValueError("Invalid render")
                    im.load()
                images.append((mode, ident + "r.png", data))
                if mode == "hd":
                    records.append(
                        dict(
                            id=ident,
                            width=38,
                            height=38,
                            scale=0,
                            recipe="recovered original: Blender 2.34 authored prone harvest; 64 phases",
                            animation="worker-prone-harvest",
                            direction=direction,
                            phase=phase,
                            sources=[
                                dict(path=p, sha256=h)
                                for p, h in exported["sources"].items()
                            ],
                            layers=[
                                dict(
                                    file=ident + "r.png",
                                    role="team",
                                    sha256=digest(data),
                                    scene_sha256=receipt["scene"],
                                )
                            ],
                            render_settings=dict(
                                renderer_sha256=renderer, hd_pixel_size=128
                            ),
                        )
                    )
    # Complete validation barrier before modifying installed assets.
    for mode, name, data in images:
        (hd if mode == "hd" else ROOT / "content/data/gfx").joinpath(name).write_bytes(
            data
        )
    ids = {r["id"] for r in records}
    manifest["frames"] = [r for r in manifest["frames"] if r["id"] not in ids] + records
    write_json(hd / "manifest.json", manifest)
    (hd / "frames.txt").write_text(
        "\n".join(
            ["GLOB2_HIGHRES 1"]
            + [" ".join(row) for row in rows if row[0] not in ids]
            + [f"{r['id']} 38 38 0 - {r['id']}r.png" for r in records]
        )
        + "\n"
    )
    native = ROOT / "content/data/skins/colony-v1"
    web = ROOT / "services/online/apps/web/public/skins/models"
    m = json.loads((native / "manifest.json").read_text())
    for parity, name in enumerate(NAMES):
        _, vertices, indices, _, _ = struct.unpack_from(
            "<4sIIII", (work / (name + ".gsk")).read_bytes()
        )
        for ext in (".gsk", ".gsb", ".guv", ".view.json"):
            for destination in (native, web):
                (destination / (name + ext)).write_bytes(
                    (work / (name + ext)).read_bytes()
                )
        m["meshes"][name + ".gsk"] = dict(
            sha256=exported["files"][name + ".gsk"],
            source="tools/unit-animation/prone_harvest_motion.py",
            sourceSha256=exported["sources"][
                "tools/unit-animation/prone_harvest_motion.py"
            ],
            dependencies=exported["sources"],
            vertices=vertices,
            triangles=indices // 3,
            detailUV=dict(file=name + ".guv", sha256=exported["files"][name + ".guv"]),
        )
        m.setdefault("shapes", {})[name + ".gsb"] = dict(
            sha256=exported["files"][name + ".gsb"],
            sources=exported["sources"],
            format="GSB1",
            version=1,
            file=name + ".gsb",
            accepted=True,
            clips=[dict(id=parity, name=name, frames=256, shapes=32, normalShapes=24)],
        )
    write_json(native / "manifest.json", m)
    print("Installed 512 native/HD poses and two interleaved mesh clips")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("command", choices=["render", "export", "install"])
    p.add_argument("--work", type=Path, required=True)
    p.add_argument("--blender", type=Path)
    p.add_argument("--libraries")
    p.add_argument("--workers", type=int, default=4)
    a = p.parse_args()
    work = a.work.resolve()
    validate_work(work)
    if a.command == "render":
        if not a.blender or a.workers < 1:
            p.error("render needs --blender and positive --workers")
        render(work, a.blender.resolve(), a.libraries, a.workers)
    elif a.command == "export":
        export_meshes(work)
    else:
        install(work)
