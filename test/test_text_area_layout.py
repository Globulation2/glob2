#!/usr/bin/env python3
"""Run the production text-area layout with a deterministic headless font."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

# Only the widget/font dependencies are replaced. Compile the production
# layout method unchanged, and reject any partial UTF-8 passed to the font.
HEADER = r'''
#include <cassert>
#include <string>
#include <vector>
namespace GAGGUI {
class TextArea {
public:
    std::string text;
    unsigned cursorPosY = 0;
    int spriteWidth = 0;
    bool sprite = false;
    std::vector<unsigned> lines, frames, lines_frames;
    std::vector<bool> show_image;
    int available = 30;
    void getScreenPos(int* x, int* y, int* w, int* h) {
        *x = *y = 0; *w = available + 14; *h = 100;
    }
    int getStringWidth(const std::string& value) {
        int width = 0;
        for (size_t i = 0; i < value.size();) {
            unsigned char c = value[i];
            unsigned length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            assert(c < 0x80 || c >= 0xC2);
            assert(i + length <= value.size());
            for (unsigned j = 1; j < length; ++j)
                assert((static_cast<unsigned char>(value[i+j]) & 0xC0) == 0x80);
            width += (c == '\n' || c == '\r') ? 0 : c == ' ' ? 3 : 10;
            i += length;
        }
        return width;
    }
    void layout();
};
}
'''

MAIN = r'''
#include "GUITextArea.h"
#include <iostream>
using GAGGUI::TextArea;
void check(const std::string& text, const std::vector<unsigned>& expected) {
    TextArea area;
    area.text = text;
    area.layout();
    if (area.lines != expected) {
        std::cerr << "Unexpected line breaks for " << text << ":";
        for (auto start : area.lines) std::cerr << " " << start;
        std::cerr << "\n";
        assert(false);
    }
    assert(area.show_image.size() == area.lines.size());
    std::string joined;
    for (size_t i = 0; i < area.lines.size(); ++i) {
        size_t end = i+1 < area.lines.size() ? area.lines[i+1] : text.size();
        auto line = text.substr(area.lines[i], end-area.lines[i]);
        joined += line;
        auto visibleEnd = line.find_last_not_of(" \t\r\n");
        auto visible = visibleEnd == std::string::npos ? "" : line.substr(0, visibleEnd+1);
        assert(area.getStringWidth(visible) <= area.available);
    }
    assert(joined == text);
}
int main() {
    check("日本語設定画面", {0, 9, 18});
    check("日本語設定画面\n", {0, 9, 18, 22});
    check("한국어설정화면", {0, 9, 18});
    check("абвгдеж", {0, 6, 12});
    check("😀😁😂😃", {0, 12});
    check("abcdefg", {0, 3, 6});
    check("ab 日本語設定", {0, 3, 12});
    check("日本語 \n", {0, 11});
    check("abc def", {0, 4});
    check("日本語 設定\n", {0, 10, 17});
    check("ab cd", {0, 3});
    check("abc", {0});
    check("", {0});
    std::cout << "Text-area UTF-8 wrapping: 13 cases passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix='glob2-text-layout-') as directory:
    work = Path(directory)
    (work / 'GUITextArea.h').write_text(HEADER, encoding='utf-8')
    (work / 'main.cpp').write_text(MAIN, encoding='utf-8')
    binary = work / 'layout-test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++17', '-I' + str(work), str(work / 'main.cpp'),
        str(ROOT / 'libgag/src/GUITextAreaLayout.cpp'), '-o', str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)
