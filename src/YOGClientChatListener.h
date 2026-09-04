// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGClientChatListener_h
#define __YOGClientChatListener_h

#include <memory>

class YOGMessage;

///This class is a mix-in class for objects that want to listen for recieved texts
class YOGClientChatListener
{
public:
	virtual ~YOGClientChatListener() {}

	///Recieves a text message
	virtual void recieveTextMessage(std::shared_ptr<YOGMessage> message)=0;
};

#endif
