// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGClientDownloadableMapListener_h
#define YOGClientDownloadableMapListener_h

class YOGClientDownloadableMapListener
{
public:
	virtual ~YOGClientDownloadableMapListener() {}

	virtual void mapListUpdated() = 0;
	virtual void mapThumbnailsUpdated() = 0;
};



#endif
