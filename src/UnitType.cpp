/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "UnitType.h"
#include <Stream.h>

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

void UnitType::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	startImage[STOP_WALK] = stream->readUint32("startImageStopWalk");
	startImage[STOP_SWIM] = stream->readUint32("startImageStopSwim");
	startImage[STOP_FLY] = stream->readUint32("startImageStopFly");
	startImage[WALK] = stream->readUint32("startImageWalk");
	startImage[SWIM] = stream->readUint32("startImageSwim");
	startImage[FLY] = stream->readUint32("startImageFly");
	startImage[BUILD] = stream->readUint32("startImageBuild");
	startImage[HARVEST] = stream->readUint32("startImageHarvest");
	startImage[ATTACK_SPEED] = stream->readUint32("startImageAttack");

	hungryness = stream->readSint32("hungryness");

	performance[STOP_WALK] = stream->readSint32("stopWalkSpeed");
	performance[STOP_SWIM] = stream->readSint32("stopSwimSpeed");
	performance[STOP_FLY] = stream->readSint32("stopFlySpeed");
	performance[WALK] = stream->readSint32("walkSpeed");
	performance[SWIM] = stream->readSint32("swimSpeed");
	performance[FLY] = stream->readSint32("flySpeed");
	performance[BUILD] = stream->readSint32("buildSpeed");
	performance[HARVEST] = stream->readSint32("harvestSpeed");
	performance[ATTACK_SPEED] = stream->readSint32("attackSpeed");
	performance[ATTACK_STRENGTH] = stream->readSint32("attackForce");
	performance[MAGIC_ATTACK_AIR] = stream->readSint32("magicAttackAir");
	performance[MAGIC_ATTACK_GROUND] = stream->readSint32("magicAttackGround");
	performance[MAGIC_CREATE_WOOD] = stream->readSint32("magicCreateWood");
	performance[MAGIC_CREATE_CORN] = stream->readSint32("magicCreateCorn");
	performance[MAGIC_CREATE_ALGA] = stream->readSint32("magicCreateAlga");
	performance[ARMOR] = stream->readSint32("armor");
	performance[HP] = stream->readSint32("hpMax");

	harvestDamage = stream->readSint32("harvestDamage");
	armorReductionPerHappyness = stream->readSint32("armorReductionPerHappyness");
	experiencePerLevel = stream->readSint32("experiencePerLevel");
	magicActionCooldown = stream->readSint32("magicActionCooldown");
}

void UnitType::save(GAGCore::OutputStream *stream)
{
	stream->writeUint32(startImage[STOP_WALK], "startImageStopWalk");
	stream->writeUint32(startImage[STOP_SWIM], "startImageStopSwim");
	stream->writeUint32(startImage[STOP_FLY], "startImageStopFly");
	stream->writeUint32(startImage[WALK], "startImageWalk");
	stream->writeUint32(startImage[SWIM], "startImageSwim");
	stream->writeUint32(startImage[FLY], "startImageFly");
	stream->writeUint32(startImage[BUILD], "startImageBuild");
	stream->writeUint32(startImage[HARVEST], "startImageHarvest");
	stream->writeUint32(startImage[ATTACK_SPEED], "startImageAttack");

	stream->writeSint32(hungryness, "hungryness");

	stream->writeSint32(performance[STOP_WALK], "stopWalkSpeed");
	stream->writeSint32(performance[STOP_SWIM], "stopSwimSpeed");
	stream->writeSint32(performance[STOP_FLY], "stopFlySpeed");
	stream->writeSint32(performance[WALK], "walkSpeed");
	stream->writeSint32(performance[SWIM], "swimSpeed");
	stream->writeSint32(performance[FLY], "flySpeed");
	stream->writeSint32(performance[BUILD], "buildSpeed");
	stream->writeSint32(performance[HARVEST], "harvestSpeed");
	stream->writeSint32(performance[ATTACK_SPEED], "attackSpeed");
	stream->writeSint32(performance[ATTACK_STRENGTH], "attackForce");
	stream->writeSint32(performance[MAGIC_ATTACK_AIR], "magicAttackAir");
	stream->writeSint32(performance[MAGIC_ATTACK_GROUND], "magicAttackGround");
	stream->writeSint32(performance[MAGIC_CREATE_WOOD], "magicCreateWood");
	stream->writeSint32(performance[MAGIC_CREATE_CORN], "magicCreateCorn");
	stream->writeSint32(performance[MAGIC_CREATE_ALGA], "magicCreateAlga");
	stream->writeSint32(performance[ARMOR], "armor");
	stream->writeSint32(performance[HP], "hpMax");

	stream->writeSint32(harvestDamage, "harvestDamage");
	stream->writeSint32(armorReductionPerHappyness, "armorReductionPerHappyness");
	stream->writeSint32(experiencePerLevel, "experiencePerLevel");
	stream->writeSint32(magicActionCooldown, "magicActionCooldown");
}

Uint32 UnitType::checkSum(void)
{
	Uint32 cs = 0;
	cs ^= hungryness;
	cs = (cs<<1) | (cs>>31);
	for (int i=STOP_WALK; i<HP; i++)
	{
		cs ^= performance[i];
		cs = (cs<<1) | (cs>>31);
	}
	cs ^= harvestDamage;
	cs = (cs<<1) | (cs>>31);
	cs ^= armorReductionPerHappyness;
	cs = (cs<<1) | (cs>>31);
	cs ^= experiencePerLevel;
	cs = (cs<<1) | (cs>>31);
	cs ^= magicActionCooldown;
	cs = (cs<<1) | (cs>>31);
	return cs;
}
