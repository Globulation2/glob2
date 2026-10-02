"""Pinned SDL3_image source and release PNG/JPEG/WebP codec policy."""

import json
from pathlib import Path
from tool_archives import digest

SDL_IMAGE = json.loads(Path(__file__).with_name("sdl3-versions.json").read_text())["SDL_image"]
ARTIFACT = {key: SDL_IMAGE[key] for key in ("url", "sha256")}
CODECS = (
    "ANI",
    "AVIF",
    "BMP",
    "GIF",
    "JPG",
    "JXL",
    "LBM",
    "PCX",
    "PNG",
    "PNM",
    "QOI",
    "SVG",
    "TGA",
    "TIF",
    "WEBP",
    "XCF",
    "XPM",
    "XV",
)
DEPENDENCIES = ("sdl3", "libpng", "libjpeg", "libwebp", "libwebpdemux", "libwebpmux")


def lean_options():
    return [
        "-DSDLIMAGE_"
        + name
        + "="
        + ("ON" if name in ("PNG", "JPG", "WEBP") else "OFF")
        for name in CODECS
    ] + [
        "-DBUILD_SHARED_LIBS=ON",
        "-DSDLIMAGE_DEPS_SHARED=OFF",
        "-DSDLIMAGE_STRICT=ON",
        "-DSDLIMAGE_BACKEND_STB=OFF",
        "-DSDLIMAGE_BACKEND_IMAGEIO=OFF",
        "-DSDLIMAGE_SAMPLES=OFF",
        "-DSDLIMAGE_TESTS=OFF",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DSDLIMAGE_PNG_SAVE=ON",
        "-DSDLIMAGE_JPG_SAVE=ON",
        "-DSDLIMAGE_WEBP_SAVE=OFF",
    ]


def verified(prefix, identity):
    marker = prefix / "manifest.json"
    if not marker.is_file():
        return False
    try:
        record = json.loads(marker.read_text())
        return (
            record["identity"] == identity
            and bool(record["files"])
            and all(
                (prefix / name).is_file() and digest(prefix / name) == value
                for name, value in record["files"].items()
            )
        )
    except (ValueError, KeyError, OSError):
        return False
