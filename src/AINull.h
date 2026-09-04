// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __AI_NULL_H
#define __AI_NULL_H

#include "AIImplementation.h"

class AINull : public AIImplementation
{
public:
	AINull() { }
	~AINull() { }
	
	void init(Player *player) { }

	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor) { return true; }
	void save(GAGCore::OutputStream *stream) { }
	
	std::shared_ptr<Order> getOrder(void);
	
private:
};

#endif

 

