// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

///This class allows a player to connect to a YOG router and send and receive
///administrator commands to it. It is meant to be standalone with control of
///program flow
class YOGClientRouterAdministrator
{
public:
	///Constructs this router administrator
	YOGClientRouterAdministrator();

	///Executes, running the console to output output and receive commands
	int execute();

private:
};


