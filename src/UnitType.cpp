/*
 * Globulation 2 unit type
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "UnitType.h"

UnitType& UnitType::operator+=(const UnitType &a)
{
	for (int i=0; i<NB_MOVE; i++)
		startImage[i]=a.startImage[i];

	hungryness+=a.hungryness;
	
	for (int i=0; i<NB_ABILITY; i++)
		performance[i]+=a.performance[i];
	
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
	
	for (int i=0; i<NB_ABILITY; i++)
		performance[i]/=a;

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
	for (int i=0; i<NB_ABILITY; i++)
		performance[i]*=a;
	
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
	
	for (int i=0; i<NB_ABILITY; i++)
		r+= a.performance[i] * this->performance[i];

	return r;
}


void UnitType::copyIf(const UnitType a, const UnitType b)
{
	for (int i=0; i<NB_MOVE; i++)
		startImage[i]=a.startImage[i];

	if (b.hungryness)
		hungryness=a.hungryness;
	
	for (int i=0; i<NB_ABILITY; i++)
		if (b.performance[i])
			performance[i]=a.performance[i];
}


void UnitType::copyIfNot(const UnitType a, const UnitType b)
{
	for (int i=0; i<NB_MOVE; i++)
		startImage[i]=a.startImage[i];

	
	if (!(b.hungryness))
		hungryness=a.hungryness;
	
	for (int i=0; i<NB_ABILITY; i++)
		if (!(b.performance[i]))
			performance[i]=a.performance[i];
}
