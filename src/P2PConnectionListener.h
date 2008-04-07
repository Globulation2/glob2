/*
  Copyright (C) 2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef P2PConnectionListener_h
#define P2PConnectionListener_h

#include "boost/shared_ptr.hpp"

class P2PConnectionEvent;

///This class listens for incoming messages on a p2p connection
class P2PConnectionListener
{
public:
	///Accepts an incoming Net Message from a p2p connection
	virtual void recieveP2PEvent(boost::shared_ptr<P2PConnectionEvent> event) = 0;
};

#endif


