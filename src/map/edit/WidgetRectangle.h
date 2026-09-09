// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault
// Copyright (C) 2022-2023 Nathan Mills

#pragma once

///A generic rectangle structure used for a variety of purposes, but mainly for the convience of the widget system
struct widgetRectangle
{
	widgetRectangle(int x, int y, int width, int height) : x(x), y(y), width(width), height(height) {}
	widgetRectangle() : x(0), y(0), width(0), height(0) {}
	//! Half-open on both axes: the top and left edges are inside, the bottom and
	//! right edges are not. This makes abutting rectangles tile without either
	//! overlapping or leaving a dead pixel line between them.
	bool is_in(int posx, int posy) { return posx>=x && posx<(x+width) && posy>=y && posy<(y+height); }

	int x;
	int y;
	int width;
	int height;
};


// Editor controls retain their distance from the right edge in logical pixels.
class RightAnchoredWidgetRectangle : public widgetRectangle
{
public:
	RightAnchoredWidgetRectangle(const widgetRectangle& rectangle, int windowWidth)
		: widgetRectangle(rectangle), windowWidth(windowWidth) {}

	void updateWindowWidth(int width)
	{
		x += width - windowWidth;
		windowWidth = width;
	}

private:
	int windowWidth;
};
