/*(LGPL)
------------------------------------------------------------
	glSDL 0.7 - SDL 2D API on top of OpenGL
------------------------------------------------------------
 * (c) David Olofson, 2001-2004
 * (c) Bram Thijssen, 2004 (glSDL_SetLineWidth, glSDL_SetLineStipple, 
                            glSDL_DrawLine, glSDL_DrawRect, glSDL_SetFillAlpha).
 * This code is released under the terms of the GNU LGPL.
 */

#define	_GLSDL_NO_REDEFINES_
#include "glSDL.h"

#ifndef M_PI
#define M_PI 3.1415926535
#endif

#ifdef HAVE_OPENGL

#define LEAK_TRACKING

#define	DBG(x) x		/*error messages, warnings*/
#define	DBG2(x)		/*texture allocation*/
#define	DBG3(x)		/*chopping/tiling*/
#define	DBG4(x)		/*texture uploading*/
#define	DBG5(x) 	/*two-way chopping/tiling*/
#define	DBG6(x) 	/*OpenGL lib loading*/

/*#define	CKSTATS*/	/*colorkey statistics*/

/* Keep this on for now! Makes large surfaces faster. */
#define	FAKE_MAXTEXSIZE	256

#include <string.h>
#include <stdlib.h>
#include <math.h>

#if HAS_SDL_OPENGL_H
#include "SDL_opengl.h"
#else
#ifdef WIN32
#include <windows.h>
#endif
#if defined(__APPLE__) && defined(__MACH__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

/*----------------------------------------------------------
	OpenGL interface
----------------------------------------------------------*/

