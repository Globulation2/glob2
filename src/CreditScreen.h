// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include "Glob2Screen.h"

class CreditScreen : public Glob2Screen
{
public:
	CreditScreen();
	virtual ~CreditScreen() { }
	void onAction(Widget *source, Action action, int par1, int par2);
};

