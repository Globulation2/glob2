#!/usr/bin/env python3
"""Probe prospective locale glyphs through SDL3_ttf without registering locales.

This is a readiness diagnostic, not linguistic or shaping certification. Rendered
BMPs must be inspected for conjuncts, combining marks and bidirectional ordering.
Use --library and --sdl-library to select the exact built runtime being tested;
otherwise GLOB2_SDL3_PREFIX or system library discovery is used.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import re


class Color(ctypes.Structure):
    _fields_ = [(c, ctypes.c_uint8) for c in ('r', 'g', 'b', 'a')]


def library_path(name, explicit):
    if explicit:
        return str(Path(explicit).resolve())
    prefix = os.environ.get('GLOB2_SDL3_PREFIX')
    if prefix:
        # Match the library stem, not its prefix: SDL3_image/net/ttf are not
        # substitutes for SDL3. Include versioned Unix libraries without a
        # development symlink and Windows DLLs.
        pattern = re.compile(r'(?:lib)?' + re.escape(name)
                             + r'(?:\.\d+)*(?:\.dylib|\.so(?:\.\d+)*|\.dll)', re.I)
        paths = sorted(p for p in (Path(prefix) / 'lib').iterdir()
                       if pattern.fullmatch(p.name) and p.is_file())
        if paths:
            return str(paths[0].resolve())
        raise RuntimeError(f'{name} not found under GLOB2_SDL3_PREFIX')
    found = ctypes.util.find_library(name)
    if not found:
        raise RuntimeError(f'{name} not found; select an explicit library')
    return found


def probe(args):
    core_path = library_path('SDL3', args.sdl_library)
    ttf_path = library_path('SDL3_ttf', args.library)
    core = ctypes.CDLL(core_path, mode=ctypes.RTLD_GLOBAL)
    ttf = ctypes.CDLL(ttf_path)
    ttf.TTF_Init.restype = ctypes.c_bool
    ttf.TTF_OpenFont.argtypes = [ctypes.c_char_p, ctypes.c_float]
    ttf.TTF_OpenFont.restype = ctypes.c_void_p
    ttf.TTF_CloseFont.argtypes = [ctypes.c_void_p]
    ttf.TTF_AddFallbackFont.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    ttf.TTF_AddFallbackFont.restype = ctypes.c_bool
    ttf.TTF_FontHasGlyph.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    ttf.TTF_FontHasGlyph.restype = ctypes.c_bool
    ttf.TTF_RenderText_Blended.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                         ctypes.c_size_t, Color]
    ttf.TTF_RenderText_Blended.restype = ctypes.c_void_p
    core.SDL_SaveBMP.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    core.SDL_SaveBMP.restype = ctypes.c_bool
    core.SDL_DestroySurface.argtypes = [ctypes.c_void_p]
    core.SDL_GetError.restype = ctypes.c_char_p
    def error():
        return core.SDL_GetError().decode('utf-8', errors='replace')
    major, minor, patch = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
    ttf.TTF_GetHarfBuzzVersion.argtypes = [ctypes.POINTER(ctypes.c_int)] * 3
    ttf.TTF_GetHarfBuzzVersion(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
    samples = json.loads(args.probes.read_text(encoding='utf-8'))
    selected = args.locales or list(samples)
    unknown = set(selected) - samples.keys()
    if unknown:
        raise ValueError('Unknown probe locales: ' + ', '.join(sorted(unknown)))
    if args.render_directory:
        args.render_directory.mkdir(parents=True, exist_ok=True)
    report = {'font': str(args.font.resolve()),
              'font_sha256': hashlib.sha256(args.font.read_bytes()).hexdigest(),
              'fallback_fonts': [{'path': str(path.resolve()),
                                  'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                                 for path in args.fallback_font],
              'sdl_library': core_path, 'ttf_library': ttf_path,
              'harfbuzz_version': [major.value, minor.value, patch.value],
              'limits': 'Direct SDL_ttf probe; not the game bidi/wrapping path. '
                        'Glyph coverage and successful rasterization do not prove correct shaping.',
              'locales': {}}
    if not ttf.TTF_Init():
        raise RuntimeError(error())
    try:
        font = ttf.TTF_OpenFont(os.fsencode(args.font), args.size)
        if not font:
            raise RuntimeError(error())
        fallbacks = []
        try:
            for path in args.fallback_font:
                fallback = ttf.TTF_OpenFont(os.fsencode(path), args.size)
                if not fallback:
                    raise RuntimeError(error())
                fallbacks.append(fallback)
                if not ttf.TTF_AddFallbackFont(font, fallback):
                    raise RuntimeError(error())
            for locale in selected:
                text = samples[locale]
                missing = sorted({c for c in text if not c.isspace()
                                  and c not in '\u200c\u200d\u200e\u200f'
                                  and not any(ttf.TTF_FontHasGlyph(candidate, ord(c))
                                              for candidate in [font] + fallbacks)})
                row = {'text': text, 'missing': missing,
                       'missing_codepoints': [f'U+{ord(c):04X}' for c in missing]}
                if args.render_directory:
                    output = args.render_directory / f'{locale}.bmp'
                    surface = ttf.TTF_RenderText_Blended(font, text.encode('utf-8'),
                                                       0, Color(0, 0, 0, 255))
                    if not surface:
                        raise RuntimeError(f'{locale}: {error()}')
                    try:
                        if not core.SDL_SaveBMP(surface, os.fsencode(output)):
                            raise RuntimeError(error())
                    finally:
                        core.SDL_DestroySurface(surface)
                    row['render'] = str(output.resolve())
                report['locales'][locale] = row
        finally:
            ttf.TTF_CloseFont(font)
            for fallback in fallbacks:
                ttf.TTF_CloseFont(fallback)
    finally:
        ttf.TTF_Quit()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, default=Path(__file__).with_name('sans.ttf'))
    parser.add_argument('--fallback-font', type=Path, action='append', default=[])
    parser.add_argument('--probes', type=Path, default=Path(__file__).with_name('script-probes.json'))
    parser.add_argument('--library')
    parser.add_argument('--sdl-library')
    parser.add_argument('--locales', nargs='+')
    parser.add_argument('--size', type=float, default=20)
    parser.add_argument('--render-directory', type=Path)
    parser.add_argument('--json-output', type=Path)
    args = parser.parse_args()
    if args.size <= 0:
        parser.error('--size must be positive')
    try:
        report = probe(args)
    except (OSError, ValueError, RuntimeError, AttributeError) as exc:
        parser.exit(2, f'Script probe failed: {exc}\n')
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for locale, row in report['locales'].items():
        print(f"{locale}: {len(row['missing'])} missing glyphs")
    print('HarfBuzz:', '.'.join(map(str, report['harfbuzz_version'])))
    return int(any(row['missing'] for row in report['locales'].values()))


if __name__ == '__main__':
    raise SystemExit(main())