static struct
{
	void	(APIENTRY*Begin)(GLenum);
	void	(APIENTRY*BindTexture)(GLenum, GLuint);
	void	(APIENTRY*BlendFunc)(GLenum, GLenum);
	void	(APIENTRY*Color4ub)(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
	void	(APIENTRY*Color3ub)(GLubyte red, GLubyte green, GLubyte blue);
	void	(APIENTRY*DeleteTextures)(GLsizei n, const GLuint *textures);
	void	(APIENTRY*Disable)(GLenum cap);
	void	(APIENTRY*Enable)(GLenum cap);
	void	(APIENTRY*End)(void);
	void	(APIENTRY*Flush)(void);
	void	(APIENTRY*GenTextures)(GLsizei n, GLuint *textures);
	GLenum	(APIENTRY*GetError)(void);
	void	(APIENTRY*GetIntegerv)(GLenum pname, GLint *params);
	void	(APIENTRY*LoadIdentity)(void);
	void	(APIENTRY*MatrixMode)(GLenum mode);
	void	(APIENTRY*Ortho)(GLdouble left, GLdouble right, GLdouble bottom,
			GLdouble top, GLdouble zNear, GLdouble zFar);
	void	(APIENTRY*PixelStorei)(GLenum pname, GLint param);
	void	(APIENTRY*ReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height,
			GLenum format, GLenum type, GLvoid *pixels);
	void	(APIENTRY*TexCoord2f)(GLfloat s, GLfloat t);
	void	(APIENTRY*TexImage2D)(GLenum target, GLint level, GLint internalformat,
			GLsizei width, GLsizei height, GLint border,
			GLenum format, GLenum type, const GLvoid *pixels);
	void	(APIENTRY*TexParameteri)(GLenum target, GLenum pname, GLint param);
	void	(APIENTRY*TexSubImage2D)(GLenum target, GLint level,
			GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
			GLenum format, GLenum type, const GLvoid *pixels);
	void	(APIENTRY*Translatef)(GLfloat x, GLfloat y, GLfloat z);
	void	(APIENTRY*Vertex2i)(GLint x, GLint y);
	void	(APIENTRY*Vertex2d)(GLdouble x, GLdouble y);
	void	(APIENTRY*Viewport)(GLint x, GLint y, GLsizei width, GLsizei height);

	void	(APIENTRY*Rotated)(GLdouble, GLdouble, GLdouble, GLdouble);
	void	(APIENTRY*Scalef)(GLfloat, GLfloat, GLfloat);
	void	(APIENTRY*PushMatrix)(void);
	void	(APIENTRY*PopMatrix)(void);
	
	void	(APIENTRY*LineWidth)(GLfloat w);
	void	(APIENTRY*LineStipple)(GLint factor, GLuint pattern);
} gl;


static int GetGL(void)
{
	int i;
	struct
	{
		const char	*name;
		void		**fn;
	} glfuncs[] = {
		{"glBegin", (void *)&gl.Begin },
		{"glBindTexture", (void *)&gl.BindTexture },
		{"glBlendFunc", (void *)&gl.BlendFunc },
		{"glColor4ub", (void *)&gl.Color4ub },
		{"glColor3ub", (void *)&gl.Color3ub },
		{"glDeleteTextures", (void *)&gl.DeleteTextures },
		{"glDisable", (void *)&gl.Disable },
		{"glEnable", (void *)&gl.Enable },
		{"glEnd", (void *)&gl.End },
		{"glFlush", (void *)&gl.Flush },
		{"glGenTextures", (void *)&gl.GenTextures },
		{"glGetError", (void *)&gl.GetError },
		{"glGetIntegerv", (void *)&gl.GetIntegerv },
		{"glLoadIdentity", (void *)&gl.LoadIdentity },
		{"glMatrixMode", (void *)&gl.MatrixMode },
		{"glOrtho", (void *)&gl.Ortho },
		{"glPixelStorei", (void *)&gl.PixelStorei },
		{"glReadPixels", (void *)&gl.ReadPixels },
		{"glTexCoord2f", (void *)&gl.TexCoord2f },
		{"glTexImage2D", (void *)&gl.TexImage2D },
		{"glTexParameteri", (void *)&gl.TexParameteri },
		{"glTexSubImage2D", (void *)&gl.TexSubImage2D },
		{"glTranslatef", (void *)&gl.Translatef },
		{"glVertex2i", (void *)&gl.Vertex2i },
		{"glVertex2d", (void *)&gl.Vertex2d },
		{"glViewport", (void *)&gl.Viewport },

		{"glRotated", (void *)&gl.Rotated },
		{"glScalef", (void *)&gl.Scalef },
		{"glPushMatrix", (void *)&gl.PushMatrix },
		{"glPopMatrix", (void *)&gl.PopMatrix },
		
		{"glLineWidth", (void *)&gl.LineWidth },
		{"glLineStipple", (void *)&gl.LineStipple },
		{NULL, NULL }
	};
	for(i = 0; glfuncs[i].name; ++i)
	{
		*glfuncs[i].fn = SDL_GL_GetProcAddress(glfuncs[i].name);
		if(!*glfuncs[i].fn)
		{
			DBG6(fprintf(stderr,"glSDL/wrapper ERROR: Could not get "
					"OpenGL function '%s'!\n",
					glfuncs[i].name);)
			return -1;
		}
		DBG6(printf("Got OpenGL function '%s'.\n", glfuncs[i].name);)
	}
	return 0;
}

/*
void glSDL_glPushMatrix()
{
	gl.PushMatrix();
}
void glSDL_glPopMatrix()
{
	gl.PopMatrix();
}
void glSDL_glRotate(int angle, int x, int y, int z)
{
	gl.Rotate(angle, x, y, z);
}
*/
static int LoadGL(void)
{
	if(GetGL() < 0)
	{
		DBG6(printf("Couldn't get GL funcs! Trying to load lib...\n");)
		SDL_GL_LoadLibrary(NULL);
		if(GetGL() < 0)
		{
			DBG6(printf("Still couldn't get GL funcs!\n");)
			return -1;
		}
	}
	return 0;
}


static void UnloadGL(void)
{
}


static void print_glerror(int point)
{
#if (DBG(1)+0 == 1)
	const char *err = "<unknown>";
	switch(gl.GetError())
	{
	  case GL_NO_ERROR:
		return;
	  case GL_INVALID_ENUM:
		err = "GL_INVALID_ENUM";
		break;
	  case GL_INVALID_VALUE:
		err = "GL_INVALID_VALUE";
		break;
	  case GL_INVALID_OPERATION:
		err = "GL_INVALID_OPERATION";
		break;
	  case GL_STACK_OVERFLOW:
		err = "GL_STACK_OVERFLOW";
		break;
	  case GL_STACK_UNDERFLOW:
		err = "GL_STACK_UNDERFLOW";
		break;
	  case GL_OUT_OF_MEMORY:
		err = "GL_OUT_OF_MEMORY";
		break;
	  default:
		err = "<unknown>";
		break;
	}
	fprintf(stderr,"OpenGL error \"%s\" at point %d.\n", err, point);
#endif
}


static struct
{
	int	do_blend;
	int	do_texture;
	GLint	texture;
	GLenum	sfactor, dfactor;
} glstate;

static void gl_reset(void)
{
	glstate.do_blend = -1;
	glstate.do_blend = -1;
	glstate.texture = -1;
	glstate.sfactor = 0xffffffff;
	glstate.dfactor = 0xffffffff;
}

static __inline__ void gl_do_blend(int on)
{
	if(glstate.do_blend == on)
		return;

	if(on)
		gl.Enable(GL_BLEND);
	else
		gl.Disable(GL_BLEND);
	glstate.do_blend = on;
}

static __inline__ void gl_do_texture(int on)
{
	if(glstate.do_texture == on)
		return;

	if(on)
		gl.Enable(GL_TEXTURE_2D);
	else
		gl.Disable(GL_TEXTURE_2D);
	glstate.do_texture = on;
}

static __inline__ void gl_blendfunc(GLenum sfactor, GLenum dfactor)
{
	if((sfactor == glstate.sfactor) && (dfactor == glstate.dfactor))
		return;

	gl.BlendFunc(sfactor, dfactor);

	glstate.sfactor = sfactor;
	glstate.dfactor = dfactor;
}

static __inline__ void gl_texture(GLuint tx)
{
	if(tx == glstate.texture)
		return;

	gl.BindTexture(GL_TEXTURE_2D, tx);
	glstate.texture = tx;
}


/*----------------------------------------------------------
	Global stuff
----------------------------------------------------------*/

static int using_glsdl = 0;
#define	USING_GLSDL	(0 != using_glsdl)

#define	MAX_TEXINFOS	16384

static glSDL_TexInfo **texinfotab = NULL;
static GLint maxtexsize = 256;
static SDL_PixelFormat RGBfmt, RGBAfmt;

static void UnloadTexture(glSDL_TexInfo *txi);

static int scale = 1;

static SDL_Surface *fake_screen = NULL;


static int glSDL_BlitGL(SDL_Surface *src, SDL_Rect *srcrect,
			 SDL_Surface *dst, SDL_Rect *dstrect);


/* Get texinfo for a surface. */
static __inline__ glSDL_TexInfo *glSDL_GetTexInfo(SDL_Surface *surface)
{
	if(texinfotab)
		return texinfotab[surface->unused1];
	else
		return NULL;
}


/* Allocate a "blank" texinfo for a suface. */
glSDL_TexInfo *glSDL_AllocTexInfo(SDL_Surface *surface)
{
	int handle, i = 0;
	glSDL_TexInfo *txi;
	if(!surface)
		return NULL;

	txi = glSDL_GetTexInfo(surface);
	if(txi)
		return txi;		/* There already is one! --> */

	/* Find a free handle... */
	handle = -1;
	for(i = 1; i < MAX_TEXINFOS + 1; ++i)
		if(NULL == texinfotab[i])
		{
			handle = i;
			break;
		}

	if(handle < 0)
	{
		DBG(fprintf(stderr, "glSDL/wrapper: Out of handles!\n"));
		return NULL;
	}

	/* ...and hook a new texinfo struct up to it. */
	texinfotab[handle] = calloc(1, sizeof(glSDL_TexInfo));
	if(!texinfotab[handle])
		return NULL;

	/* Connect the surface to the new TexInfo. */
	surface->unused1 = (Uint32)handle;

	DBG2(fprintf(stderr, "glSDL/wrapper: Allocated TexInfo %d.\n", handle));

	return texinfotab[handle];
}


static void FreeTexInfo(Uint32 handle)
{
	if(handle >= MAX_TEXINFOS)
		return;
	if(!texinfotab[handle])
		return;

	UnloadTexture(texinfotab[handle]);
	texinfotab[handle]->textures = 0;
	free(texinfotab[handle]->texture);
	texinfotab[handle]->texture = NULL;
	free(texinfotab[handle]);
	texinfotab[handle] = NULL;
	DBG2(fprintf(stderr, "glSDL/wrapper: Freed TexInfo %d.\n", handle));
}


/* Detach and free the texinfo of a surface. */
void glSDL_FreeTexInfo(SDL_Surface *surface)
{
	if(!texinfotab)
		return;

	if(!surface)
		return;

	if(!glSDL_GetTexInfo(surface))
		return;

	FreeTexInfo(surface->unused1);
	GLSDL_FIX_SURFACE(surface);
}


/*
 * Calculate chopping/tiling of a surface to
 * fit it into the smallest possible OpenGL
 * texture.
 */
static int CalcChop(SDL_Surface *s, glSDL_TexInfo *txi)
{
	int rows, vw, vh;
	int vertical = 0;
	int texsize;
	int lastw, lasth, minsize;

	vw = s->w;
	vh = s->h;

	DBG3(fprintf(stderr, "w=%d, h=%d ", vw, vh));
	if(vh > vw)
	{
		int t = vw;
		vw = vh;
		vh = t;
		vertical = 1;
		DBG3(fprintf(stderr, "(vertical) \t"));
	}

	/*
	 * Check whether this is a "huge" surface - at least one dimension
	 * must be <= than the maximum texture size, or we'll have to chop
	 * in both directions.
	 */
	if(vh > maxtexsize)
	{
		/*
		 * Very simple hack for now; we just tile
		 * both ways with maximum size textures.
		 */
		texsize = maxtexsize;

		txi->tilemode = GLSDL_TM_HUGE;
		txi->texsize = texsize;
		txi->tilew = texsize;
		txi->tileh = texsize;
		txi->tilespertex = 1;

		/* Calculate number of textures needed */
		txi->textures = (vw + texsize - 1) / texsize;
		txi->textures *= (vh + texsize - 1) / texsize;
		txi->texture = malloc(txi->textures * sizeof(int));
		memset(txi->texture, -1, txi->textures * sizeof(int));
		DBG5(fprintf(stderr, "two-way tiling; textures=%d\n", txi->textures));
		if(!txi->texture)
		{
			fprintf(stderr, "glSDL/wrapper: INTERNAL ERROR: Failed to allocate"
					" texture name table!\n");
			return -3;
		}
		return 0;
	}

	/* Calculate minimum size */
	rows = 1;
	lastw = vw;
	lasth = vh;
	minsize = lastw > lasth ? lastw : lasth;
	while(1)
	{
		int w, h, size;
		++rows;
		w = vw / rows;
		h = rows * vh;
		size = w > h ? w : h;
		if(size >= minsize)
		{
			--rows;
			break;
		}
		lastw = w;
		lasth = h;
		minsize = size;
	}
	if(minsize > maxtexsize)
	{
		/* Handle multiple textures for very wide/tall surfaces. */
		minsize = maxtexsize;
		rows = (vw + minsize-1) / minsize;
	}
	DBG3(fprintf(stderr, "==> minsize=%d ", minsize));
	DBG3(fprintf(stderr, "(rows=%d) \t", rows));

	/* Recalculate with nearest higher power-of-2 width. */
	for(texsize = 1; texsize < minsize; texsize <<= 1)
		;
	txi->texsize = texsize;
	rows = (vw + texsize-1) / texsize;
	DBG3(fprintf(stderr, "==> texsize=%d (rows=%d) \t", texsize, rows));

	/* Calculate number of tiles per texture */
	txi->tilespertex = txi->texsize / vh;
	DBG3(fprintf(stderr, "tilespertex=%d \t", txi->tilespertex));

	/* Calculate number of textures needed */
	txi->textures = (rows + txi->tilespertex-1) / txi->tilespertex;
	txi->texture = malloc(txi->textures * sizeof(int));
	memset(txi->texture, -1, txi->textures * sizeof(int));
	DBG3(fprintf(stderr, "textures=%d, ", txi->textures));
	if(!txi->texture)
	{
		fprintf(stderr, "glSDL/wrapper: INTERNAL ERROR: Failed to allocate"
				" texture name table!\n");
		return -2;
	}

	/* Set up tile size. (Only one axis supported here!) */
	if(1 == rows)
	{
		txi->tilemode = GLSDL_TM_SINGLE;
		if(vertical)
		{
			txi->tilew = vh;
			txi->tileh = vw;
		}
		else
		{
			txi->tilew = vw;
			txi->tileh = vh;
		}
	}
	else if(vertical)
	{
		txi->tilemode = GLSDL_TM_VERTICAL;
		txi->tilew = vh;
		txi->tileh = texsize;
	}
	else
	{
		txi->tilemode = GLSDL_TM_HORIZONTAL;
		txi->tilew = texsize;
		txi->tileh = vh;
	}

	DBG3(fprintf(stderr, "tilew=%d, tileh=%d\n", txi->tilew, txi->tileh));
	return 0;
}


/* Add a glSDL_TexInfo struct to an SDL_Surface */
static int glSDL_AddTexInfo(SDL_Surface *surface)
{
	glSDL_TexInfo *txi;

	if(!surface)
		return -1;
	if(IS_GLSDL_SURFACE(surface))
		return 0;	/* Do nothing */

	glSDL_AllocTexInfo(surface);
	txi = glSDL_GetTexInfo(surface);
	if(!txi)
		return -2;	/* Oops! Didn't get a texinfo... --> */

	if(CalcChop(surface, txi) < 0)
		return -3;

	SDL_SetClipRect(surface, NULL);

	return 0;
}


/* Create a surface of the prefered OpenGL RGB texture format */
static SDL_Surface *CreateRGBSurface(int w, int h)
{
	SDL_Surface *s;
	Uint32 rmask, gmask, bmask;
	int bits = 24;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	rmask = 0x00ff0000;
	gmask = 0x0000ff00;
	bmask = 0x000000ff;
#else
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
#endif
	s = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h,
			bits, rmask, gmask, bmask, 0);
	if(s)
		GLSDL_FIX_SURFACE(s);

	glSDL_AddTexInfo(s);
	return s;
}


