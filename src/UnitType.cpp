/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "UnitType.h"

UnitType& UnitType::operator+=(const UnitType &a)
{
	{
		for (int i=0; i<NB_MOVE; i++)
			startImage[i]=a.startImage[i];
	}

	hungryness+=a.hungryness;

	{	
		for (int i=0; i<NB_ABILITY; i++)
			performance[i]+=a.performance[i];
	}
	return *this;	
}

UnitType UnitType::operator+(const UnitType &a)
{
	UnitType r;
	r=*this;
	r+=a;
	return r;
}

UnitType& UnitType::operator/=(int a)
{
	hungryness/=a;

	{
		for (int i=0; i<NB_ABILITY; i++)
			performance[i]/=a;
	}

	return *this;	
}

UnitType UnitType::operator/(int a)
{
	UnitType r;
	r=*this;
	r/=a;
	return r;
}

UnitType& UnitType::operator*=(int a)
{
	
	hungryness*=a;
	{
		for (int i=0; i<NB_ABILITY; i++)
			performance[i]*=a;
	}
	
	return *this;	
}

UnitType UnitType::operator*(int a)
{
	UnitType r;
	r=*this;
	r*=a;
	return r;
}

int UnitType::operator*(const UnitType &a)
{
	int r=0;

	{
		for (int i=0; i<NB_ABILITY; i++)
			r+= a.performance[i] * this->performance[i];
	}

	return r;
}


void UnitType::copyIf(const UnitType a, const UnitType b)
{
	{
		for (int i=0; i<NB_MOVE; i++)
			startImage[i]=a.startImage[i];
	}

	if (b.hungryness)
		hungryness=a.hungryness;
	{
		for (int i=0; i<NB_ABILITY; i++)
			if (b.performance[i])
				performance[i]=a.performance[i];
	}
}


void UnitType::copyIfNot(const UnitType a, const UnitType b)
{
	{
		for (int i=0; i<NB_MOVE; i++)
			startImage[i]=a.startImage[i];
	}

	
	if (!(b.hungryness))
		hungryness=a.hungryness;

	{	
		for (int i=0; i<NB_ABILITY; i++)
			if (!(b.performance[i]))
				performance[i]=a.performance[i];
	}
}
