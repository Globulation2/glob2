"""Measure current checkout strategy labels with the game's 13px SDL font."""
import ctypes
import ctypes.util
import pathlib
import sys
root = pathlib.Path(sys.argv[1])
lib = ctypes.CDLL(ctypes.util.find_library('SDL2_ttf'))
assert lib.TTF_Init() == 0
lib.TTF_OpenFont.argtypes = [ctypes.c_char_p, ctypes.c_int]
lib.TTF_OpenFont.restype = ctypes.c_void_p
lib.TTF_SizeUTF8.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
font = lib.TTF_OpenFont(str(root/'data/fonts/sans.ttf').encode(), 13)
assert font
for p in sorted((root/'data').glob('texts.*.txt')):
    if p.name in ('texts.keys.txt', 'texts.list.txt', 'texts.incomplete.txt'):
        continue
    a = p.read_text().splitlines()
    label = dict(zip(a[::2], a[1::2]))['[AI strategy]']
    w, h = ctypes.c_int(), ctypes.c_int()
    assert lib.TTF_SizeUTF8(font, label.encode(), ctypes.byref(w), ctypes.byref(h)) == 0
    print(p.name, w.value, label)
    assert w.value <= 110