/* Create a surface of the prefered OpenGL RGBA texture format */
static SDL_Surface *CreateRGBASurface(int w, int h)
{
	SDL_Surface *s;
	Uint32 rmask, gmask, bmask, amask;
	int bits = 32;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	rmask = 0xff000000;
	gmask = 0x00ff0000;
	bmask = 0x0000ff00;
	amask = 0x000000ff;
#else
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = 0xff000000;
#endif
	s = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h,
			bits, rmask, gmask, bmask, amask);
	if(s)
		GLSDL_FIX_SURFACE(s);

	glSDL_AddTexInfo(s);
	return s;
}


static void init_formats(void)
{
	SDL_Surface *s = CreateRGBSurface(1, 1);
	if(!s)
		return;
	RGBfmt = *(s->format);
	glSDL_FreeSurface(s);

	s = CreateRGBASurface(1, 1);
	if(!s)
		return;
	RGBAfmt = *(s->format);
	glSDL_FreeSurface(s);
}


static int FormatIsOk(SDL_Surface *surface)
{
	SDL_PixelFormat *pf;
	if(!surface)
		return 1;	/* Well, there ain't much we can do anyway... */

	pf = surface->format;

	/* Colorkeying requires an alpha channel! */
	if(surface->flags & SDL_SRCCOLORKEY)
		if(!pf->Amask)
			return 0;

	/* We need pitch == (width * BytesPerPixel) for glTex[Sub]Image2D() */
	if(surface->pitch != (surface->w * pf->BytesPerPixel))
		return 0;

	if(pf->Amask)
	{
		if(pf->BytesPerPixel != RGBAfmt.BytesPerPixel)
			return 0;
		if(pf->Rmask != RGBAfmt.Rmask)
			return 0;
		if(pf->Gmask != RGBAfmt.Gmask)
			return 0;
		if(pf->Bmask != RGBAfmt.Bmask)
			return 0;
		if(pf->Amask != RGBAfmt.Amask)
			return 0;
	}
	else
	{
		if(pf->BytesPerPixel != RGBfmt.BytesPerPixel)
			return 0;
		if(pf->Rmask != RGBfmt.Rmask)
			return 0;
		if(pf->Gmask != RGBfmt.Gmask)
			return 0;
		if(pf->Bmask != RGBfmt.Bmask)
			return 0;
	}
	return 1;
}



static void key2alpha(SDL_Surface *surface)
{
	int x, y;
#ifdef CKSTATS
	int transp = 0;
#endif
	Uint32 ckey = surface->format->colorkey;
	if(SDL_LockSurface(surface) < 0)
		return;

	for(y = 0; y < surface->h; ++y)
	{
		Uint32 *px = (Uint32 *)((char *)surface->pixels + y*surface->pitch);
		for(x = 0; x < surface->w; ++x)
			if(px[x] == ckey)
			{
				px[x] = 0;
#ifdef CKSTATS
				++transp;
#endif
			}
	}
#ifdef CKSTATS
	printf("glSDL/wrapper: key2alpha(); %dx%d surface, %d opaque pixels.\n",
			surface->w, surface->h,
			surface->w * surface->h - transp);
#endif
	SDL_UnlockSurface(surface);
}



/*----------------------------------------------------------
	SDL style API
----------------------------------------------------------*/

static void KillAllTextures(void)
{
	if(texinfotab)
	{
		unsigned i;
#ifdef LEAK_TRACKING
		int leaked = 0;
		for(i = 3; i < MAX_TEXINFOS + 1; ++i)
			if(texinfotab[i])
			{
				++leaked;
				fprintf(stderr, "glSDL/wrapper: Leaked TexInfo"
						" %d! (%d %dx%d textures)\n",
						i,
						texinfotab[i]->textures,
						texinfotab[i]->texsize,
						texinfotab[i]->texsize
						);
			}
		if(leaked)
			fprintf(stderr, "glSDL/wrapper: Leaked %d TexInfos!\n", leaked);
#endif
		for(i = 1; i < MAX_TEXINFOS + 1; ++i)
			FreeTexInfo(i);
		free(texinfotab);
		texinfotab = NULL;
	}
}

void glSDL_Quit(void)
{
	if(SDL_WasInit(SDL_INIT_VIDEO))
	{
		SDL_Surface *screen = SDL_GetVideoSurface();
		glSDL_FreeTexInfo(screen);
		UnloadGL();
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		if(fake_screen)
		{
			glSDL_FreeTexInfo(fake_screen);
			SDL_FreeSurface(fake_screen);
			fake_screen = NULL;
		}
	}
#ifndef LEAK_TRACKING
	KillAllTextures();
#endif
}


void glSDL_FullQuit(void)
{
#ifdef LEAK_TRACKING
	KillAllTextures();
#endif
	glSDL_Quit();
	SDL_Quit();
}


void glSDL_QuitSubSystem(Uint32 flags)
{
	if(flags & SDL_INIT_VIDEO)
		glSDL_Quit();
	SDL_QuitSubSystem(flags);
}


SDL_Surface *glSDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
	SDL_Surface *screen;
	GLint gl_doublebuf;

	using_glsdl = 0;

	if(!(flags & SDL_GLSDL))
	{
		screen = SDL_SetVideoMode(width, height, bpp, flags);
		if(screen)
			GLSDL_FIX_SURFACE(screen);
		return screen;
	}

	if((SDL_Linked_Version()->major <= 1) &&
			(SDL_Linked_Version()->minor <= 2) &&
			(SDL_Linked_Version()->patch < 5))
		fprintf(stderr, "glSDL/wrapper WARNING: Using SDL version"
				" 1.2.5 or later is strongly"
				" recommended!\n");

	if(LoadGL() < 0)
	{
		fprintf(stderr, "glSDL/wrapper ERROR: Could not load OpenGL library!\n");
		return NULL;
	}

/*
 * FIXME: Here's the place to insert proper handling of this call being
 *        used for resizing the window... For now, just make sure we
 *        don't end up with invalid texinfos and stuff no matter what.
 */
	KillAllTextures();

	texinfotab = calloc(MAX_TEXINFOS + 1, sizeof(glSDL_TexInfo *));
	if(!texinfotab)
		return NULL;

	/* Remove flag to avoid confusion inside SDL - just in case! */
	flags &= ~SDL_GLSDL;

	flags |= SDL_OPENGL;
	if(bpp == 15)
	{
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	}
	else if(bpp == 16)
	{
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
	}
	else if(bpp >= 24)
	{
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	}
	gl_doublebuf = flags & SDL_DOUBLEBUF;
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, gl_doublebuf);

	scale = 1;

	screen = SDL_SetVideoMode(width*scale, height*scale, bpp, flags);
	if(!screen)
	{
		KillAllTextures();
		return NULL;
	}

	GLSDL_FIX_SURFACE(screen);

#ifdef	FAKE_MAXTEXSIZE
	maxtexsize = FAKE_MAXTEXSIZE;
#else
	gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtexsize);
#endif
	DBG(fprintf(stderr, "glSDL/wrapper: Max texture size: %d\n", maxtexsize));

	init_formats();
	gl_reset();

	if(glSDL_AddTexInfo(screen) < 0)
	{
		DBG(fprintf(stderr, "glSDL/wrapper: Failed to add info to screen surface!\n"));
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return NULL;
	}

	glSDL_SetClipRect(screen, &screen->clip_rect);

	gl.Viewport(0, 0, screen->w * scale, screen->h * scale);
	/*
	 * Note that this projection is upside down in
	 * relation to the OpenGL coordinate system.
	 */
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.Ortho(0, scale * (float)screen->w, scale * (float)screen->h, 0,
			-1.0, 1.0);

	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.Translatef(0.0f, 0.0f, 0.0f);

	gl.Disable(GL_DEPTH_TEST);
	gl.Disable(GL_CULL_FACE);

	/*
	 * Create a software shadow buffer of the requested size.
	 * This is used for blit-from-screen and simulation of
	 * direct software rendering. (Dog slow crap. It's only
	 * legitimate use is probably screen shots.)
	 */
	fake_screen = CreateRGBSurface(screen->w / scale,
			screen->h / scale);
	using_glsdl = 1;
	return fake_screen;
}


SDL_Surface *glSDL_GetVideoSurface(void)
{
	if(fake_screen)
		return fake_screen;
	else
		return SDL_GetVideoSurface();
}


void glSDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects)
{
	if(IS_GLSDL_SURFACE(screen))
		glSDL_Flip(screen);
	else
		SDL_UpdateRects(screen, numrects, rects);
}


void glSDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
	SDL_Rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	glSDL_UpdateRects(screen, 1, &r);
}


int glSDL_Flip(SDL_Surface *screen)
{
	if(!IS_GLSDL_SURFACE(screen))
		return SDL_Flip(screen);

	SDL_GL_SwapBuffers();
	return 0;
}


void glSDL_FreeSurface(SDL_Surface *surface)
{
	if(!surface)
		return;
	glSDL_FreeTexInfo(surface);
	SDL_FreeSurface(surface);
}


