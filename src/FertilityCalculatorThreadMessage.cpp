/*
  Copyright (C) 2007 Bradley Arsenault

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


