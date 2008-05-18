/*
  Copyright (C) 2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

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

