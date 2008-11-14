/*
  This file is part of Globulation 2, a free software real-time strategy game
  http://www.globulation2.org
  Copyright (C) 2001-2005 Stephane Magnenat & Luc-Olivier de Charriere and other contributors
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

#include <Magick++.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
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

unsigned initialTexSize = 256;

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
		texsize = initialTexSize;
		posx = 0;
		posy = 0;
		maxy = 0;
	}
	
	void writeFrame(const Geometry &frameSize, const Image &frame, bool doDraw)
	{
		Frame f;
		f.x = posx;
		f.y = posy;
		f.w = frameSize.width();
		f.h = frameSize.height();
		f.plane = planes.size() - 1;
		if (doDraw)
			planes[planes.size() - 1].draw(DrawableCompositeImage(posx, posy, frame));
		frames.push_back(f);
	}

	bool layout(size_t count, char *files[], bool doDraw)
	{
		planes.clear();
		planes.push_back(Image(Geometry(texsize, texsize), Color("transparent")));
		frames.clear();
		posx = 0;
		posy = 0;
		maxy = 0;
		for (size_t i=0; i<count; i++)
		{
			Image frame(files[i]);
			frame.matte(true);
			Geometry size = frame.size();
			if ( (posy + size.height() <= texsize) && (posx + size.width() <= texsize) )
			{
				writeFrame(size, frame, doDraw);
				posx += size.width();
				maxy = max(maxy, posy + size.height());
			}
			else if ( (maxy + size.height() <= texsize) && (size.width() <= texsize) )
			{
				posy = maxy;
				posx = 0;
				writeFrame(size, frame, doDraw);
				posx += size.width();
				maxy = max(maxy, posy + size.height());
			}
			else
			{
				if ((size.width() > texsize) || (size.height() > texsize))
					return false;
				planes.push_back(Image(Geometry(texsize, texsize), Color("transparent")));
				posx = posy = maxy = 0;
				writeFrame(size, frame, doDraw);
				posx += size.width();
				maxy = max(maxy, posy + size.height());
			}
		}
		return true;
	}
	
	bool findBestLayout(size_t count, char *files[])
	{
		unsigned oldTexSize;
		bool layoutResult;
		do
		{
			oldTexSize = texsize;
			texsize >>= 1;
			cout << "Trying texture of " << texsize << endl;
			layoutResult = layout(count, files, false);
		}
		while (layoutResult && (frames.size() > 0) && (planes.size() == 1) && (texsize > 0));
		
		texsize = oldTexSize;
		cout << "Using texture of " << texsize << endl;
		layoutResult = layout(count, files, true);
		
		return (frames.size() != 0) && layoutResult;
	}
	
	void writeSpritePlanes(const char *spriteName)
	{
		for (size_t i=0; i<planes.size(); i++)
		{
			ostringstream planeFileName;
			planeFileName << spriteName << ".plane" << i << ".png";
			planes[i].depth(8);
			planes[i].write(planeFileName.str());
		}
	}
	
	void writeSpriteText(const char *spriteName)
	{
		ostringstream spriteFileName;
		spriteFileName << spriteName << ".sprite";
		ofstream spriteFile(spriteFileName.str().c_str());
		if (spriteFile)
		{
			spriteFile << "frameCount = " << frames.size() << ";\n";
			spriteFile << "planeFileName = " << spriteName << ";\n";
			for (size_t i=0; i<frames.size(); i++)
			{
				spriteFile << i << "\n";
				spriteFile << "{\n";
				spriteFile << "\tx = " << frames[i].x << ";\n";
				spriteFile << "\ty = " << frames[i].y << ";\n";
				spriteFile << "\tw = " << frames[i].w << ";\n";
				spriteFile << "\th = " << frames[i].h << ";\n";
				spriteFile << "\tplane = " << frames[i].plane << ";\n";
				spriteFile << "}\n";
			}
			spriteFile << endl;
		}
		else
		{
			cerr << "Can't create sprite description file " << spriteFileName.str() << endl;
			exit(3);
		}
	}
};

void usage(const char *exeName)
{
	cerr << "Usage:\n" << exeName << " (-maxtexturesize) [sprite name] [frame images] ..." << endl;
}

int main(int argc, char *argv[])
{
	if (argc < 3)
	{
		usage(argv[0]);
		return 1;
	}
	if (argv[1][0] == '-')
	{
		initialTexSize = atoi(&argv[1][1]);
		cout << "Changing initialTexSize to " << initialTexSize << endl;
		argc--;
		argv++;
	}
	Layouter l;
	try
	{
		if (l.findBestLayout(argc-2, argv+2))
		{
			l.writeSpritePlanes(argv[1]);
			l.writeSpriteText(argv[1]);
		}
		else
			cerr << "Can't find a suitable layout !" << endl;
	}
	catch ( Exception &error_ ) 
	{ 
		cerr << "Caught exception: " << error_.what() << endl; 
	}
	return 0;
}
