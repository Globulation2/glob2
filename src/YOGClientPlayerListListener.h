// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGClientPlayerListListener_h
#define YOGClientPlayerListListener_h

class YOGClientPlayerListListener
{
public:
	virtual ~YOGClientPlayerListListener() {}

	virtual void playerListUpdated() = 0;
};



#endif
