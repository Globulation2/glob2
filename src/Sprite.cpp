/*
 * GAG sprite support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "Sprite.h"
#include <math.h>
#include "GlobalContainer.h"



// class for handling color lookup
void Palette::load(SDL_RWops *input, SDL_PixelFormat *format)
{
	char srcPal[768];
	int i,j;
	Uint8 r,g,b;

	this->format=format;
	SDL_RWread(input,srcPal,768,1);

	i=0;
	j=0;

	while (j<256)
	{
		r=srcPal[i];
		R[j]=r;
		i++;
		g=srcPal[i];
		G[j]=g;
		i++;
		b=srcPal[i];
		B[j]=b;
		i++;
		colors[j]=SDL_MapRGB(format,r,g,b);
		j++;
	}
}

void Palette::decHue(float degree)
{
	int n;
	float r,g,b;
	float h,s,v;
	for (n=0;n<256;n++)
	{
		r=((float)R[n])/255.0f;
		g=((float)G[n])/255.0f;
		b=((float)B[n])/255.0f;

		RGBtoHSV(r,g,b,&h,&s,&v);

		h+=degree;
		if (h<0.0f)
			h+=360.0f;
		else if (h>=360.0f)
			h-=360.0f;

		HSVtoRGB(&r,&g,&b,h,s,v);

		R[n]=(int)(r*255.0f);
		G[n]=(int)(g*255.0f);
		B[n]=(int)(b*255.0f);

		colors[n]=SDL_MapRGB(format,R[n],G[n],B[n]);
	}
}

void Palette::toBlackAndWhite(void)
{
	int n;
	for (n=0; n<256; n++)
	{
		/*int c=((int)R[n]+(int)G[n]+(int)B[n])/3;
		R[n]=(Uint8)c;
		G[n]=(Uint8)c;
		B[n]=(Uint8)c;
		colors[n]=SDL_MapRGB(format,c,c,c);*/
		R[n]>>=1;
		G[n]>>=1;
		B[n]>>=1;
		colors[n]=SDL_MapRGB(format,R[n],G[n],B[n]);
	}
}

// r,g,b values are from 0 to 1
// h = [0,360], s = [0,1], v = [0,1]
//		if s == 0, then h = -1 (undefined)
float Palette::fmin(float f1, float f2, float f3)
{
	if ((f1<=f2) && (f1<=f3))
		return f1;
	else if (f2<=f3)
		return f2;
	else
		return f3;
}

float Palette::fmax(float f1, float f2, float f3)
{
	if ((f1>=f2) && (f1>=f3))
		return f1;
	else if (f2>=f3)
		return f2;
	else
		return f3;
}

void Palette::RGBtoHSV( float r, float g, float b, float *h, float *s, float *v )
{
	float min, max, delta;
	min = fmin( r, g, b );
	max = fmax( r, g, b );
	*v = max;				// v
	delta = max - min;
	if( max != 0 )
		*s = delta / max;		// s
	else {
		// r = g = b = 0		// s = 0, v is undefined
		*s = 0;
		*h = -1;
		return;
	}
	if( r == max )
		*h = ( g - b ) / delta;		// between yellow & magenta
	else if( g == max )
		*h = 2 + ( b - r ) / delta;	// between cyan & yellow
	else
		*h = 4 + ( r - g ) / delta;	// between magenta & cyan
	*h *= 60;				// degrees
	if( *h < 0 )
		*h += 360;
}

void Palette::HSVtoRGB( float *r, float *g, float *b, float h, float s, float v )
{
	int i;
	float f, p, q, t;
	if( s == 0 ) {
		// achromatic (grey)
		*r = *g = *b = v;
		return;
	}
	h /= 60;			// sector 0 to 5
	i = (int)floor( h );
	f = h - i;			// factorial part of h
	p = v * ( 1 - s );
	q = v * ( 1 - s * f );
	t = v * ( 1 - s * ( 1 - f ) );
	switch( i ) {
		case 0:
			*r = v;
			*g = t;
			*b = p;
			break;
		case 1:
			*r = q;
			*g = v;
			*b = p;
			break;
		case 2:
			*r = p;
			*g = v;
			*b = t;
			break;
		case 3:
			*r = p;
			*g = q;
			*b = v;
			break;
		case 4:
			*r = t;
			*g = p;
			*b = v;
			break;
		default:		// case 5:
			*r = v;
			*g = p;
			*b = q;
			break;
	}
}