int glSDL_LockSurface(SDL_Surface *surface)
{
	if(!surface)
		return 0;

	if(IS_GLSDL_SURFACE(surface))
	{
		if((surface == fake_screen) ||
				(SDL_GetVideoSurface() == surface))
		{
			if(scale > 1)
				return -1;

			glSDL_Invalidate(fake_screen, NULL);

			gl.PixelStorei(GL_UNPACK_ROW_LENGTH,
					fake_screen->pitch /
					fake_screen->format->BytesPerPixel);

			gl.ReadPixels(0, 0, fake_screen->w, fake_screen->h,
					GL_RGB, GL_UNSIGNED_BYTE,
					fake_screen->pixels);
			return 0;
		}
		else
		{
			glSDL_Invalidate(surface, NULL);
			return SDL_LockSurface(surface);
		}
	}
	else
		return SDL_LockSurface(surface);
}


void glSDL_UnlockSurface(SDL_Surface *surface)
{
	if(!surface)
		return;

	if(IS_GLSDL_SURFACE(surface))
	{
		glSDL_UploadSurface(surface);
		if((surface == fake_screen) ||
				(SDL_GetVideoSurface() == surface))
			glSDL_BlitGL(fake_screen, NULL,
					SDL_GetVideoSurface(), NULL);
	}
	else
		SDL_UnlockSurface(surface);
}


int glSDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key)
{
	int res = SDL_SetColorKey(surface, flag, key);
	if(res < 0)
		return res;		
	/*
	 * If an application does this *after* SDL_DisplayFormat,
	 * we're basically screwed, unless we want to do an
	 * in-place surface conversion hack here.
	 *
	 * What we do is just kill the glSDL texinfo... No big
	 * deal in most cases, as glSDL only converts once anyway,
	 * *unless* you keep modifying the surface.
	 */
	if(IS_GLSDL_SURFACE(surface))
		glSDL_FreeTexInfo(surface);
	return res;
}


int glSDL_SetAlpha(SDL_Surface *surface, Uint32 flag, Uint8 alpha)
{
	/*
	 * This is just parameters to OpenGL, so the actual
	 * "work" is done in glSDL_BlitSurface().
	 */
	return SDL_SetAlpha(surface, flag, alpha);
}


SDL_bool glSDL_SetClipRect(SDL_Surface *surface, SDL_Rect *rect)
{
	SDL_bool res;
	SDL_Surface *screen;
	SDL_Rect fsr;
	if(!surface)
		return SDL_FALSE;

	screen = SDL_GetVideoSurface();

	res = SDL_SetClipRect(surface, rect);
	if(!res)
		return SDL_FALSE;

	if(!rect)
	{
		fsr.x = 0;
		fsr.y = 0;
		fsr.w = screen->w;
		fsr.h = screen->h;
		rect = &fsr;
	}
	if(surface == fake_screen)
	{
		SDL_Rect r;
		r.x = rect->x;
		r.y = rect->y;
		r.w = rect->w;
		r.h = rect->h;
		surface = screen;
		SDL_SetClipRect(surface, rect);
		return SDL_TRUE;
	}
	return SDL_TRUE;
}


static int glSDL_BlitFromGL(SDL_Rect *srcrect,
		SDL_Surface *dst, SDL_Rect *dstrect)
{
	int i, sy0, dy0;
	SDL_Rect sr, dr;

	if(scale > 1)
		return -1;
/*
FIXME: Some clipping, perhaps...? :-)
*/
	/* In case the destination has an OpenGL texture... */
	glSDL_Invalidate(dst, dstrect);

	/* Abuse the fake screen buffer a little. */
	gl.PixelStorei(GL_UNPACK_ROW_LENGTH, fake_screen->pitch /
			fake_screen->format->BytesPerPixel);
	if(srcrect)
		gl.ReadPixels(srcrect->x, srcrect->y, srcrect->w, srcrect->h,
				GL_RGB, GL_UNSIGNED_BYTE, fake_screen->pixels);
	else
		gl.ReadPixels(0, 0, fake_screen->w, fake_screen->h,
				GL_RGB, GL_UNSIGNED_BYTE, fake_screen->pixels);

	/* Blit to the actual target! (Vert. flip... Uuurgh!) */
	if(srcrect)
		sr = *srcrect;
	else
	{
		sr.x = sr.y = 0;
		sr.w = dst->w;
		srcrect = &sr;
	}

	if(dstrect)
		dr = *dstrect;
	else
	{
		dr.x = dr.y = 0;
		dstrect = &dr;
	}

	i = srcrect->h;
	sy0 = srcrect->y;
	dy0 = dstrect->y + dstrect->h - 1;
	while(i--)
	{
		sr.y = sy0 + i;
		dr.y = dy0 - i;
		sr.h = 1;
		if(SDL_BlitSurface(fake_screen, &sr, dst, &dr) < 0)
			return -1;
	}
	return 0;
}


static __inline__ void BlitGL_single(glSDL_TexInfo *txi,
		float sx1, float sy1, SDL_Rect *dst, unsigned char alpha)
{
	float sx2, sy2, texscale;
	if(!txi->textures)
		return;
	if(-1 == txi->texture[0])
		return;
	gl_texture(txi->texture[0]);

	texscale = 1.0 / (float)txi->texsize;
	sx2 = (sx1 + (float)dst->w) * texscale;
	sy2 = (sy1 + (float)dst->h) * texscale;
	sx1 *= texscale;
	sy1 *= texscale;

	gl.Begin(GL_QUADS);
	gl.Color4ub(255, 255, 255, alpha);
	gl.TexCoord2f(sx1, sy1);
	gl.Vertex2i(dst->x, dst->y);
	gl.TexCoord2f(sx2, sy1);
	gl.Vertex2i(dst->x + dst->w, dst->y);
	gl.TexCoord2f(sx2, sy2);
	gl.Vertex2i(dst->x + dst->w, dst->y + dst->h);
	gl.TexCoord2f(sx1, sy2);
	gl.Vertex2i(dst->x, dst->y + dst->h);
	gl.End();
}


static void BlitGL_htile(glSDL_TexInfo *txi,
		float sx1, float sy1, SDL_Rect *dst, unsigned char alpha)
{
	int tex;
	float tile, sx2, sy2, yo;
	float texscale = 1.0 / (float)txi->texsize;
	float tileh = (float)txi->tileh * texscale;
	sx2 = (sx1 + (float)dst->w) * texscale;
	sy2 = (sy1 + (float)dst->h) * texscale;
	sx1 *= texscale;
	sy1 *= texscale;
	tile = floor(sx1);
	tex = (int)tile / txi->tilespertex;
	yo = ((int)tile % txi->tilespertex) * tileh;

	if(tex >= txi->textures)
		return;
	if(-1 == txi->texture[tex])
		return;
	gl_texture(txi->texture[tex]);

	gl.Begin(GL_QUADS);
	while(tile < sx2)
	{
		int tdx1 = dst->x;
		int tdx2 = dst->x + dst->w;
		float tsx1 = sx1 - tile;
		float tsx2 = sx2 - tile;

		/* Clip to current tile */
		if(tsx1 < 0.0)
		{
			tdx1 -= tsx1 * txi->texsize;
			tsx1 = 0.0;
		}
		if(tsx2 > 1.0)
		{
			tdx2 -= (tsx2 - 1.0) * txi->texsize;
			tsx2 = 1.0;
		}

		/* Maybe select next texture? */
		if(yo + tileh > 1.0)
		{
			++tex;
			gl.End();
			if(tex >= txi->textures)
				return;
			if(-1 == txi->texture[tex])
				return;
			gl_texture(txi->texture[tex]);
			yo = 0.0;
			gl.Begin(GL_QUADS);
		}

		gl.Color4ub(255, 255, 255, alpha);
		gl.TexCoord2f(tsx1, yo + sy1);
		gl.Vertex2i(tdx1, dst->y);
		gl.TexCoord2f(tsx2, yo + sy1);
		gl.Vertex2i(tdx2, dst->y);
		gl.TexCoord2f(tsx2, yo + sy2);
		gl.Vertex2i(tdx2, dst->y + dst->h);
		gl.TexCoord2f(tsx1, yo + sy2);
		gl.Vertex2i(tdx1, dst->y + dst->h);

		tile += 1.0;
		yo += tileh;
	}
	gl.End();
}


static void BlitGL_vtile(glSDL_TexInfo *txi,
		float sx1, float sy1, SDL_Rect *dst, unsigned char alpha)
{
	int tex;
	float tile, sx2, sy2, xo;
	float texscale = 1.0 / (float)txi->texsize;
	float tilew = (float)txi->tilew * texscale;
	sx2 = (sx1 + (float)dst->w) * texscale;
	sy2 = (sy1 + (float)dst->h) * texscale;
	sx1 *= texscale;
	sy1 *= texscale;
	tile = floor(sy1);
	tex = (int)tile / txi->tilespertex;
	xo = ((int)tile % txi->tilespertex) * tilew;

	if(tex >= txi->textures)
		return;
	if(-1 == txi->texture[tex])
		return;
	gl_texture(txi->texture[tex]);

	gl.Begin(GL_QUADS);
	while(tile < sy2)
	{
		int tdy1 = dst->y;
		int tdy2 = dst->y + dst->h;
		float tsy1 = sy1 - tile;
		float tsy2 = sy2 - tile;

		/* Clip to current tile */
		if(tsy1 < 0.0)
		{
			tdy1 -= tsy1 * txi->texsize;
			tsy1 = 0.0;
		}
		if(tsy2 > 1.0)
		{
			tdy2 -= (tsy2 - 1.0) * txi->texsize;
			tsy2 = 1.0;
		}

		/* Maybe select next texture? */
		if(xo + tilew > 1.0)
		{
			++tex;
			gl.End();
			if(tex >= txi->textures)
				return;
			if(-1 == txi->texture[tex])
				return;
			gl_texture(txi->texture[tex]);
			xo = 0.0;
			gl.Begin(GL_QUADS);
		}

		gl.Color4ub(255, 255, 255, alpha);
		gl.TexCoord2f(xo + sx1, tsy1);
		gl.Vertex2i(dst->x, tdy1);
		gl.TexCoord2f(xo + sx2, tsy1);
		gl.Vertex2i(dst->x + dst->w, tdy1);
		gl.TexCoord2f(xo + sx2, tsy2);
		gl.Vertex2i(dst->x + dst->w, tdy2);
		gl.TexCoord2f(xo + sx1, tsy2);
		gl.Vertex2i(dst->x, tdy2);

		tile += 1.0;
		xo += tilew;
	}
	gl.End();
}


