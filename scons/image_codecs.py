"""Pinned SDL_image source and the release PNG/JPEG/WebP codec policy."""

import json

from tool_archives import digest

ARTIFACT = {
    "url": "https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/SDL2_image-2.8.12.tar.gz",
    "sha256": "393f5efb50536ec13ca4f4affb69cc9966d3c3f969e6c5e701faddf9f9785381",
}
CODECS = (
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
DEPENDENCIES = ("SDL2", "libpng", "libjpeg", "libwebp", "libwebpdemux")


def lean_options():
    return [
        "-DSDL2IMAGE_"
        + name
        + "="
        + ("ON" if name in ("PNG", "JPG", "WEBP") else "OFF")
        for name in CODECS
    ] + [
        "-DBUILD_SHARED_LIBS=ON",
        "-DSDL2IMAGE_DEPS_SHARED=OFF",
        "-DSDL2IMAGE_STRICT=ON",
        "-DSDL2IMAGE_BACKEND_STB=OFF",
        "-DSDL2IMAGE_BACKEND_IMAGEIO=OFF",
        "-DSDL2IMAGE_SAMPLES=OFF",
        "-DSDL2IMAGE_TESTS=OFF",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DSDL2IMAGE_PNG_SAVE=ON",
        "-DSDL2IMAGE_JPG_SAVE=ON",
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
