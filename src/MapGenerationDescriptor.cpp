/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "MapGenerationDescriptor.h"

MapGenerationDescriptor::MapGenerationDescriptor()
{
	terrainType=Map::GRASS;
	methode=eUNIFORM;
	waterRatio=50;
	sandRatio=50;
	grassRatio=50;
	smooth=4;
}


MapGenerationDescriptor::~MapGenerationDescriptor()
{
}

char *MapGenerationDescriptor::getData()
{
	data[0]=0;
	return data;
}

bool MapGenerationDescriptor::setData(const char *data, int dataLength)
{
	assert(getDataLength()==dataLength);
	memcpy(this->data, data, dataLength);
	return (getDataLength()==dataLength);
}

Sint32 MapGenerationDescriptor::checkSum()
{
	return 0;
}
