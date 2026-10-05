# SPDX-License-Identifier: GPL-3.0-or-later
"""Untrusted community uploads: disk-backed decoding and portable Opus releases.

Invoked by the music worker, not the HTTP process. Inputs are local staged files.
The caller owns the job directory and deletes it (including sources) on completion.
"""

import argparse
import array
import base64
import hashlib
import json
import math
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave
import zipfile

RATE = 48000
MOODS = ("calm", "building", "combat")
MIN_FRAMES, MAX_FRAMES = 10 * RATE, 900 * RATE
MAX_SOURCE = 512 * 1024 * 1024
MAX_TAGS = 1024 * 1024
# Direct media containers only: playlists/concat inputs could read other local
# files even with network protocols disabled. All common source formats remain.
AUDIO_FORMATS = (
    "wav,flac,mp3,aac,mov,ogg,matroska,webm,aiff,ape,wv,asf,amr,au,ac3,eac3,dts,tta,tak"
)
IMAGE_FORMATS = "jpeg_pipe,png_pipe,webp_pipe,bmp_pipe,tiff_pipe,gif,mov"

# Import the existing encoder rather than duplicating its circular history logic.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from encode_music import encode, _page_crc  # noqa: E402


def run(args):
    result = subprocess.run(
        args, stdin=subprocess.DEVNULL, capture_output=True, timeout=1200
    )
    if result.returncode:
        raise ValueError(
            "Audio could not be decoded or converted: "
            + result.stderr.decode("utf-8", "replace")[-1500:]
        )
    return result.stdout


def decode(source, target):
    if not source.is_file() or not 0 < source.stat().st_size <= MAX_SOURCE:
        raise ValueError("Each source must be between 1 byte and 512 MiB.")
    probe = json.loads(
        run(
            [
                "ffprobe",
                "-v",
                "error",
                "-protocol_whitelist",
                "file,pipe",
                "-format_whitelist",
                AUDIO_FORMATS,
                "-show_streams",
                "-of",
                "json",
                str(source),
            ]
        )
    )
    streams = probe.get("streams", [])
    audio = [s for s in streams if s.get("codec_type") == "audio"]
    if len(audio) != 1:
        raise ValueError("Choose a file containing exactly one audio track.")
    # Read one frame beyond the limit: do not silently truncate a long source.
    run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-xerror",
            "-nostdin",
            "-y",
            "-protocol_whitelist",
            "file,pipe",
            "-format_whitelist",
            AUDIO_FORMATS,
            "-i",
            str(source),
            "-map",
            "0:a:0",
            "-vn",
            "-sn",
            "-dn",
            "-threads",
            "1",
            "-ar",
            str(RATE),
            "-ac",
            "2",
            "-t",
            str((MAX_FRAMES + 1) / RATE),
            "-f",
            "s16le",
            str(target),
        ]
    )
    frames = target.stat().st_size // 4
    if not MIN_FRAMES <= frames <= MAX_FRAMES:
        raise ValueError("Each track must be between 10 seconds and 15 minutes.")
    return frames


def inspect(sources, directory):
    directory.mkdir(parents=True, exist_ok=True)
    lengths = [decode(Path(sources[mood]), directory / f"{mood}.pcm") for mood in MOODS]
    return {
        "frames": lengths,
        "seconds": [n / RATE for n in lengths],
        "equal": len(set(lengths)) == 1,
    }


def comments(metadata, mood, frames, cover):
    fields = {
        "TITLE": metadata["title"] + " — " + mood.capitalize(),
        "ALBUM": metadata["title"],
        "ARTIST": metadata["artist"],
        "DESCRIPTION": metadata["description"],
        "LICENSE": metadata["license"],
        "COPYRIGHT": metadata["credits"],
        "SOURCE": "\n".join(metadata["sources"]),
        "GENRE": "; ".join(metadata["tags"]),
        "TRACKNUMBER": str(MOODS.index(mood) + 1),
        "GLOB2_SCHEMA": "1",
        "GLOB2_RELEASE": metadata["id"],
        "GLOB2_ORIGIN": metadata["origin"],
        "GLOB2_MOOD": mood,
        "GLOB2_FRAMES": str(frames),
        "GLOB2_AI_GENERATED": "1" if metadata["aiGenerated"] else "0",
    }
    if cover:
        mime = b"image/jpeg"
        picture = (
            struct.pack(">II", 3, len(mime))
            + mime
            + struct.pack(">I", 0)
            + struct.pack(">IIIII", 512, 512, 24, 0, len(cover))
            + cover
        )
        fields["METADATA_BLOCK_PICTURE"] = base64.b64encode(picture).decode("ascii")
    vendor = b"Globulation 2 community music"
    packet = (
        b"OpusTags"
        + struct.pack("<I", len(vendor))
        + vendor
        + struct.pack("<I", len(fields))
    )
    for key, value in fields.items():
        if "\0" in value:
            raise ValueError("Metadata cannot contain NUL characters.")
        item = (key + "=" + value).encode("utf-8")
        packet += struct.pack("<I", len(item)) + item
    if len(packet) > MAX_TAGS:
        raise ValueError("Embedded metadata is too large.")
    return packet


