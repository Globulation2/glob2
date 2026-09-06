// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

class YOGClientPlayerListListener
{
public:
	virtual ~YOGClientPlayerListListener() {}

	virtual void playerListUpdated() = 0;
};



