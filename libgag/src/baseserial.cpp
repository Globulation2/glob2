/*
    DPS - Dynamic Parallel Schedules
    Copyright (C) 2000-2003 Sebastian Gerlach

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*!	\file baseserial.cpp
	\brief Serializer implementation

	Implementation of methods used in serializer.
*/

#include <dps/base.h>

namespace base
{
	//! Destructor
	RefVector::~RefVector()
	{
		for(Size i=1;i<curSize;i++)
			contents[i]->downcount();
		free(contents);
	}

	//! Add an element to the vector
	/*! If the object is already in the vector, it is not added.
		/param el Object to add
		/retval Index of the object in the vector
	*/
	Size RefVector::addElement(Object* el)
	{
		for(Size i=0;i<curSize;i++)
			if(contents[i]==el)
				return i;
		if(curSize==maxSize)
		{
			maxSize+=increment;
			contents=(Object**)realloc(contents,maxSize*sizeof(Object*));
		}
		contents[curSize++]=el;
		el->upcount();
		return curSize-1;
	}

	//! Destructor
	ConstRefVector::~ConstRefVector()
	{
		for(Size i=1;i<curSize;i++)
			contents[i]->downcount();
		free(contents);
	}

	//! Add an element to the vector
	/*! If the object is already in the vector, it is not added.
		/param el Object to add
		/retval Index of the object in the vector
	*/
	Size ConstRefVector::addElement(const Object* el)
	{
		for(Size i=0;i<curSize;i++)
			if(contents[i]==el)
				return i;
		if(curSize==maxSize)
		{
			maxSize+=increment;
			contents=(const Object**)realloc(contents,maxSize*sizeof(Object*));
		}
		contents[curSize++]=el;
		el->upcount();
		return curSize-1;
	}

}

