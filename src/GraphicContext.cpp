/*
 * GAG graphic context file
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include <stdarg.h>
#include <stdio.h>
#include "GraphicContext.h"
#include <math.h>
#include "Utilities.h"
#include "GlobalContainer.h"

extern GlobalContainer globalContainer;

SDLBitmapFont::SDLBitmapFont()
{
	picture=NULL;
	CharPos=NULL;
}

SDLBitmapFont::SDLBitmapFont(const char *filename)
{
	load(filename);
}

SDLBitmapFont::~SDLBitmapFont()
{
	if (picture)
		SDL_FreeSurface(picture);
	if (CharPos)
		delete[] CharPos;
}

int SDLBitmapFont::getStringWidth(const char *string) const
{
	return textWidth(string);
}

int SDLBitmapFont::getStringHeight(const char *string) const
{
	return height;
}

bool SDLBitmapFont::load(const char *filename)
{
	init();
	
	SDL_Surface *temp, *sprite;
	SDL_RWops *stream=globalContainer.fileManager.open(filename, "r");		
	temp=IMG_Load_RW(stream, 0);
	SDL_FreeRW(stream);	
	sprite=SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);	

	return load(sprite);
}

void SDLBitmapFont::drawString(SDL_Surface *Surface, int x, int y, const char *text, SDL_Rect *clip) const
{
	if ((!picture) || (!Surface) || (!text))
		return;
	int ofs, i=0;
	SDL_Rect srcrect, dstrect;
	while (text[i]!='\0')
	{
		if (text[i]==' ')
		{
			x+=spacew;
			i++;
		}
		else if ((text[i]>=startChar)&&(text[i]<=lastChar))
		{
			ofs=2*(text[i]-startChar);
			srcrect.w = dstrect.w = (Uint16) ( (CharPos[ofs+3]+CharPos[ofs+2])/2-(CharPos[ofs+1]+CharPos[ofs+0])/2 );
			srcrect.h = dstrect.h = (Uint16) height;
			srcrect.x = (Sint16) ( (CharPos[ofs+1]+CharPos[ofs+0])/2 );
			srcrect.y = 1;
			dstrect.x = (Sint16) ( x-(CharPos[ofs+1]-CharPos[ofs+0])/2 );
			dstrect.y = (Sint16) y;
			x+=CharPos[ofs+2]-CharPos[ofs+1];
			if (clip)
				Utilities::sdcRects(&srcrect, &dstrect, *clip);
			SDL_BlitSurface( picture, &srcrect, Surface, &dstrect);
			i++;
		}
		else
			i++;// other chars are ignored
	}
}

void SDLBitmapFont::init()
{
	picture=NULL;
	CharPos=NULL;
	lastChar=startChar-1;
	height=0;
	spacew=0;
	backgroundR=0;
	backgroundG=0;
	backgroundB=0;
}

void SDLBitmapFont::getPixel(Sint32 x, Sint32 y, Uint8 *r, Uint8 *g, Uint8 *b)
{
	if ((!picture)||(x<0)||(x>=picture->w))
	{
    	fprintf(stderr, "VID : SDLBitmapFontGetPixel recieved a bad parameter.\n");
    	assert(false);
    	return;
	}

	int bpp = picture->format->BytesPerPixel;

	Uint8 *row = (Uint8 *)picture->pixels + y*picture->pitch + x*picture->format->BytesPerPixel;

	Uint32 pixel;

	switch (bpp)
	{
		case 1:
		{
			Uint8 *cp = (Uint8 *)row;
			pixel = (Uint32)*cp;
		}
		break;

		case 2:
		{
			Uint16 *cp = (Uint16 *)row;
			pixel = (Uint32)*cp;
		}
		break;

		case 3:	
		{
			Uint32 *cp = (Uint32 *)row;		
			pixel = (Uint32)*cp;
			if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
				pixel >>= 8;
		}
		break;
		
		case 4:
		{
			Uint32 *cp = (Uint32 *)row;
			pixel = (Uint32)*cp;
		}
		break;
	}
	
	SDL_GetRGB(pixel, picture->format, r, g, b);
}
void SDLBitmapFont::setBackGround(Sint32 x, Sint32 y)
{
	getPixel(x, y, &backgroundR, &backgroundG, &backgroundB);
	Uint32 background=SDL_MapRGB(picture->format, backgroundR, backgroundG, backgroundB);	
	SDL_SetColorKey(picture, SDL_SRCCOLORKEY, background);
}
bool SDLBitmapFont::isBackGround(Sint32 x, Sint32 y)
{
	Uint8 r, g, b;
	getPixel(x, y, &r, &g, &b);	
	return ((r==backgroundR)&&(g==backgroundG)&&(b==backgroundB));
} 	

bool SDLBitmapFont::doStartNewChar(int x)
{
	if (!picture)
		return false;
	
	Uint8 r, g, b;
	getPixel(x, 0, &r, &g, &b);

	return ((r==255)&&(g==0)&&(b=255));
}

int SDLBitmapFont::shorteringChar(int x)
{
	//This do count the number of (255,255,0) pixels, starting from x.
	if (!picture)
		return false;
	
	int s=0;
	
	Uint8 r, g, b;
	getPixel((x+s), 0, &r, &g, &b);


	while ((r==255)&&(g==255)&&(b==0))
	{
		s++;
		getPixel((x+s), 0, &r, &g, &b);
	}
	return s;
}


bool SDLBitmapFont::load(SDL_Surface *fontSurface)
{
	int x=0, i=0;
	
	int CharPos[256];
	
	if (!fontSurface)
	{
    	fprintf(stderr, "VID : SDLBitmapFont received a NULL SDL_Surface\n");
    	assert(false);
    	return false;
    }
    picture=fontSurface;
    height=picture->h-1;
	while (x < picture->w)
	{
		if(doStartNewChar(x))
		{
			CharPos[i++]=x;
			while (( x < picture->w-1) && (doStartNewChar(x)))
				x++;
			
			int s=shorteringChar(x);			
			CharPos[i++]=x-s;
			
			//printf("CharPos[%d]=%d, CharPos[%d]=%d\n", i-2, CharPos[i-2], i-1, CharPos[i-1]);
		}
		x++;
	}
	CharPos[i++]=picture->w;
	
	lastChar=startChar+(i/2)-1;
	setBackGround(0, height);

	
	this->CharPos=new int[i];
	memcpy(this->CharPos, CharPos, i*sizeof(int));
	
	//We search for a smart space width:
	spacew=0;
	if (!spacew)
		spacew=textWidth("a");
	if (!spacew)
		spacew=textWidth("A");
	if (!spacew)
		spacew=textWidth("0");
	if (!spacew)
		spacew=CharPos[1]-CharPos[0];
	
	return true;
}

int SDLBitmapFont::textWidth(const char *text, int min, int max) const
{
	if (!picture)
		return 0;
	int ofs, x=0,i=min;
	while ((text[i]!='\0')&&(i<max))
	{
		if (text[i]==' ')
		{
			x+=spacew;
			i++;
		}
		else if ((text[i]>=startChar)&&(text[i]<=lastChar))
		{
			ofs=2*(text[i]-startChar);
			x+=CharPos[ofs+2]-CharPos[ofs+1];
			i++;
		}
		else
			i++;
	}
	return x;
}

bool SDLBitmapFont::printable(char c)
{
	
	//printf("startChar=%d, lastChar=%d, ' '=%d \n", startChar, lastChar, ' ');
	return (((c>=startChar)&&(c<=lastChar)) || (c==' '));
}

SDLGraphicContext::SDLGraphicContext(void)
{
	screen=NULL;

	// Load the SDL library
	if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO)<0 )
	{
		fprintf(stderr, "SDL : Initialisation Error : %s\n", SDL_GetError());
		exit(1);
	}
	else
	{
		fprintf(stderr, "SDL : Initialized : Graphic Context created\n");
	}

	atexit(SDL_Quit);

	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	SDL_EnableUNICODE(1);
}

SDLGraphicContext::~SDLGraphicContext(void)
{
	fprintf(stderr, "SDL : Graphic Context destroyed\n");
}

bool SDLGraphicContext::setRes(int w, int h, int depth, Uint32 flags)
{
	depth=32;
	screen = SDL_SetVideoMode(w, h, depth, flags);

	if (!screen)
	{
		fprintf(stderr, "VID : %s\n", SDL_GetError());
		return false;
	}
	else
	{
		setClipRect(NULL);
		if (flags&SDL_FULLSCREEN)
			fprintf(stderr, "VID : Screen set to %dx%d with %d bpp in fullscreen\n", w, h, depth);
		else
			fprintf(stderr, "VID : Screen set to %dx%d with %d bpp in window\n", w, h, depth);
		return true;
	}
}

void SDLGraphicContext::dbgprintf(const char *msg, ...)
{
	va_list arglist;

	fprintf(stderr,"DBG : ");

	va_start(arglist, msg);
    vfprintf(stderr, msg, arglist );
	va_end(arglist);

	fprintf(stderr,"\n");
}

void SDLGraphicContext::setClipRect(int x, int y, int w, int h)
{
	clipRect.x=x;
	clipRect.y=y;
	clipRect.w=w;
	clipRect.h=h;
	SDL_SetClipRect(screen, &clipRect);
}

void SDLGraphicContext::setClipRect(SDL_Rect *rect)
{
	if (rect)
	{
		clipRect=*rect;
	}
	else
	{
		clipRect.x=0;
		clipRect.y=0;
		clipRect.w=screen->w;
		clipRect.h=screen->h;
	}
	SDL_SetClipRect(screen, &clipRect);
}


void SDLGraphicContext::drawSprite(Sprite *sprite, int x, int y)
{
	sprite->draw(screen, &clipRect, x, y);
}

void SDLGraphicContext::drawPixel(int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	// TODO : do the alpha !!!
	if ((x<clipRect.x) || (x>=clipRect.x+clipRect.w) || (y<clipRect.y) || (y>=clipRect.y+clipRect.h))
		return;

 	/* Set the pixel */
    switch(screen->format->BitsPerPixel)
	{
        case 8:
		{
            *(((Uint8 *)screen->pixels)+y*screen->pitch+x) = (Uint8)SDL_MapRGB(screen->format, r, g, b);
		}
		break;
        case 16:
		{
            Uint16 *mem=((Uint16 *)screen->pixels)+y*(screen->pitch>>1)+x;
			Uint8 dr, dg, db;
			Uint8 na=255-a;
			SDL_GetRGB(*mem, screen->format, &dr, &dg, &db);
			r=(a*r+na*dr)>>8;
			g=(a*g+na*dg)>>8;
			b=(a*b+na*db)>>8;
            *mem = (Uint16)SDL_MapRGB(screen->format, r, g, b);
		}
		break;
        case 24:
		{
			Uint8 *bits=((Uint8 *)screen->pixels+y*screen->pitch+x);
			Uint32 pixel=SDL_MapRGB(screen->format, r, g, b);
			{ /* Format/endian independent */
                Uint8 nr, ng, nb;

                nr = (pixel>>screen->format->Rshift)&0xFF;
                ng = (pixel>>screen->format->Gshift)&0xFF;
                nb = (pixel>>screen->format->Bshift)&0xFF;
                *((bits)+screen->format->Rshift/8) = nr;
                *((bits)+screen->format->Gshift/8) = ng;
                *((bits)+screen->format->Bshift/8) = nb;
            }
		}
		break;
        case 32:
		{
			Uint32 *mem=((Uint32 *)screen->pixels)+y*(screen->pitch>>2)+x;
			Uint8 dr, dg, db;
			Uint8 na=255-a;
			SDL_GetRGB(*mem, screen->format, &dr, &dg, &db);
			r=(a*r+na*dr)>>8;
			g=(a*g+na*dg)>>8;
			b=(a*b+na*db)>>8;
            *mem = (Uint32)SDL_MapRGB(screen->format, r, g, b);
		}
        break;
    }
}

