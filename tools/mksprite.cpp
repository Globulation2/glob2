/*
  This file is part of Globulation 2, a free software real-time strategy game
  http://glob2.ysagoon.com
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors
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

#include <Magick++.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <algorithm>

using namespace std;
using namespace Magick;

struct Frame
{
	int x, y, w, h;
	int plane;
};
vector<Frame> frames;
vector<Image> planes;

class Layouter
{
protected:
	unsigned texsize;
	unsigned posx;
	unsigned posy;
	unsigned maxy;
	
public:
	Layouter()
	{
		texsize = 256;
		posx = 0;
		posy = 0;
		maxy = 0;
	}
	
	void writeFrame(const Geometry &frameSize, const Image &frame)
	{
		Frame f;
		f.x = posx;
		f.y = posy;
		f.w = frameSize.width();
		f.h = frameSize.height();
		planes[planes.size() - 1].draw(DrawableCompositeImage(posx, posy, frame));
		frames.push_back(f);
	}

	void layout(size_t count, char *files[])
	{
		planes.push_back(Image(Geometry(texsize, texsize), Color("transparent")));
		for (size_t i=0; i<count; i++)
		{
			Image frame(files[i]);
			frame.matte(true);
			Geometry size = frame.size();
			if ( (posy + size.height() < texsize) && (posx + size.width() < texsize) )
			{
				writeFrame(size, frame);
				posx += size.width();
				maxy = max(maxy, posy + size.height());
			}
			else if (maxy + size.height() < texsize)
			{
				posy = maxy;
				posx = 0;
				writeFrame(size, frame);
			}
			else
			{
				cerr << "Next frame" << std::endl;
				planes.push_back(Image(Geometry(texsize, texsize), Color("transparent")));
				posx = posy = maxy = 0;
				writeFrame(size, frame);
			}
		}
		for (size_t i=0; i<planes.size(); i++)
		{
			ostringstream planeFileName;
			planeFileName << "plane" << i << ".png";
			planes[i].write(planeFileName.str());
		}
	}
};

int main(int argc, char *argv[])
{
	if (argc == 1)
		return 1;
	Layouter l;
	try
	{
		l.layout(argc-1, argv+1);	
	}
	catch ( Exception &error_ ) 
	{ 
		cerr << "Caught exception: " << error_.what() << endl; 
	}
	return 0;
}