static void BlitGL_hvtile(SDL_Surface *src, glSDL_TexInfo *txi,
		float sx1, float sy1, SDL_Rect *dst, unsigned char alpha)
{
	int x, y, last_tex, tex;
	float sx2, sy2;
	float texscale = 1.0 / (float)txi->texsize;
	int tilesperrow = (src->w + txi->tilew - 1) / txi->tilew;
	sx2 = (sx1 + (float)dst->w) * texscale;
	sy2 = (sy1 + (float)dst->h) * texscale;
	sx1 *= texscale;
	sy1 *= texscale;

	last_tex = tex = floor(sy1) * tilesperrow + floor(sx1);
	if(tex >= txi->textures)
		return;
	if(-1 == txi->texture[tex])
		return;
	gl_texture(txi->texture[tex]);

	gl.Begin(GL_QUADS);
	for(y = floor(sy1); y < sy2; ++y)
	{
		int tdy1 = dst->y;
		int tdy2 = dst->y + dst->h;
		float tsy1 = sy1 - y;
		float tsy2 = sy2 - y;

		/* Clip to current tile */
		if(tsy1 < 0.0)
		{
			tdy1 -= tsy1 * txi->texsize;
			tsy1 = 0.0;
		}
		if(tsy2 > 1.0)
		{
			tdy2 -= (tsy2 - 1.0) * txi->texsize;
			tsy2 = 1.0;
		}
		for(x = floor(sx1); x < sx2; ++x)
		{
			int tdx1 = dst->x;
			int tdx2 = dst->x + dst->w;
			float tsx1 = sx1 - x;
			float tsx2 = sx2 - x;

			/* Clip to current tile */
			if(tsx1 < 0.0)
			{
				tdx1 -= tsx1 * txi->texsize;
				tsx1 = 0.0;
			}
			if(tsx2 > 1.0)
			{
				tdx2 -= (tsx2 - 1.0) * txi->texsize;
				tsx2 = 1.0;
			}

			/* Select texture */
			tex = y * tilesperrow + x;
			if(tex != last_tex)
			{
				gl.End();
				if(tex >= txi->textures)
					return;
				if(-1 == txi->texture[tex])
					return;
				gl_texture(txi->texture[tex]);
				last_tex = tex;
				gl.Begin(GL_QUADS);
			}

			gl.Color4ub(255, 255, 255, alpha);
			gl.TexCoord2f(tsx1, tsy1);
			gl.Vertex2i(tdx1, tdy1);
			gl.TexCoord2f(tsx2, tsy1);
			gl.Vertex2i(tdx2, tdy1);
			gl.TexCoord2f(tsx2, tsy2);
			gl.Vertex2i(tdx2, tdy2);
			gl.TexCoord2f(tsx1, tsy2);
			gl.Vertex2i(tdx1, tdy2);
		}
	}
	gl.End();
}


/*
 * Calculate the actual blit rectangle and source offset
 * for a blit from a rectangle in a surface with specified
 * size to a surface with a cliprect.
 *
 * In:	rect	source rectangle
 *	w, h	source surface size
 *	(x, y)	destination coordinate
 *	clip	destination clip rectangle
 *
 * Out:	(x, y)	source top-left offset
 *	rect	destination rectangle
 *
 * Returns 1 if the result is visible, otherwise 0.
 */
static __inline__ int blitclip(SDL_Rect *rect, int w, int h,
		int *x, int *y, SDL_Rect *clip)
{
	int sx1, sy1, sx2, sy2;
	int dx1, dy1, dx2, dy2;

	/* Get source and destination coordinates */
	sx1 = rect->x;
	sy1 = rect->y;
	sx2 = sx1 + rect->w;
	sy2 = sy1 + rect->h;
	dx1 = *x;
	dy1 = *y;

	/* Keep source rect inside source surface */
	if(sx1 < 0)
	{
		dx1 -= sx1;
		sx1 = 0;
	}
	if(sy1 < 0)
	{
		dy1 -= sy1;
		sy1 = 0;
	}
	if(sx2 > w)
		sx2 = w;
	if(sy2 > h)
		sy2 = h;

	/* Cull blits from void space */
	if(sx1 >= sx2 || sy1 >= sy2)
		return 0;

	/* Calculate destination lower-right */
	dx2 = dx1 + (sx2 - sx1);
	dy2 = dy1 + (sy2 - sy1);

	if(clip)
	{
		/* Clip to destination cliprect */
		if(dx1 < clip->x)
		{
			sx1 += clip->x - dx1;
			dx1 = clip->x;
		}
		if(dy1 < clip->y)
		{
			sy1 += clip->y - dy1;
			dy1 = clip->y;
		}
		if(dx2 > clip->x + clip->w)
			dx2 = clip->x + clip->w;
		if(dy2 > clip->y + clip->h)
			dy2 = clip->y + clip->h;
	}

	/* Cull nop/off-screen blits */
	if(dx1 >= dx2 || dy1 >= dy2)
		return 0;

	*x = sx1;
	*y = sy1;
	rect->x = dx1;
	rect->y = dy1;
	rect->w = dx2 - dx1;
	rect->h = dy2 - dy1;
	return 1;
}

static int rotate_angle;
static int rotate_set;
static int rotate_x;
static int rotate_y;
static int rotate_center_set;
static float scale_x;
static float scale_y;
static int scale_set;
static int blt_alpha;
static int blt_alpha_set;
static int viewport_set;

void glSDL_SetRotateAngle(int angle)
{
	rotate_angle = angle;
	rotate_set = 1;
}
void glSDL_SetRotateCenter(int x, int y)
{
	rotate_x = x;
	rotate_y = y;
	rotate_center_set = 1;
}
void glSDL_SetScale(float x, float y)
{
	scale_x = x;
	scale_y = y;
	scale_set = 1;
}
void glSDL_SetBltAlpha(int alpha)
{
	blt_alpha = alpha;
	blt_alpha_set = 1;
}
void glSDL_SetLineWidth(float width)
{
	gl.LineWidth(width);
}
void glSDL_SetLineStipple(int factor, Uint16 pattern)
{
	if (factor == 0)
		gl.Disable(GL_LINE_STIPPLE);
	else
	{
		gl.Enable(GL_LINE_STIPPLE);
		glLineStipple(factor, pattern);
	}
}
void glSDL_ResetRotateAngle()
{
	rotate_set = 0;
}
void glSDL_ResetRotateCenter()
{
	rotate_center_set = 0;
}
void glSDL_ResetScale()
{
	scale_set = 0;
}
void glSDL_ResetBltAlpha()
{
	blt_alpha_set = 0;
}
void glSDL_ResetParameters()
{
	glSDL_ResetRotateAngle();
	glSDL_ResetRotateCenter();
	glSDL_ResetScale();
	glSDL_ResetBltAlpha();
}