void SDLGraphicContext::drawRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	drawHorzLine(x, y, w, r, g, b, a);
	drawHorzLine(x, y+h-1, w, r, g, b, a);
	drawVertLine(x, y, h, r, g, b, a);
	drawVertLine(x+w-1, y, h, r, g, b, a);
}

void SDLGraphicContext::drawFilledRect(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	SDL_Rect rect;
	rect.x=x;
	rect.y=y;
	rect.w=w;
	rect.h=h;
	SDL_FillRect(screen, &rect, SDL_MapRGB(screen->format, r, g, b));
}

void SDLGraphicContext::drawVertLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	Uint32 pixel;

	// clip on x
	if ((x<clipRect.x) || (x >= (clipRect.x+clipRect.w)))
		return;

	// set l positiv
	if (l<0)
	{
		y+=l;
		l=-l;
	}

	// clip on y at top
	if (y<clipRect.y)
	{
		l-=(clipRect.y-y);
		y=clipRect.y;
	}

	// clip on y at bottom
	if ((y+l) >= (clipRect.y+clipRect.h))
	{
		l=clipRect.y+clipRect.h-y;
	}

	// ignore wrong case
	if (l<=0)
		return;

    pixel = SDL_MapRGB(screen->format, r, g, b);

	/* Set the pixels */
    switch(screen->format->BitsPerPixel)
	{
        case 8:
		{
			Uint8 *bits = ((Uint8 *)screen->pixels)+y*screen->pitch+x;
			for (int n=l-1; n>=0; --n)
			{
				*bits = (Uint8)pixel;
				bits+=screen->pitch;
			}
		}
		break;
        case 16:
		{
			int increment=(screen->pitch>>1);
			Uint16 *bits = ((Uint16 *)screen->pixels)+y*increment+x;
			for (int n=l-1; n>=0; --n)
			{
                *bits = (Uint16)pixel;
				bits+=increment;
			}
		}
		break;
        case 24:
		{
			 /* Format/endian independent */
			Uint8 *bits = ((Uint8 *)screen->pixels)+y*screen->pitch+x*3;
            Uint8 nr, ng, nb;
			for (int n=l-1; n>=0; --n)
			{
                nr = (pixel>>screen->format->Rshift)&0xFF;
                ng = (pixel>>screen->format->Gshift)&0xFF;
                nb = (pixel>>screen->format->Bshift)&0xFF;
                *((bits)+screen->format->Rshift/8) = nr;
                *((bits)+screen->format->Gshift/8) = ng;
                *((bits)+screen->format->Bshift/8) = nb;
				bits+=screen->pitch;
            }
		}
		break;
        case 32:
		{
			int increment=(screen->pitch>>2);
			Uint32 *bits = ((Uint32 *)screen->pixels)+y*increment+x;
			for (int n=l-1; n>=0; --n)
			{
                *((Uint32 *)(bits)) = (Uint32)pixel;
				bits+=increment;
			}
		}
		break;
		default:
			break;
    }
}