// class for handling palettized Sprite (like Units)
void PalSprite::load(SDL_RWops *input)
{
	// TODO : SDL_RWread(input,data,w,h); : clean this is a stupid old glob 1 ugly C loader
	// (thanks for the "stupid glob 1" comentary!)
	Uint32 num;
	Uint16 w,h;

	num=SDL_ReadBE32(input);
	h=SDL_ReadBE16(input);
	w=SDL_ReadBE16(input);

	if ((w&0x1)!=0)
		w++;

	if (data)
		free (data);
	data=(Uint8 *)malloc(w*h);
	SDL_RWread(input,data,w,h);
	this->w=(int)w;
	this->h=(int)h;
}

void PalSprite::save(SDL_RWops *input)
{
	Uint32 num;
	Uint16 w,h;
	w=(Uint16)this->w;
	h=(Uint16)this->h;
	num=0;

	SDL_WriteBE32(input, num);
	SDL_WriteBE16(input, h);
	SDL_WriteBE16(input, w);
	SDL_RWwrite(input,data,w,h);
}

void PalSprite::enableColorKey(Uint8 key)
{
	isColorKey=true;
	this->key=key;
}

void PalSprite::disableColorKey(void)
{
	isColorKey=false;
}

void PalSprite::draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y)
{
	int imgStartX, imgStartY, imgWidth, imgHeight;
	int dy, dx;
	Uint8 *srcPtr;

	// clip
	if (x<clip->x)
		imgStartX=clip->x-x;
	else
		imgStartX=0;
	if (y<clip->y)
		imgStartY=clip->y-y;
	else
		imgStartY=0;

	if (x+w>clip->x+clip->w)
		imgWidth=clip->x+clip->w-x;
	else
		imgWidth=w;
	if (y+h>clip->y+clip->h)
		imgHeight=clip->y+clip->h-y;
	else
		imgHeight=h;

	if ((imgHeight-imgStartY<=0) || (imgWidth-imgStartX<=0))
		return;

	if (dest->format->BitsPerPixel==16)
	{
		Uint16 *destPtr;
		if (isColorKey)
		{
			Uint8 color;
			for (dy=imgStartY;dy<imgHeight;dy++)
			{
				srcPtr=data+(dy)*w+imgStartX;
				destPtr=(Uint16 *)dest->pixels+(y+dy)*dest->w+x+imgStartX;
				for (dx=imgWidth-imgStartX-1;dx>=0;dx--)
				{
					color=*srcPtr;
					if (color!=key)
						*destPtr=(Uint16)(pal->colors[color]);
					destPtr++;
					srcPtr++;
				}
			}
		}
		else
		{
			for (dy=imgStartY;dy<imgHeight;dy++)
			{
				srcPtr=data+(dy)*w+imgStartX;
				destPtr=(Uint16 *)dest->pixels+(y+dy)*dest->w+x+imgStartX;
				for (dx=imgWidth-imgStartX-1;dx>=0;dx--)
				{
					*destPtr=(Uint16)(pal->colors[*srcPtr]);
					destPtr++;
					srcPtr++;
				}
			}
		}
	}
	else if (dest->format->BitsPerPixel==32)
	{
		Uint32 *destPtr;
		if (isColorKey)
		{
			Uint8 color;
			for (dy=imgStartY;dy<imgHeight;dy++)
			{
				srcPtr=data+(dy)*w+imgStartX;
				destPtr=(Uint32 *)dest->pixels+(y+dy)*dest->w+x+imgStartX;
				for (dx=imgWidth-imgStartX-1;dx>=0;dx--)
				{
					color=*srcPtr;
					if (color!=key)
						*destPtr=pal->colors[color];
					destPtr++;
					srcPtr++;
				}
			}
		}
		else
		{
			for (dy=imgStartY;dy<imgHeight;dy++)
			{
				srcPtr=data+(dy)*w+imgStartX;
				destPtr=(Uint32 *)dest->pixels+(y+dy)*dest->w+x+imgStartX;
				for (dx=imgWidth-imgStartX-1;dx>=0;dx--)
				{
					*destPtr=pal->colors[*srcPtr];
					destPtr++;
					srcPtr++;
				}
			}
		}
	}
	else
		assert(false); // we can only have 16 or 32 bpp
}