def tag_opus(path, packet):
    """Replace encoder comments without changing audio packets or granule positions.

    The comment packet may span pages. Renumber audio pages and recompute CRCs;
    never remux through FFmpeg, which could lose circular pre-skip/end trimming.
    """
    data = path.read_bytes()
    pages, offset = [], 0
    while offset < len(data):
        if data[offset : offset + 4] != b"OggS" or offset + 27 > len(data):
            raise ValueError("Invalid encoder page")
        segments = data[offset + 26]
        size = 27 + segments + sum(data[offset + 27 : offset + 27 + segments])
        page = bytearray(data[offset : offset + size])
        if len(page) != size:
            raise ValueError("Truncated encoder page")
        pages.append(page)
        offset += size
    if len(pages) < 3 or not pages[1][27 + pages[1][26] :].startswith(b"OpusTags"):
        raise ValueError("Missing encoder comments")
    end = 1
    while (
        pages[end][26] == 0 or pages[end][26] and pages[end][26 + pages[end][26]] == 255
    ):
        end += 1
    serial = struct.unpack_from("<I", pages[0], 14)[0]
    lacing = [255] * (len(packet) // 255) + [len(packet) % 255]
    output, consumed, sequence = [pages[0]], 0, 1
    for start in range(0, len(lacing), 255):
        segments = lacing[start : start + 255]
        final = start + len(segments) == len(lacing)
        size = sum(segments)
        page = bytearray(
            b"OggS"
            + bytes([0, 0 if start == 0 else 1])
            + struct.pack(
                "<QIII", 0 if final else 0xFFFFFFFFFFFFFFFF, serial, sequence, 0
            )
            + bytes([len(segments)])
            + bytes(segments)
            + packet[consumed : consumed + size]
        )
        _page_crc(page)
        output.append(page)
        consumed += size
        sequence += 1
    for page in pages[end + 1 :]:
        struct.pack_into("<I", page, 18, sequence)
        _page_crc(page)
        output.append(page)
        sequence += 1
    path.write_bytes(b"".join(output))


def waveform(path, frames):
    peaks = []
    block = max(1, math.ceil(frames / 512))
    with path.open("rb") as source:
        while data := source.read(block * 4):
            samples = array.array("h", data)
            if sys.byteorder != "little":
                samples.byteswap()
            peaks.append(round(max(abs(x) for x in samples) / 32768, 4))
    return peaks


def loudness_advice(path, mood, target):
    # FFmpeg's streaming meter avoids allocating another whole-song array.
    meter = subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-nostats",
            "-nostdin",
            "-v",
            "info",
            "-protocol_whitelist",
            "file,pipe",
            "-i",
            str(path),
            "-vn",
            "-af",
            "ebur128=peak=true:framelog=verbose",
            "-f",
            "null",
            "-",
        ],
        stdin=subprocess.DEVNULL,
        capture_output=True,
        timeout=1200,
    )
    if meter.returncode:
        return [
            f"{mood.capitalize()}: loudness analysis unavailable; check level by ear."
        ]
    summary = meter.stderr.decode("utf-8", "replace").split("Summary:")[-1]
    match = re.search(r"I:\s+([-\d.]+) LUFS", summary)
    warnings = []
    if match and abs(float(match[1]) - target) > 3:
        warnings.append(
            f"{mood.capitalize()}: measured {match[1]} LUFS; soundtrack guidance is {target} LUFS."
        )
    peak = re.search(r"Peak:\s+([-\d.]+) dBFS", summary)
    if peak and float(peak[1]) > -1:
        warnings.append(
            f"{mood.capitalize()}: true peak is {peak[1]} dBTP; check for clipping on transitions."
        )
    return warnings