void SDLGraphicContext::drawHorzLine(int x, int y, int l, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	Uint32 pixel;

	// clip on y
	if ((y<clipRect.y) || (y >= (clipRect.y+clipRect.h)))
		return;

	// set l positiv
	if (l<0)
	{
		x+=l;
		l=-l;
	}

	// clip on x at left
	if (x<clipRect.x)
	{
		l-=(clipRect.x-x);
		x=clipRect.x;
	}

	// clip on x at right
	if ((x+l) >= (clipRect.x+clipRect.w))
	{
		l=clipRect.x+clipRect.w-x;
	}

	// ignore wrong case
	if (l<=0)
		return;

    pixel = SDL_MapRGB(screen->format, r, g, b);

    /* Set the pixels */
    switch(screen->format->BitsPerPixel)
	{
        case 8:
		{
			Uint8 *bits= ((Uint8 *)screen->pixels)+y*screen->pitch+x;
			for (int n=l-1; n>=0; --n)
			{
                *bits++ = (Uint8)pixel;
			}
		}
		break;
        case 16:
		{
			Uint16 *bits= ((Uint16 *)screen->pixels)+y*(screen->pitch>>1)+x;
			for (int n=l-1; n>=0; --n)
			{
                *bits++ = (Uint16)pixel;
			}
		}
		break;
        case 24:
		{
			 /* Format/endian independent */
            Uint8 nr, ng, nb;
			Uint8 *bits= ((Uint8 *)screen->pixels)+y*screen->pitch+x*3;
			for (int n=l-1; n>=0; --n)
			{
                nr = (pixel>>screen->format->Rshift)&0xFF;
                ng = (pixel>>screen->format->Gshift)&0xFF;
                nb = (pixel>>screen->format->Bshift)&0xFF;
                *((bits)+screen->format->Rshift/8) = nr;
                *((bits)+screen->format->Gshift/8) = ng;
                *((bits)+screen->format->Bshift/8) = nb;
				bits+=3;
            }
		}
		break;
        case 32:
		{
			Uint32 *bits= ((Uint32 *)screen->pixels)+y*(screen->pitch>>2)+x;
			for (int n=l-1; n>=0; --n)
			{
                *bits++ = (Uint32)pixel;
			}

		}
		break;
		default:
			break;
    }
}

