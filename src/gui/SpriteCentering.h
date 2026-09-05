// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Centering a sprite inside a fixed-size box is a recurring right-panel idiom:
// the icon is smaller than its cell, so it gets an (dx, dy) nudge to sit in the
// middle. Several draw paths open-coded it as two near-identical lines, which is
// exactly how one of them ended up centering the Y axis against the sprite's
// *width* instead of its height. Funnelling every call through one helper keeps
// the two axes bound to getW/getH in a single place so they can't be transposed.

#pragma once

#include <GraphicContext.h> // GAGCore::Sprite

// Pixel offset from a box's top-left corner to where a sprite must be drawn to
// appear centered within it.
struct SpriteCenterOffset
{
	int dx;
	int dy;
};

// Pure integer core: offset to center a spriteW x spriteH sprite inside a
// boxW x boxH box. Uses an arithmetic right shift (floor toward -infinity) to
// reproduce the historical hand-written `(box - sprite) >> 1` exactly, including
// the off-by-one behaviour when the sprite is larger than the box.
constexpr SpriteCenterOffset centerInBox(int boxW, int boxH, int spriteW, int spriteH)
{
	return SpriteCenterOffset{ (boxW - spriteW) >> 1, (boxH - spriteH) >> 1 };
}

// Sprite-bound convenience: reads the width and height of image `imgid` off
// `sprite`. This is the only place the two axes are wired to getW/getH, so no
// call site can accidentally swap them.
inline SpriteCenterOffset centerSprite(int boxW, int boxH, GAGCore::Sprite* sprite, int imgid)
{
	return centerInBox(boxW, boxH, sprite->getW(imgid), sprite->getH(imgid));
}
