// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef FertilityCalculatorThreadMessage_h
#define FertilityCalculatorThreadMessage_h

#include <string>
#include "SDL_net.h"

enum FertilityCalculatorThreadMessageType
{
	FCTMUpdateCompletionPercent,
	FCTMFertilityCompleted,
	//type_append_marker
};


///This class represents a message sent between the main thread and the thread that manages fertility calculations
class FertilityCalculatorThreadMessage
{
public:
	virtual ~FertilityCalculatorThreadMessage() {}

	///Returns the event type
	virtual Uint8 getMessageType() const = 0;
	
	///Returns a formatted version of the event
	virtual std::string format() const = 0;
	
	///Compares two IRCThreadMessageType
	virtual bool operator==(const FertilityCalculatorThreadMessage& rhs) const = 0;
};


///FCTUpdateCompletionPercent
class FCTUpdateCompletionPercent : public FertilityCalculatorThreadMessage
{
public:
	///Creates a FCTUpdateCompletionPercent event
	FCTUpdateCompletionPercent(float percent);

	///Returns FCTMUpdateCompletionPercent
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two FertilityCalculatorThreadMessage
	bool operator==(const FertilityCalculatorThreadMessage& rhs) const;

	///Retrieves percent
	float getPercent() const;
private:
	float percent;
};




///FCTFertilityCompleted
class FCTFertilityCompleted : public FertilityCalculatorThreadMessage
{
public:
	///Creates a FCTFertilityCompleted event
	FCTFertilityCompleted();

	///Returns FCTMFertilityCompleted
	Uint8 getMessageType() const;

	///Returns a formatted version of the event
	std::string format() const;
	
	///Compares two FertilityCalculatorThreadMessage
	bool operator==(const FertilityCalculatorThreadMessage& rhs) const;
};



//event_append_marker

#endif