void SDLGraphicContext::drawLine(int x1, int y1, int x2, int y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	// from leto/calodox. 1999, Bresenham anti-aliased line code
	const int FIXED=8;
	const int I=255; /* nombre de degres different d'importance ligne/fond */
	const int Ibits=8;
	#define Swap(a,b) swap=a; a=b; b=swap;
	#define Abs(x) (x>0 ? x : -x)
	#define Sgn(x) (x>0 ? (x == 0 ? 0 : 1) : (x==0 ? 0 : -1))
    long dx, dy;
    long m,w;
    long e;
	Sint32 littleincx;
	Sint32 littleincy;
	Sint32 bigincx;
	Sint32 bigincy;
	Sint32 alphadecx;
	Sint32 alphadecy;

    long swap;
    int test=1;
	int x;

    /* calcul des deltas */
    dx= x2 - x1;
    if ( dx==0)
	{
		drawVertLine(x1,y1,y2-y1,r,g,b,a);
		return;
    }
    dy= y2 - y1;
    if ( dy==0)
	{
		drawHorzLine(x1,y1,x2-x1,r,g,b,a);
		return;
    }

    /* Y clipping */
    if (dy<0)
	{
		test = -test;
		Swap(x1,x2);
		Swap(y1,y2);
		dx=-dx;
		dy=-dy;
	}
    /* the 2 points are Y-sorted. (y1<=y2) */
    if (y2 < clipRect.y)
		return;
    if (y1 >= clipRect.y+clipRect.h)
		return;
    if (y1 < clipRect.y)
	{
		x1=x2-( (y2-clipRect.y)*(x2-x1) ) / (y2-y1);
		y1=clipRect.y;
    }
	if (y1==y2)
	{
		drawHorzLine(x1,y1,x2-x1,r,g,b,a);
		return;
    }
    if (y2 >= clipRect.y+clipRect.h)
	{
		x2=x1-( (y1-(clipRect.y+clipRect.h))*(x1-x2) ) / (y1-y2);
		y2=(clipRect.y+clipRect.h-1);
    }
	if ( x1==x2)
	{
		drawVertLine(x1,y1,y2-y1,r,g,b,a);
		return;
    }

	/* X clipping */
    if (dx<0)
	{
		test = -test;
		Swap(x1,x2);
		Swap(y1,y2);
		dx=-dx;
		dy=-dy;
    }
    /* the 2 points are X-sorted. (x1<=x2) */
    if (x2 < clipRect.x)
		return;
    if (x1 >= clipRect.x+clipRect.w)
		return;
    if (x1 < clipRect.x)
	{
		y1=y2-( (x2-clipRect.x)*(y2-y1) ) / (x2-x1);
		x1=clipRect.x;
    }
	if ( x1==x2)
	{
		drawVertLine(x1,y1,y2-y1,r,g,b,a);
		return;
    }
    if (x2 >= clipRect.x+clipRect.w)
	{
		y2=y1-( (x1-(clipRect.x+clipRect.w))*(y1-y2) ) / (x1-x2);
		x2=(clipRect.x+clipRect.w-1);
    }

	// last return case
	if (x1>=(clipRect.x+clipRect.w) || y1>=(clipRect.y+clipRect.h) || (x2<clipRect.x) || (y2<clipRect.y))
		return;

    dx = x2-x1;
    dy = y2-y1;
	/* prepare les variables pour dessiner la ligne
       dans la bonne direction */

    if (Abs(dx) > Abs(dy))
	{
		littleincx = 1;
		littleincy = 0;
		bigincx = 1;
		bigincy = Sgn(dy);
		alphadecx = 0;
		alphadecy = Sgn(dy);
    }
	else
	{
		test = -test;
		Swap(dx,dy);
		littleincx = 0;
		littleincy = 1;
		bigincx = Sgn(dx);
		bigincy = 1;
		alphadecx = 1;
		alphadecy = 0;
    }

    if (dx<0)
	{
		dx= -dx;
		littleincx=0;
		littleincy=-littleincy;
		bigincx =-bigincx;
		bigincy =-bigincy;
		alphadecy=-alphadecy;
    }

    /* calcul de la position initiale */
    int px,py;
	px=x1;
	py=y1;

	/* initialisation des variables pour l'algo de bresenham */
    if (dx==0)
		return;
	if (dy==0)
		return;
    m = (Abs(dy)<< (Ibits+FIXED)) / Abs(dx);
    w = (I <<FIXED)-m;
    e = 1<<(FIXED-1);

    /* premier point */
	drawPixel(px,py,r,g,b,(Uint8)(I-(e>>FIXED)));

    /* main loop */
    x=dx+1;
    if (x<=0)
		return;

	while (--x)
	{
		if (e < w)
		{
			px+=littleincx;
			py+=littleincy;
			e+= m;
		}
		else
		{
			px+=bigincx;
			py+=bigincy;
			e-= w;
		}
		if (((clipRect.y+clipRect.h)-y2)>1)
			drawPixel(px,py,r,g,b,(Uint8)(I-(e>>FIXED)));
		drawPixel(px+alphadecx,py+alphadecy,r,g,b,(Uint8)(e>>FIXED));
	}
}

