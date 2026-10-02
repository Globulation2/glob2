#!/usr/bin/env python3
"""Compare mainstream archive settings using identical staged bytes.

Portable Windows ZIP remains available. Linux always retains gzip and offers
xz only for a meaningful measured download saving; compression changes never
change the runtime payload. Temporary alternatives stay outside release output.
"""

import argparse
import gzip
import json
import lzma
import os
import shutil
import tarfile
import tempfile
import zipfile
from pathlib import Path

MIB = 1024 * 1024


def zip_archive(stage, output):
    stage, output = Path(stage), Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(path for path in stage.rglob("*") if path.is_file())
    if any(path.is_symlink() for path in stage.rglob("*")):
        raise ValueError("Windows ZIP stage cannot contain symlinks")
    with tempfile.TemporaryDirectory(
        prefix="glob2-zip-", dir=output.parent
    ) as temporary:
        alternatives = {}
        for level in (6, 9):
            path = Path(temporary) / f"level{level}.zip"
            with zipfile.ZipFile(
                path, "w", zipfile.ZIP_DEFLATED, compresslevel=level
            ) as archive:
                for source in files:
                    archive.write(source, source.relative_to(stage.parent))
            alternatives[level] = path
        # Prefer the cheaper setting when stronger compression saves no bytes.
        level = min(
            alternatives, key=lambda item: (alternatives[item].stat().st_size, item)
        )
        sizes = {str(item): path.stat().st_size for item, path in alternatives.items()}
        alternatives[level].replace(output)
    return dict(format="zip", selected_level=level, bytes_by_level=sizes)


def choose_xz(gzip_bytes, xz6_bytes, xz9_bytes):
    level = 9 if xz6_bytes - xz9_bytes >= MIB else 6
    size = xz9_bytes if level == 9 else xz6_bytes
    saved = gzip_bytes - size
    return (
        level if saved >= MIB or (gzip_bytes and saved / gzip_bytes >= 0.05) else None
    )


def linux_archives(stage, gzip_output, epoch=0):
    stage, gzip_output = Path(stage), Path(gzip_output)
    if not gzip_output.name.endswith(".tar.gz"):
        raise ValueError("Linux output must end with .tar.gz")
    gzip_output.parent.mkdir(parents=True, exist_ok=True)
    xz_output = gzip_output.with_name(gzip_output.name[:-3] + ".xz")
    # A re-run that no longer qualifies must not leave an old optional artifact.
    with tempfile.TemporaryDirectory(
        prefix="glob2-tar-", dir=gzip_output.parent
    ) as temporary:
        task = Path(temporary)
        raw = task / "payload.tar"

        def normalize(info):
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mtime = epoch
            return info

        with tarfile.open(raw, "w", format=tarfile.PAX_FORMAT) as archive:
            for path in sorted(stage.iterdir()):
                archive.add(path, arcname=path.name, filter=normalize)
        alternatives = {}
        # gzip metadata is fixed so results measure compression rather than time.
        compressed_gzip = task / "payload.tar.gz"
        with raw.open("rb") as source, compressed_gzip.open("wb") as packed:
            with gzip.GzipFile(
                filename="", fileobj=packed, mode="wb", compresslevel=9, mtime=epoch
            ) as destination:
                shutil.copyfileobj(source, destination)
        for level in (6, 9):
            path = task / f"level{level}.xz"
            with (
                raw.open("rb") as source,
                lzma.open(path, "wb", preset=level) as destination,
            ):
                shutil.copyfileobj(source, destination)
            alternatives[level] = path
        sizes = {
            str(level): path.stat().st_size for level, path in alternatives.items()
        }
        gzip_bytes = compressed_gzip.stat().st_size
        chosen = choose_xz(gzip_bytes, sizes["6"], sizes["9"])
        compressed_gzip.replace(gzip_output)
        outputs = [str(gzip_output)]
        if chosen:
            xz_output = gzip_output.with_name(gzip_output.name[:-3] + ".xz")
            alternatives[chosen].replace(xz_output)
            outputs.append(str(xz_output))
        else:
            xz_output.unlink(missing_ok=True)
    return dict(
        format="tar",
        gzip_bytes=gzip_bytes,
        xz_bytes_by_level=sizes,
        selected_xz_level=chosen,
        outputs=outputs,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("format", choices=("zip", "linux"))
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument(
        "--epoch", type=int, default=int(os.environ.get("SOURCE_DATE_EPOCH", 0))
    )
    args = parser.parse_args()
    result = (
        zip_archive(args.stage, args.output)
        if args.format == "zip"
        else linux_archives(args.stage, args.output, args.epoch)
    )
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
