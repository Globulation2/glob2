"""Pinned WebP decoder/demux port for Emscripten 4.0.15 (BSD licensed libwebp)."""

import glob
import os

VERSION = "1.6.0"
HASH = "5c159d9760efcb92749092536daada22c0a73c20926c76097a5f0448ddbf874cf761324ca97925ca5f578b30477564b2b072b47667e504673797128b31cafcbf"
deps = []


def needed(settings):
    return True


def library_name(settings):
    return (
        "libglob2_webp"
        + ("-mt" if settings.PTHREADS else "")
        + ("-wasm-sjlj" if settings.SUPPORT_LONGJMP == "wasm" else "")
        + ".a"
    )


def get(ports, settings, shared):
    ports.fetch_project(
        "glob2_webp",
        f"https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-{VERSION}.tar.gz",
        sha512hash=HASH,
    )
    root = ports.get_dir("glob2_webp", "libwebp-" + VERSION)
    ports.install_headers(os.path.join(root, "src/webp"), target="webp")

    def create(final):
        sources = []
        for folder in ("dec", "demux", "dsp", "utils"):
            sources += [
                os.path.relpath(p, root)
                for p in sorted(glob.glob(os.path.join(root, "src", folder, "*.c")))
            ]
        flags = [
            "-I" + root,
            "-I" + os.path.join(root, "src"),
            "-I" + os.path.join(root, "sharpyuv"),
        ]
        if settings.PTHREADS:
            flags.append("-pthread")
        if settings.SUPPORT_LONGJMP == "wasm":
            flags.append("-sSUPPORT_LONGJMP=wasm")
        ports.build_port(root, final, "glob2_webp", flags=flags, srcs=sources)

    return [shared.cache.get_lib(library_name(settings), create, what="port")]


def clear(ports, settings, shared):
    shared.cache.erase_lib(library_name(settings))


def show():
    return "glob2_webp (libwebp 1.6.0 decoder/demux; BSD license)"
