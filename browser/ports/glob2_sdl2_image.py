"""Pinned SDL_image port with PNG/JPEG/WebP for Emscripten 4.0.15."""

import glob
import os

VERSION = "2.8.12"
HASH = "c2574d83f8ee89b97cc992fe6a579c87a1b9a1aa7a8a3cfb6bf58f10d3880a157d70cd053767cc2ade1bd124e102356a50274d688e24b2e04a375588e54114ac"
deps = ["sdl2", "libpng", "libjpeg", "glob2_webp"]


def needed(settings):
    return True


def library_name(settings):
    return (
        "libglob2_sdl2_image"
        + ("-mt" if settings.PTHREADS else "")
        + ("-wasm-sjlj" if settings.SUPPORT_LONGJMP == "wasm" else "")
        + ".a"
    )


def process_dependencies(settings):
    settings.USE_SDL = 2
    settings.USE_LIBPNG = 1
    settings.USE_LIBJPEG = 1


def get(ports, settings, shared):
    ports.fetch_project(
        "glob2_sdl2_image",
        f"https://github.com/libsdl-org/SDL_image/releases/download/release-{VERSION}/SDL2_image-{VERSION}.tar.gz",
        sha512hash=HASH,
    )
    root = ports.get_dir("glob2_sdl2_image", "SDL2_image-" + VERSION)
    ports.install_headers(os.path.join(root, "include"), target="SDL2")

    def create(final):
        sources = [
            os.path.relpath(p, root)
            for p in sorted(glob.glob(os.path.join(root, "src", "*.c")))
        ]
        flags = [
            "-sUSE_SDL=2",
            "-sUSE_LIBPNG=1",
            "-sUSE_LIBJPEG=1",
            "-DLOAD_PNG",
            "-DLOAD_JPG",
            "-DLOAD_WEBP",
            "-I" + os.path.join(root, "include"),
        ]
        if settings.PTHREADS:
            flags.append("-pthread")
        if settings.SUPPORT_LONGJMP == "wasm":
            flags.append("-sSUPPORT_LONGJMP=wasm")
        ports.build_port(root, final, "glob2_sdl2_image", flags=flags, srcs=sources)

    return [shared.cache.get_lib(library_name(settings), create, what="port")]


def clear(ports, settings, shared):
    shared.cache.erase_lib(library_name(settings))


def show():
    return "glob2_sdl2_image (SDL_image 2.8.12 PNG/JPEG/WebP; zlib license)"
