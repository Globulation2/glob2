// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __UNITSSKINS_H
#define __UNITSSKINS_H

#include <map>
#include <string>

namespace GAGCore
{
	class TextInputStream;
}
using namespace GAGCore;
namespace GAGGUI
{
	class MultiTextButton;
}
using namespace GAGGUI;
class UnitSkin;

class UnitsSkins
{
public:
	UnitsSkins();
	virtual ~UnitsSkins();
	
	UnitSkin *getSkin(const std::string &name);
	void buildSkinsList(MultiTextButton *target) const;
	
protected:
	std::map<std::string, UnitSkin *> unitsSkins;
};

#endif
