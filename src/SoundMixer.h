/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Martin Voelkle
  for any question or comment contact us at nct@ysagoon.com or marv@ysagoon.com

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


#ifndef __SOUNDMIXER_H
#define __SOUNDMIXER_H

#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <vector>

class SoundMixer
{
public:
	std::vector<OggVorbis_File *> tracks;
	int actTrack, nextTrack;
	bool earlyChange;
	bool soundEnabled;

public:
	SoundMixer();

	~SoundMixer();

	int loadTrack(const char *name);

	void setNextTrack(unsigned i, bool earlyChange=false);
};

#endif