static int glSDL_BlitGL(SDL_Surface *src, SDL_Rect *srcrect,
			 SDL_Surface *dst, SDL_Rect *dstrect)
{
	glSDL_TexInfo *txi;
	SDL_Rect r;
	int x, y;
	int xc, yc;
	unsigned char alpha;
	int clip_vis;
	if(!src || !dst)
		return -1;

	/* Get source and destination coordinates */
	if(srcrect)
		r = *srcrect;
	else
	{
		r.x = r.y = 0;
		r.w = src->w;
		r.h = src->h;
	}
	if(dstrect)
	{
		x = dstrect->x;
		y = dstrect->y;
	}
	else
		x = y = 0;

	/* Clip! */
	if(!(rotate_set | scale_set) && !viewport_set)
		clip_vis = blitclip(&r, src->w, src->h, &x, &y, &dst->clip_rect);
	else
		clip_vis = blitclip(&r, src->w, src->h, &x, &y, NULL);
	if(!clip_vis)
	{
		if(dstrect)
			dstrect->w = dstrect->h = 0;
		return 0;
	}

	/* Write back the resulting cliprect */
	if(dstrect)
		*dstrect = r;

	/* Make sure we have a source with a valid texture */
	glSDL_UploadSurface(src);
	txi = glSDL_GetTexInfo(src);
	if(!txi)
		return -1;

	/* Set up blending */
	if((src->flags & (SDL_SRCALPHA | SDL_SRCCOLORKEY)) | blt_alpha_set)
	{
		gl_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl_do_blend(1);
	}
	else
		gl_do_blend(0);

	/* Enable texturing */
	gl_do_texture(1);

	/*
	 * Note that we actually *prevent* the use of "full surface alpha"
	 * and alpha channel in combination - to stay SDL 2D compatible.
	 */
	if(blt_alpha_set)
		alpha = blt_alpha;
	else
	{
		if((src->flags & SDL_SRCALPHA) &&
				(!src->format->Amask || (src->flags & SDL_SRCCOLORKEY)))
			alpha = src->format->alpha;
		else
			alpha = 255;
	}

	/* Render! */
	if(rotate_set | scale_set)
	{
		if(!rotate_center_set)
		{
			rotate_x = r.w >> 1;
			rotate_y = r.h >> 1;
		}

		xc = r.x + rotate_x;
		yc = r.y + rotate_y;

		gl.PushMatrix();
		gl.Translatef(xc, yc, 0);
		if(rotate_set)
			gl.Rotated(rotate_angle, 0, 0, 1);
		if(scale_set)
			gl.Scalef(scale_x, scale_y, 0);

		r.x = -rotate_x;
		r.y = -rotate_y;

		switch(txi->tilemode)
		{
		  case GLSDL_TM_SINGLE:
			BlitGL_single(txi, x, y, &r, alpha);
			break;
		  case GLSDL_TM_HORIZONTAL:
			BlitGL_htile(txi, x, y, &r, alpha);
			break;
		  case GLSDL_TM_VERTICAL:
			BlitGL_vtile(txi, x, y, &r, alpha);
			break;
		  case GLSDL_TM_HUGE:
			BlitGL_hvtile(src, txi, x, y, &r, alpha);
			break;
		}

		gl.PopMatrix();
	}
	else
	{
		switch(txi->tilemode)
		{
		  case GLSDL_TM_SINGLE:
			BlitGL_single(txi, x, y, &r, alpha);
			break;
		  case GLSDL_TM_HORIZONTAL:
			BlitGL_htile(txi, x, y, &r, alpha);
			break;
		  case GLSDL_TM_VERTICAL:
			BlitGL_vtile(txi, x, y, &r, alpha);
			break;
		  case GLSDL_TM_HUGE:
			BlitGL_hvtile(src, txi, x, y, &r, alpha);
			break;
		}
	}
	glSDL_ResetParameters();
	return 0;
}


int glSDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect,
		SDL_Surface *dst, SDL_Rect *dstrect)
{
	SDL_Surface *vs;
	if(!src || !dst)
		return -1;

	/*
	 * Figure out what to do:
	 *      Not using glSDL:        SDL_BlitSurface()
	 *      screen->screen:         _glSDL_BlitFromGL() + _glSDL_BlitGL()
	 *      surface->screen:        _glSDL_BlitGL()
	 *      screen->surface:        _glSDL_BlitFromGL()
	 *      surface->surface:       SDL_BlitSurface()
	 */
	if(!USING_GLSDL)
		return SDL_BlitSurface(src, srcrect, dst, dstrect);

	vs = SDL_GetVideoSurface();
	if(src == fake_screen)
		src = vs;
	if(dst == fake_screen)
		dst = vs;
	if(src == vs)
	{
		if(dst == vs)
		{
			glSDL_BlitFromGL(srcrect, fake_screen, dstrect);
			return glSDL_BlitGL(fake_screen, srcrect,
					dst, dstrect);
		}
		else
		{
			return glSDL_BlitFromGL(srcrect, dst, dstrect);
		}
	}
	else
	{
		if(dst == vs)
		{
			return glSDL_BlitGL(src, srcrect,
					dst, dstrect);
		}
		else
		{
			glSDL_Invalidate(dst, dstrect);
			return SDL_BlitSurface(src, srcrect, dst, dstrect);
		}
	}
}

int glSDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
	SDL_Surface *vs = SDL_GetVideoSurface();
	SDL_PixelFormat *pf = dst->format;
	int dx1, dy1, dx2, dy2;
	Uint32 a;
	Uint8 r, g, b;
	
	SDL_GetRGB(color, pf, &r, &g, &b);
	#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	a = color & 0x000000ff;
	#else
	a = color & 0xff000000;
	a = a >> 24;
	#endif

	if(dst == fake_screen)
		dst = vs;
	if(vs != dst)
		glSDL_Invalidate(dst, dstrect);
	if((vs != dst) || !USING_GLSDL)
	{
		if (a < 255)
			return 1;
		else
			return SDL_FillRect(dst, dstrect, color);
	}

	if(dstrect)
	{
		dx1 = dstrect->x;
		dy1 = dstrect->y;
		dx2 = dx1 + dstrect->w;
		dy2 = dy1 + dstrect->h;
		if(dx1 < dst->clip_rect.x)
			dx1 = dst->clip_rect.x;
		if(dy1 < dst->clip_rect.y)
			dy1 = dst->clip_rect.y;
		if(dx2 > dst->clip_rect.x + dst->clip_rect.w)
			dx2 = dst->clip_rect.x + dst->clip_rect.w;
		if(dy2 > dst->clip_rect.y + dst->clip_rect.h)
			dy2 = dst->clip_rect.y + dst->clip_rect.h;
		dstrect->x = dx1;
		dstrect->y = dy1;
		dstrect->w = dx2 - dx1;
		dstrect->h = dy2 - dy1;
		if(!dstrect->w || !dstrect->h)
			return 0;
	}
	else
	{
		dx1 = dst->clip_rect.x;
		dy1 = dst->clip_rect.y;
		dx2 = dx1 + dst->clip_rect.w;
		dy2 = dy1 + dst->clip_rect.h;
	}
	
	if (a < 255)
	{
		gl_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl_do_blend(1);
	}
	else
		gl_do_blend(0);
	gl_do_texture(0);

	gl.Begin(GL_QUADS);
	if (a < 255)
		gl.Color4ub(r, g, b, a);
	else
		gl.Color3ub(r, g, b);
	gl.Vertex2i(dx1, dy1); gl.Vertex2i(dx1, dy2);
	gl.Vertex2i(dx1, dy2); gl.Vertex2i(dx2, dy2);
	gl.Vertex2i(dx2, dy2); gl.Vertex2i(dx2, dy1);
	gl.Vertex2i(dx2, dy1); gl.Vertex2i(dx1, dy1);
	gl.End();

	return 0;
}


SDL_Surface *glSDL_DisplayFormat(SDL_Surface *surface)
{
	SDL_Surface *s, *tmp;
	if(USING_GLSDL)
	{
		int use_rgba = (surface->flags & SDL_SRCCOLORKEY) ||
				((surface->flags & SDL_SRCALPHA) &&
				surface->format->Amask);
		if(use_rgba)
			tmp = SDL_ConvertSurface(surface, &RGBAfmt, SDL_SWSURFACE);
		else
			tmp = SDL_ConvertSurface(surface, &RGBfmt, SDL_SWSURFACE);
		if(!tmp)
			return NULL;
		GLSDL_FIX_SURFACE(tmp);
		SDL_SetAlpha(tmp, 0, 0);

		if(surface->flags & SDL_SRCCOLORKEY)
		{
			/*
			 * We drop colorkey data here, but we have to,
			 * or we'll run into trouble when converting,
			 * in particular from indexed color formats.
			 */
			SDL_SetColorKey(tmp, SDL_SRCCOLORKEY,
					surface->format->colorkey);
			key2alpha(tmp);
		}
		SDL_SetColorKey(tmp, 0, 0);

		if(use_rgba)
			s = CreateRGBASurface(surface->w, surface->h);
		else
			s = CreateRGBSurface(surface->w, surface->h);
		if(!s)
		{
			glSDL_FreeSurface(tmp);
			return NULL;
		}
		SDL_BlitSurface(tmp, NULL, s, NULL);
		glSDL_FreeSurface(tmp);

		if(surface->flags & SDL_SRCALPHA)
			SDL_SetAlpha(s, SDL_SRCALPHA,
					surface->format->alpha);
		return s;
	}
	else
	{
		s = SDL_DisplayFormat(surface);
		if(s)
			GLSDL_FIX_SURFACE(s);
		return s;
	}
}


SDL_Surface *glSDL_DisplayFormatAlpha(SDL_Surface *surface)
{
	SDL_Surface *s, *tmp;
	if(USING_GLSDL)
	{
		tmp = SDL_ConvertSurface(surface, &RGBAfmt, SDL_SWSURFACE);
		if(!tmp)
			return NULL;
		GLSDL_FIX_SURFACE(tmp);

		SDL_SetAlpha(tmp, 0, 0);
		SDL_SetColorKey(tmp, 0, 0);
		s = CreateRGBASurface(surface->w, surface->h);
		if(!s)
		{
			glSDL_FreeSurface(tmp);
			return NULL;
		}
		SDL_BlitSurface(tmp, NULL, s, NULL);
		glSDL_FreeSurface(tmp);

		if(surface->flags & SDL_SRCCOLORKEY)
		{
			SDL_SetColorKey(s, SDL_SRCCOLORKEY,
					surface->format->colorkey);
			key2alpha(s);
		}
		if(surface->flags & SDL_SRCALPHA)
			SDL_SetAlpha(s, SDL_SRCALPHA,
					surface->format->alpha);
		return s;
	}
	else
	{
		s = SDL_DisplayFormatAlpha(surface);
		if(s)
			GLSDL_FIX_SURFACE(s);
		return s;
	}
}