def convert(sources, metadata, options, output, cover_path=None):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="music-") as temporary:
        work = Path(temporary)
        inspection = inspect(sources, work)
        lengths = inspection["frames"]
        repair = options.get("repair", "none")
        if repair not in ("none", "trim", "pad"):
            raise ValueError("Unknown length repair.")
        if not inspection["equal"] and repair == "none":
            raise ValueError("Choose trim or pad explicitly for unequal track lengths.")
        frames = max(lengths) if repair == "pad" else min(lengths)
        cover = None
        if cover_path:
            probe = json.loads(
                run(
                    [
                        "ffprobe",
                        "-v",
                        "error",
                        "-protocol_whitelist",
                        "file,pipe",
                        "-format_whitelist",
                        IMAGE_FORMATS,
                        "-show_streams",
                        "-of",
                        "json",
                        str(cover_path),
                    ]
                )
            )
            images = [
                s for s in probe.get("streams", []) if s.get("codec_type") == "video"
            ]
            if (
                len(images) != 1
                or not 0 < images[0].get("width", 0) <= 4096
                or not 0 < images[0].get("height", 0) <= 4096
            ):
                raise ValueError("Choose one cover image up to 4096 × 4096 pixels.")
            # Decode one image, fit/crop to a square, discard source metadata.
            run(
                [
                    "ffmpeg",
                    "-v",
                    "error",
                    "-xerror",
                    "-nostdin",
                    "-y",
                    "-protocol_whitelist",
                    "file,pipe",
                    "-format_whitelist",
                    IMAGE_FORMATS,
                    "-i",
                    str(cover_path),
                    "-frames:v",
                    "1",
                    "-vf",
                    "scale=512:512:force_original_aspect_ratio=increase,crop=512:512",
                    "-map_metadata",
                    "-1",
                    "-q:v",
                    "5",
                    str(work / "cover.jpg"),
                ]
            )
            cover = (work / "cover.jpg").read_bytes()
            if len(cover) > 256 * 1024:
                raise ValueError("Cover image is too complex; choose a simpler image.")
            (output / "cover.jpg").write_bytes(cover)
        result = {
            "frames": frames,
            "seconds": frames / RATE,
            "tracks": [],
            "warnings": [],
        }
        if not 50 <= frames / RATE <= 120:
            result["warnings"].append(
                "The shipped soundtrack uses 50–120 second loops; longer or shorter sets are allowed."
            )
        if not inspection["equal"]:
            result["warnings"].append(
                f"Lengths were repaired using {repair}; check beats and the loop seam by ear."
            )
        envelopes = []
        for i, mood in enumerate(MOODS):
            pcm = work / f"{mood}.pcm"
            with pcm.open("r+b") as file:
                file.truncate(
                    frames * 4
                )  # extension is zero-filled, never repeated audio
            wav = work / f"{mood}.wav"
            with wave.open(str(wav), "wb") as target, pcm.open("rb") as source:
                target.setparams((2, 2, RATE, frames, "NONE", "not compressed"))
                while chunk := source.read(256 * 1024):
                    target.writeframesraw(chunk)
            if options.get("master", False):
                import soundfile as sf
                from .master import master_loop

                audio, rate = sf.read(wav)
                mastered, report = master_loop(
                    audio, (-18, -17, -16)[i], highpass_hz=20
                )
                sf.write(wav, mastered, rate, subtype="PCM_24")
                del audio, mastered
                result.setdefault("mastering", []).append(report)
            target = output / f"a{i + 1}.opus"
            encode(wav, target, expected_frames=frames, loop=True)
            tag_opus(target, comments(metadata, mood, frames, cover))
            if target.stat().st_size > 16 * 1024 * 1024:
                raise ValueError(
                    "Encoded track exceeds the 16 MiB portable-file limit."
                )
            verified = work / "verified.pcm"
            actual = decode(target, verified)
            if actual != frames:
                raise ValueError("Encoded length changed after tagging.")
            result["warnings"].extend(loudness_advice(target, mood, (-18, -17, -16)[i]))
            peaks = waveform(verified, frames)
            envelopes.append(peaks)
            with verified.open("rb") as file:
                first = struct.unpack("<hh", file.read(4))
                file.seek(-4, 2)
                last = struct.unpack("<hh", file.read(4))
            if max(abs(a - b) for a, b in zip(first, last)) > 5000:
                result["warnings"].append(
                    f"{mood.capitalize()}: possible click at the loop seam."
                )
            if max(peaks) > 0.8913:
                result["warnings"].append(
                    f"{mood.capitalize()}: decoded peak exceeds −1 dB; consider loudness correction."
                )
            result["tracks"].append(
                {
                    "mood": mood,
                    "file": target.name,
                    "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                    "bytes": target.stat().st_size,
                    "waveform": peaks,
                }
            )
        # Advisory envelope comparison, never a claim of harmonic compatibility.
        for i in range(2):
            if envelopes[i] == envelopes[i + 1]:
                result["warnings"].append(
                    "Two moods have identical envelopes; check that the arrangements differ."
                )
        result["warnings"].append(
            "Equal length does not guarantee matching beats or harmony. Audition transitions throughout the set."
        )
        with zipfile.ZipFile(
            output / "set.zip", "w", compression=zipfile.ZIP_STORED
        ) as archive:
            for i in range(3):
                name = f"a{i + 1}.opus"
                archive.write(output / name, f"{metadata['id']}/{name}")
        (output / "result.json").write_text(json.dumps(result), encoding="utf-8")
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--inspect", action="store_true")
    args = parser.parse_args()
    request = json.loads(args.request.read_text())
    if args.inspect:
        with tempfile.TemporaryDirectory(prefix="music-inspect-") as tmp:
            result = inspect(request["sources"], Path(tmp))
        args.output.write_text(json.dumps(result))
    else:
        convert(
            request["sources"],
            request["metadata"],
            request["options"],
            args.output,
            request.get("cover"),
        )


if __name__ == "__main__":
    main()
