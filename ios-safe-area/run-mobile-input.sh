#!/bin/sh
set -eu
xcrun clang++ -std=gnu++20 -O0 -I. -Ilibgag/include -Itest/support -I/Users/bradley/.codex/glob2-native-sdl/include artifacts/ios-safe-area/mobile-input-main.cpp -L/Users/bradley/.codex/glob2-native-sdl/lib -Wl,-rpath,/Users/bradley/.codex/glob2-native-sdl/lib -lSDL3 -o artifacts/ios-safe-area/mobile-input-tests
artifacts/ios-safe-area/mobile-input-tests
