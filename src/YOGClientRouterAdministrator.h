// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGClientRouterAdministrator_h
#define YOGClientRouterAdministrator_h

///This class allows a player to connect to a YOG router and send and recieve
///administrator commands to it. It is meant to be standalone with control of
///program flow
class YOGClientRouterAdministrator
{
public:
	///Constructs this router admnistrator
	YOGClientRouterAdministrator();

	///Executes, running the console to output output and recieve commands
	int execute();

private:
};


#endif
