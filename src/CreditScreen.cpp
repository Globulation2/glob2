/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "CreditScreen.h"
#include "GlobalContainer.h"
#include <iostream>

CreditScreen::CreditScreen()
{
	addWidget(new TextButton(20, 20, 70,  25, ALIGN_RIGHT, ALIGN_BOTTOM, "", -1, -1, "standard", Toolkit::getStringTable()->getString("[quit]"), 0, 27));

	int yPos = 10;
	char temp[256];
	FILE *fp = Toolkit::getFileManager()->openFP("AUTHORS", "rb");
	while (fgets(temp, sizeof(temp), fp))
	{
		std::string s(temp);

		if (s.size() && s[0]!='\n')
		{
			std::string::size_type f = s.find('<');
			std::string::size_type l = s.rfind('>');
			if ((f != std::string::npos) && (l != std::string::npos))
			{
				s.erase(f, l-f+1);
			}
			addWidget(new Text(0, yPos, ALIGN_FILL, ALIGN_TOP, "little", s.c_str()));
			yPos += 14;
		}
		else
		{
			yPos += 6;
		}
	}
	fclose(fp);
}

void CreditScreen::onAction(Widget *source, Action action, int par1, int par2)
{
	if ((action==BUTTON_RELEASED) || (action==BUTTON_SHORTCUT))
		endExecute(par1);
}