void SDLGraphicContext::drawCircle(int x, int y, int ray, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	#ifndef M_PI
	#define M_PI 3.1415927
	#endif
	float pos=0.0f;
	float inc=(2*M_PI)/(float)ray;
	float lpx=x+(float)ray;
	float lpy=y;
	float px, py;

	for (int i=0; i<ray; i++)
	{
		pos+=inc;
		px=x+cos(pos)*(float)ray;
		py=y+sin(pos)*(float)ray;
		drawLine((int)lpx, (int)lpy, (int)px, (int)py, r, g, b, a);
		lpx=px;
		lpy=py;
	}
}

void SDLGraphicContext::drawString(int x, int y, const Font *font, const char *msg, ...)
{
	va_list arglist;
	char output[1024];
	
	va_start(arglist, msg);
	vsprintf(output,  msg, arglist);
	va_end(arglist);

	((const SDLFont *)font)->drawString(screen, x, y, output, &clipRect);
}

void SDLGraphicContext::nextFrame(void)
{
	SDL_Flip(screen);
}

SDLOffScreenGraphicContext::SDLOffScreenGraphicContext(int w, int h, bool usePerPixelAlpha, Uint8 alphaValue)
{
	SDL_Surface *tempScreen=SDL_CreateRGBSurface(0, w, h, 32, 0, 0, 0, 0);

	SDL_SetAlpha(tempScreen, SDL_SRCALPHA, alphaValue);
	if (usePerPixelAlpha)
		screen=SDL_DisplayFormatAlpha(tempScreen);
	else
		screen=SDL_DisplayFormat(tempScreen);
	SDL_FreeSurface(tempScreen);

	setClipRect(NULL);
}
