/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __MultiplayerGameEventListener_h
#define __MultiplayerGameEventListener_h

#include "MultiplayerGameEvent.h"
#include "boost/shared_ptr.hpp"

/// This is a mix-in class. Classes that want to respond to 
/// MultiplayerGameEvents should derive from this class
class MultiplayerGameEventListener
{
public:
	virtual ~MultiplayerGameEventListener() {}

	///This responds to a Multiplayer Game event
	virtual void handleMultiplayerGameEvent(boost::shared_ptr<MultiplayerGameEvent> event) = 0;
};


#endif
