// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "FertilityCalculatorThreadMessage.h"

#include <sstream>
#include <typeinfo>

FCTUpdateCompletionPercent::FCTUpdateCompletionPercent(float percent)
	: percent(percent)
{
}



Uint8 FCTUpdateCompletionPercent::getMessageType() const
{
	return FCTMUpdateCompletionPercent;
}



std::string FCTUpdateCompletionPercent::format() const
{
	std::ostringstream s;
	s<<"FCTUpdateCompletionPercent("<<"percent="<<percent<<"; "<<")";
	return s.str();
}



bool FCTUpdateCompletionPercent::operator==(const FertilityCalculatorThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(FCTUpdateCompletionPercent))
	{
		const FCTUpdateCompletionPercent& r = dynamic_cast<const FCTUpdateCompletionPercent&>(rhs);
		if(r.percent == percent)
			return true;
	}
	return false;
}


float FCTUpdateCompletionPercent::getPercent() const
{
	return percent;
}



FCTFertilityCompleted::FCTFertilityCompleted()
{
}



Uint8 FCTFertilityCompleted::getMessageType() const
{
	return FCTMFertilityCompleted;
}



std::string FCTFertilityCompleted::format() const
{
	std::ostringstream s;
	s<<"FCTFertilityCompleted()";
	return s.str();
}



bool FCTFertilityCompleted::operator==(const FertilityCalculatorThreadMessage& rhs) const
{
	if(typeid(rhs)==typeid(FCTFertilityCompleted))
	{
		//const FCTFertilityCompleted& r = dynamic_cast<const FCTFertilityCompleted&>(rhs);
		return true;
	}
	return false;
}


//code_append_marker


