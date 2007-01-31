/*
  Copyright (C) 2006 Bradley Arsenault

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

#ifndef AINicowar_h
#define AINicowar_h

#include <AIEcho.h>

///Nicowar is a new powerhouse AI for Globulation 2
class NewNicowar : public AIEcho::EchoAI
{
public:
	NewNicowar();
	bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
	void tick(AIEcho::Echo& echo);
	void handle_message(AIEcho::Echo& echo, const std::string& message);
private:
	int timer;
};


#endif
