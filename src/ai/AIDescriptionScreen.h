// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include "Glob2Screen.h"

namespace GAGGUI
{
	class TextButton;
	class TextArea;
	class List;
	class Text;
};

///This screen shows descriptions for the various types of AI
class AIDescriptionScreen : public Glob2Screen
{
public:
	///This shows descriptions for the various types of AI
	AIDescriptionScreen();
	
	virtual void onAction(Widget *source, Action action, int par1, int par2);

	enum
	{
		OK,
	};	

private:
	TextButton* ok;
	TextArea *description;
	List *ailist;
	Text *title;
};