SDL_Surface *glSDL_ConvertSurface
			(SDL_Surface *src, SDL_PixelFormat *fmt, Uint32 flags)
{
	SDL_Surface *s = SDL_ConvertSurface(src, fmt, flags);
	if(s)
		GLSDL_FIX_SURFACE(s);
	return s;
}


SDL_Surface *glSDL_CreateRGBSurface
			(Uint32 flags, int width, int height, int depth, 
			Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	SDL_Surface *s = SDL_CreateRGBSurface(flags, width, height, depth, 
			Rmask, Gmask, Bmask, Amask);
	if(s)
		GLSDL_FIX_SURFACE(s);
	return s;
}


SDL_Surface *glSDL_CreateRGBSurfaceFrom(void *pixels,
			int width, int height, int depth, int pitch,
			Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceFrom(pixels,
			width, height, depth, pitch,
			Rmask, Gmask, Bmask, Amask);
	if(s)
		GLSDL_FIX_SURFACE(s);
	return s;
}


SDL_Surface *glSDL_LoadBMP(const char *file)
{
	SDL_Surface *s = SDL_LoadBMP(file);
	if(s)
		GLSDL_FIX_SURFACE(s);
	return s;
}

SDL_Surface *glSDL_IMG_Load_RW(SDL_RWops *file, int freesrc)
{
	SDL_Surface *s = IMG_Load_RW(file, freesrc);
	if(s)
		GLSDL_FIX_SURFACE(s);
	return s;
}

int glSDL_SaveBMP(SDL_Surface *surface, const char *file)
{
	SDL_Rect r;
	SDL_Surface *buf;
	SDL_Surface *screen = SDL_GetVideoSurface();

	if(!USING_GLSDL)
		return SDL_SaveBMP(surface, file);

	if((surface != screen) && (surface != fake_screen))
		return SDL_SaveBMP(surface, file);

	buf = CreateRGBSurface(fake_screen->w, fake_screen->h);

	r.x = 0;
	r.y = 0;
	r.w = fake_screen->w;
	r.h = fake_screen->h;
	if(glSDL_BlitFromGL(&r, buf, &r) < 0)
		return -1;
	
	return SDL_SaveBMP(buf, file);

	glSDL_FreeSurface(buf);
}




/*----------------------------------------------------------
	glSDL specific API extensions
----------------------------------------------------------*/

void glSDL_Invalidate(SDL_Surface *surface, SDL_Rect *area)
{
	glSDL_TexInfo *txi;
	if(!surface)
		return;
	txi = glSDL_GetTexInfo(surface);
	if(!txi)
		return;
	if(!area)
	{
		txi->invalid_area.x = 0;
		txi->invalid_area.y = 0;
		txi->invalid_area.w = surface->w;
		txi->invalid_area.h = surface->h;
		return;
	}
	txi->invalid_area = *area;
}


static int InitTexture(SDL_Surface *datasurf, glSDL_TexInfo *txi, int tex)
{
	gl.GenTextures(1, (unsigned int *)&txi->texture[tex]);
	gl.BindTexture(GL_TEXTURE_2D, txi->texture[tex]);
	gl.PixelStorei(GL_UNPACK_ROW_LENGTH, datasurf->pitch /
			datasurf->format->BytesPerPixel);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexImage2D(GL_TEXTURE_2D, 0,
			datasurf->format->Amask ? GL_RGBA8 : GL_RGB8,
			txi->texsize, txi->texsize, 0,
			datasurf->format->Amask ? GL_RGBA : GL_RGB,
			GL_UNSIGNED_BYTE, NULL);
	print_glerror(1);
	return 0;
}


/* Image tiled horizontally (wide surface), or not at all */
static int UploadHoriz(SDL_Surface *datasurf, glSDL_TexInfo *txi)
{
	int bpp = datasurf->format->BytesPerPixel;
	int res;
	int tex = 0;
	int fromx = 0;
	int toy = txi->texsize;	/* To init first texture */
	while(1)
	{
		int thistw = datasurf->w - fromx;
		if(thistw > txi->tilew)
			thistw = txi->tilew;
		else if(thistw <= 0)
			break;
		if(toy + txi->tileh > txi->texsize)
		{
			toy = 0;
			res = InitTexture(datasurf, txi, tex);
			if(res < 0)
				return res;
			++tex;
		}
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, toy,
				thistw, txi->tileh,
				datasurf->format->Amask ? GL_RGBA : GL_RGB,
				GL_UNSIGNED_BYTE,
				(char *)datasurf->pixels + bpp * fromx);
		print_glerror(2);
		fromx += txi->tilew;
		toy += txi->tileh;
		gl.Flush();
	}
	return 0;
}


/* Image tiled vertically (tall surface) */
static int UploadVert(SDL_Surface *datasurf, glSDL_TexInfo *txi)
{
	int res;
	int tex = 0;
	int fromy = 0;
	int tox = txi->texsize;	/* To init first texture */
	while(1)
	{
		int thisth = datasurf->h - fromy;
		if(thisth > txi->tileh)
			thisth = txi->tileh;
		else if(thisth <= 0)
			break;
		if(tox + txi->tilew > txi->texsize)
		{
			tox = 0;
			res = InitTexture(datasurf, txi, tex);
			if(res < 0)
				return res;
			++tex;
		}
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, tox, 0,
				txi->tilew, thisth,
				datasurf->format->Amask ? GL_RGBA : GL_RGB,
				GL_UNSIGNED_BYTE,
				(char *)datasurf->pixels + datasurf->pitch * fromy);
		print_glerror(3);
		fromy += txi->tileh;
		tox += txi->tilew;
		gl.Flush();
	}
	return 0;
}


/* Image tiled two-way (huge surface) */
static int UploadHuge(SDL_Surface *datasurf, glSDL_TexInfo *txi)
{
	int bpp = datasurf->format->BytesPerPixel;
	int res;
	int tex = 0;
	int y = 0;
	while(y < datasurf->h)
	{
		int x;
		int thisth = datasurf->h - y;
		if(thisth > txi->tileh)
			thisth = txi->tileh;
		x = 0;
		while(x < datasurf->w)
		{
			int thistw = datasurf->w - x;
			if(thistw > txi->tilew)
				thistw = txi->tilew;
			res = InitTexture(datasurf, txi, tex++);
			if(res < 0)
				return res;
//			DBG5(printf("glTexSubImage(x = %d, y = %d, w = %d, h = %d)\n",
//					x, y, thistw, thisth);)
			gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					thistw, thisth,
					datasurf->format->Amask ? GL_RGBA : GL_RGB,
					GL_UNSIGNED_BYTE,
					(char *)datasurf->pixels +
					datasurf->pitch * y + bpp * x);
			print_glerror(4);
			x += txi->tilew;
			gl.Flush();
		}
		y += txi->tileh;
	}
	return 0;
}


/* Upload all textures for a surface. */
static int UploadTextures(SDL_Surface *datasurf, glSDL_TexInfo *txi)
{
	switch(txi->tilemode)
	{
	  case GLSDL_TM_SINGLE:
	  case GLSDL_TM_HORIZONTAL:
		UploadHoriz(datasurf, txi);
		break;
	  case GLSDL_TM_VERTICAL:
		UploadVert(datasurf, txi);
		break;
	  case GLSDL_TM_HUGE:
		UploadHuge(datasurf, txi);
		break;
	}
	return 0;
}


int glSDL_UploadSurface(SDL_Surface *surface)
{
	SDL_Surface *datasurf = surface;
	glSDL_TexInfo *txi;
	int i;
	/* 
	 * For now, we just assume that *every* texture needs
	 * conversion before uploading.
	 */

	/* If there's no TexInfo, add one. */
	if(!IS_GLSDL_SURFACE(surface))
		glSDL_AddTexInfo(surface);

	txi = glSDL_GetTexInfo(surface);
	if(!txi)
		return -1;

	/* No partial updates implemented yet... */
	if(txi->invalid_area.w)
		glSDL_UnloadSurface(surface);
	else
	{
		int missing = 0;
		if(txi->textures)
		{
			for(i = 0; i < txi->textures; ++i)
				if(-1 == txi->texture[i])
				{
					missing = 1;
					break;
				}
			if(!missing)
				return 0;	/* They're already there! */
		}
	}

	if(txi->texsize > maxtexsize)
	{
		fprintf(stderr, "glSDL/wrapper: INTERNAL ERROR: Too large texture!\n");
		return -1;	/* This surface wasn't tiled properly... */
	}

	/*
	 * Kludge: Convert if not of preferred RGB or RGBA format.
	 *
	 *	Conversion should only be done when *really* needed.
	 *	That is, it should rarely have to be done with OpenGL
	 *	1.2+.
	 *
	 *	Besides, any surface that's been SDL_DisplayFormat()ed
	 *	should already be in the best known OpenGL format -
	 *	preferably one that makes DMA w/o conversion possible.
	 */
	if(FormatIsOk(surface))
		datasurf = surface;
	else
	{
		DBG(fprintf(stderr, "glSDL/wrapper: WARNING: On-the-fly conversion performed!\n"));
		if(surface->format->Amask)
			datasurf = glSDL_DisplayFormatAlpha(surface);
		else
			datasurf = glSDL_DisplayFormat(surface);
		if(!datasurf)
			return -2;
	}

	if(UploadTextures(datasurf, txi) < 0)
		return -3;

	if(datasurf != surface)
		glSDL_FreeSurface(datasurf);
	return 0;
}