void IMGSprite::load(SDL_RWops *input)
{
	if (sprite)
		SDL_FreeSurface(sprite);

	SDL_Surface *temp;
	temp=IMG_Load_RW(input, 0);
	sprite=SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);
}

void IMGSprite::enableColorKey(Uint8 key)
{
	SDL_SetColorKey(sprite, SDL_SRCCOLORKEY, key);
}

void IMGSprite::disableColorKey(void)
{
	SDL_SetColorKey(sprite,  0, 0);
}

void IMGSprite::draw(SDL_Surface *dest, const SDL_Rect *clip, int x, int y)
{
	SDL_Rect oldr, r;
	SDL_Rect newr=*clip;

	r.x=x;
	r.y=y;
	r.w=sprite->w;
	r.h=sprite->h;
	SDL_GetClipRect(dest, &oldr);
	SDL_SetClipRect(dest, &newr);
	SDL_BlitSurface(sprite, NULL, dest, &r);
	SDL_SetClipRect(dest, &oldr);
}

GraphicArchive::GraphicArchive()
{
}

void GraphicArchive::load(const char *filename)
{
	int i=0;
	SDL_RWops *stream=globalContainer->fileManager.open(filename,"rb");
	if (stream)
	{
		int endPos=SDL_RWseek(stream, 0, SEEK_END);
		SDL_RWseek(stream, 0, SEEK_SET);
		fprintf(stderr, "ARC : Loading Graphic Archive : %s of %d bytes\n",filename,endPos-SDL_RWtell(stream));
		while (SDL_RWtell(stream)<endPos)
		{
			Sprite *sprite=loadSprite(stream);
			sprites.push_back(sprite);
			//fprintf(stderr, " |- Sprite %d : %d x %d\n", i++, sprite->getW(), sprite->getH());
		}
		SDL_RWclose(stream);
	}
	else
		fprintf(stderr, "ARC : ERR : Can't open file : %s\n", filename);
}

void GraphicArchive::save(const char *filename)
{
	SDL_RWops *stream=globalContainer->fileManager.open(filename,"wb");
	for (std::vector<Sprite *>::iterator it=sprites.begin(); it!=sprites.end(); it++)
	{
		(*it)->save(stream);
	}
	SDL_RWclose(stream);
}

void GraphicArchive::freeMe()
{
	fprintf(stderr, "ARC : Freeing Graphic Archive.\n");
	for (std::vector<Sprite *>::iterator it=sprites.begin(); it!=sprites.end(); it++)
		if ((*it))
		{
			delete (*it);
			(*it)=NULL;
		}
	sprites.clear();
}

void GraphicArchive::enableColorKey(Uint8 key)
{
	for (std::vector<Sprite *>::iterator it=sprites.begin(); it!=sprites.end(); it++)
	{
		(*it)->enableColorKey(key);
	}
}

void GraphicArchive::disableColorKey()
{
	for (std::vector<Sprite *>::iterator it=sprites.begin(); it!=sprites.end(); it++)
	{
		(*it)->disableColorKey();
	}
}

Sprite *GraphicArchive::getSprite(int n)
{
	if ((n<(int)sprites.size())&&(n>=0))
	{
		return sprites[n];
	}
	else
	{
		fprintf(stderr, "ARC : ERR : Trying to load sprite %d, but there is only %d sprites\n", n, (int)sprites.size());
		return 0;
	}
}

Sprite *MacPalGraphicArchive::loadSprite(SDL_RWops *stream)
{
	PalSprite *sprite=new PalSprite();
	sprite->load(stream);
	sprite->setPal(defaultPal);
	return sprite;
}

Sprite *IMGGraphicArchive::loadSprite(SDL_RWops *stream)
{
	IMGSprite *sprite=new IMGSprite();
	sprite->load(stream);
	return sprite;
}

