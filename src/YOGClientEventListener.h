/*
  Copyright (C) 2007 Bradley Arsenault

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

#ifndef __YOGClientEventListener_h
#define __YOGClientEventListener_h

#include "boost/shared_ptr.hpp"

class YOGClientEvent;

/// This is a mix-in class. Classes that want to respond to YOG
/// events derive from this class
class YOGClientEventListener
{
public:
	virtual ~YOGClientEventListener() {}

	///This responds to a YOG event
	virtual void handleYOGClientEvent(boost::shared_ptr<YOGClientEvent> event) = 0;
};


#endif