static void UnloadTexture(glSDL_TexInfo *txi)
{
	int i;
	for(i = 0; i < txi->textures; ++i)
		gl.DeleteTextures(1, (unsigned int *)&txi->texture[i]);
	memset(&txi->invalid_area, 0, sizeof(txi->invalid_area));
}


void glSDL_UnloadSurface(SDL_Surface *surface)
{
	glSDL_TexInfo *txi;
	if(!IS_GLSDL_SURFACE(surface))
		return;

	txi = glSDL_GetTexInfo(surface);
	if(txi)
		UnloadTexture(txi);
}


SDL_Surface *glSDL_IMG_Load(const char *file)
{
	SDL_Surface *s;
	s = IMG_Load(file);
	if(s)
		GLSDL_FIX_SURFACE(s);
	return s;
}


int glSDL_DrawLine(SDL_Surface *dst, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Uint32 color, Uint8 a)
{
	SDL_Surface *vs = SDL_GetVideoSurface();
	SDL_PixelFormat *pf = dst->format;
	SDL_Rect dstrect;
	Uint8 r, g, b;
	
	/* this little hacky coordinate transformation is mandatory to comply with the software appearance of lines */
	if (y1 == y2)
	{
		y1++;
		y2++;
	}

	if(dst == fake_screen)
		dst = vs;
	if(vs != dst)
	{
		dstrect.x = x2 < x1 ? x2 : x1;
		dstrect.y = y2 < y1 ? y2 : y1;
		dstrect.w = x2 < x1 ? x1 - x2 : x2 - x1;
		dstrect.h = y2 < y1 ? y1 - y2 : y2 - y1;
		glSDL_Invalidate(dst, &dstrect);
	}
	if((vs != dst) || !USING_GLSDL)
		return -1;

	SDL_GetRGB(color, pf, &r, &g, &b);

	gl_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_do_blend(1);
	gl_do_texture(0);
	if ((y2-y1 == 0) || (x2-x1 == 0))
		gl.Disable(GL_LINE_SMOOTH);
	else
		gl.Enable(GL_LINE_SMOOTH);

	gl.Begin(GL_LINES);
	if (a < 255)
		gl.Color4ub(r, g, b, a);
	else
		gl.Color3ub(r, g, b);
	gl.Vertex2i(x1, y1);
	gl.Vertex2i(x2, y2);
	gl.End();

	return 0;
}

int glSDL_DrawRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color, Uint8 a)
{	
	SDL_Surface *vs = SDL_GetVideoSurface();
	SDL_PixelFormat *pf = dst->format;
	int dx1, dy1, dx2, dy2;
	Uint8 r, g, b;

	if(dst == fake_screen)
		dst = vs;
	if(vs != dst)
		glSDL_Invalidate(dst, dstrect);
	if((vs != dst) || !USING_GLSDL)
		return -1;

	if(dstrect)
	{
		dx1 = dstrect->x;
		dy1 = dstrect->y;
		dx2 = dx1 + dstrect->w;
		dy2 = dy1 + dstrect->h;
		if(dx1 < dst->clip_rect.x)
			dx1 = dst->clip_rect.x;
		if(dy1 < dst->clip_rect.y)
			dy1 = dst->clip_rect.y;
		if(dx2 > dst->clip_rect.x + dst->clip_rect.w)
			dx2 = dst->clip_rect.x + dst->clip_rect.w;
		if(dy2 > dst->clip_rect.y + dst->clip_rect.h)
			dy2 = dst->clip_rect.y + dst->clip_rect.h;
		dstrect->x = dx1;
		dstrect->y = dy1;
		dstrect->w = dx2 - dx1;
		dstrect->h = dy2 - dy1;
		if(!dstrect->w || !dstrect->h)
			return 0;
	}
	else
	{
		dx1 = dst->clip_rect.x;
		dy1 = dst->clip_rect.y;
		dx2 = dx1 + dst->clip_rect.w;
		dy2 = dy1 + dst->clip_rect.h;
	}

	SDL_GetRGB(color, pf, &r, &g, &b);
	
	if (a < 255)
	{
		gl_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl_do_blend(1);
	}
	else
		gl_do_blend(0);
	gl_do_texture(0);
	gl.Disable(GL_LINE_SMOOTH);

	gl.Begin(GL_LINES);
	if (a < 255)
		gl.Color4ub(r, g, b, a);
	else
		gl.Color3ub(r, g, b);
	gl.Vertex2i(dx1, dy1+1); gl.Vertex2i(dx1, dy2);
	gl.Vertex2i(dx1+1, dy2); gl.Vertex2i(dx2-1, dy2);
	gl.Vertex2i(dx2-1, dy2); gl.Vertex2i(dx2-1, dy1+1);
	gl.Vertex2i(dx2, dy1+1); gl.Vertex2i(dx1, dy1+1);
	gl.End();

	return 0;
}

int glSDL_SetPixel(SDL_Surface *dst, Sint16 x, Sint16 y, Uint32 color, Uint8 a)
{
	SDL_Surface *vs = SDL_GetVideoSurface();
	SDL_PixelFormat *pf = dst->format;
	SDL_Rect dstrect;
	Uint8 r, g, b;

	if(dst == fake_screen)
		dst = vs;
	if(vs != dst)
	{
		dstrect.x = x;
		dstrect.y = y;
		dstrect.w = 1;
		dstrect.h = 1;
		glSDL_Invalidate(dst, &dstrect);
	}
	if((vs != dst) || !USING_GLSDL)
		return -1;
	
	SDL_GetRGB(color, pf, &r, &g, &b);

	if (a < 255)
	{
		gl_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl_do_blend(1);
	}
	else
		gl_do_blend(0);
	gl_do_texture(0);
	
	gl.Begin(GL_POINTS);
	gl.Color3ub(r, g, b);
	gl.Vertex2i(x, y);
	gl.End();
	
	return 0;
}

int glSDL_DrawCircle(SDL_Surface *dst, Sint16 x, Sint16 y, Sint16 ray, Uint32 color, Uint8 a)
{
	SDL_Surface *vs = SDL_GetVideoSurface();
	SDL_PixelFormat *pf = dst->format;
	SDL_Rect dstrect;
	Uint8 r, g, b;
	int x1, y1, x2, y2;
	int i, tot;
	double fx, fy, fray;
	
	x1 = x-ray;
	y1 = y-ray;
	x2 = x+ray;
	y2 = y+ray;
		
	if (dst == fake_screen)
		dst = vs;
	if (vs != dst)
	{
		dstrect.x = x2 < x1 ? x2 : x1;
		dstrect.y = y2 < y1 ? y2 : y1;
		dstrect.w = x2 < x1 ? x1 - x2 : x2 - x1;
		dstrect.h = y2 < y1 ? y1 - y2 : y2 - y1;
		glSDL_Invalidate(dst, &dstrect);
	}
	if ((vs != dst) || !USING_GLSDL)
		return -1;

	SDL_GetRGB(color, pf, &r, &g, &b);

	gl_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_do_blend(1);
	gl_do_texture(0);
	gl.Enable(GL_LINE_SMOOTH);
	gl.LineWidth(2);

	tot = ray;
	fx = x;
	fy = y;
	fray = ray;

	gl.Begin(GL_LINES);
	if (a < 255)
		gl.Color4ub(r, g, b, a);
	else
		gl.Color3ub(r, g, b);
	for (i=0; i<tot; i++)
	{
		double angle0 = (2*M_PI*(double)i)/((double)tot);
		double angle1 = (2*M_PI*(double)(i+1))/((double)tot);
		gl.Vertex2d(fx+fray*sin(angle0), fy+fray*cos(angle0));
		gl.Vertex2d(fx+fray*sin(angle1), fy+fray*cos(angle1));
	}
	gl.End();
	gl.LineWidth(1);
	
	return 0;
}

void glSDL_CreateViewport(Sint16 x, Sint16 y, Sint16 w, Sint16 h, float scale)
{	
	SDL_Rect rect = { 0, 0, w*scale, h*scale };
	viewport_set = 1;
	glSDL_SetClipRect(SDL_GetVideoSurface(), &rect);
	gl.Viewport(x, y, w, h);
	/*
	 * Note that this projection is upside down in
	 * relation to the OpenGL coordinate system.
	 */
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.Ortho(0, scale * w, scale * h, 0,
			-1.0, 1.0);

	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.Translatef(0.0f, 0.0f, 0.0f);
}

void glSDL_ResetViewport(Sint16 w, Sint16 h)
{
	SDL_Rect rect = { 0, 0, w, h };
	viewport_set = 0;
	glSDL_SetClipRect(SDL_GetVideoSurface(), &rect);
	gl.Viewport(0, 0, w, h);
	/*
	 * Note that this projection is upside down in
	 * relation to the OpenGL coordinate system.
	 */
	gl.MatrixMode(GL_PROJECTION);
	gl.LoadIdentity();
	gl.Ortho(0, w, h, 0,
			-1.0, 1.0);

	gl.MatrixMode(GL_MODELVIEW);
	gl.LoadIdentity();
	gl.Translatef(0.0f, 0.0f, 0.0f);
}

#endif /* HAVE_OPENGL */
