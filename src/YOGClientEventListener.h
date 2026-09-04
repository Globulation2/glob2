// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGClientEventListener_h
#define __YOGClientEventListener_h

#include <memory>

class YOGClientEvent;

/// This is a mix-in class. Classes that want to respond to YOG
/// events derive from this class
class YOGClientEventListener
{
public:
	virtual ~YOGClientEventListener() {}

	///This responds to a YOG event
	virtual void handleYOGClientEvent(std::shared_ptr<YOGClientEvent> event) = 0;
};


#endif
