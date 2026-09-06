// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <memory>

class YOGMessage;

///This class is a mix-in class for objects that want to listen for received texts
class YOGClientChatListener
{
public:
	virtual ~YOGClientChatListener() {}

	///Receives a text message
	virtual void receiveTextMessage(std::shared_ptr<YOGMessage> message)=0;
};

