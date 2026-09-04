// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __GUIMESSAGEBOX_H
#define __GUIMESSAGEBOX_H

#include "GUIBase.h"

namespace GAGGUI
{
	//! This is the type of the message box (the number of buttons)
	enum MessageBoxType
	{
		//! One button (like OK)
		MB_ONEBUTTON,
		//! Two buttons (like Ok, Cancel)
		MB_TWOBUTTONS,
		//! three buttons, (like Yes, No, Cancel)
		MB_THREEBUTTONS
	};
	
	//! The display a modal message box, with a title and some buttons
	//! \retval the nummer of the clicked button, -1 on unexpected early-out (CTRL-C, ...)
	int MessageBox(GAGCore::GraphicContext *parentCtx, const std::string font, MessageBoxType type, std::string title, std::string caption1, std::string caption2 = "", std::string caption3 = "");
}

#endif
