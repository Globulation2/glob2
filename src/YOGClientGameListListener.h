// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGClientGameListListener_h
#define YOGClientGameListListener_h

///This class represents a listener for game list changes
class YOGClientGameListListener
{
public:
	virtual ~YOGClientGameListListener() {}

	///This is called when the game list is updated
	virtual void gameListUpdated() = 0;
};

#endif

