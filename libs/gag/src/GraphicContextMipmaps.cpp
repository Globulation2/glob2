// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>

#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)
// Compatibility contexts (including macOS) expose the EXT framebuffer API.
#undef glGenFramebuffers
#define glGenFramebuffers glGenFramebuffersEXT
#undef glDeleteFramebuffers
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#undef glBindFramebuffer
#define glBindFramebuffer glBindFramebufferEXT
#undef glFramebufferTexture2D
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#undef glCheckFramebufferStatus
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT

namespace GAGCore
{
namespace
{
struct Pipeline
{
	GLuint program = 0, framebuffer = 0, scratch = 0;
	GLint extent = -1;
	bool attempted = false, separateFramebuffers = false;
};
std::map<SDL_GLContext, Pipeline> pipelines;
GLuint shader(GLenum type, const std::string &source)
{
	GLuint result = glCreateShader(type);
	const char *data = source.c_str();
	glShaderSource(result, 1, &data, nullptr);
	glCompileShader(result);
	GLint ok = 0;
	glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		glDeleteShader(result);
		return 0;
	}
	return result;
}
bool initialize(Pipeline &p)
{
	if (p.attempted)
		return p.program != 0;
	p.attempted = true;
	// Integer arithmetic is required for byte-identical alpha-weighted filtering.
	const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	const bool modern = version && std::atoi(version) >= 3;
	if ((!modern && !SDL_GL_ExtensionSupported("GL_EXT_gpu_shader4")) ||
		!SDL_GL_ExtensionSupported("GL_EXT_framebuffer_object"))
		return false;
	p.separateFramebuffers = SDL_GL_ExtensionSupported("GL_EXT_framebuffer_blit") ||
							 SDL_GL_ExtensionSupported("GL_ARB_framebuffer_object");
	const std::string prefix = modern ? "#version 130\n"
									  : "#version 120\n#extension GL_EXT_gpu_shader4 : "
										"require\n#define texelFetch texelFetch2D\n";
	const GLuint vertex = shader(GL_VERTEX_SHADER, prefix + "void main(){gl_Position=gl_Vertex;}");
	const GLuint fragment = shader(GL_FRAGMENT_SHADER, prefix + R"GLSL(
uniform sampler2D source;
uniform vec2 extent;
uvec4 pixel(ivec2 p) {
    return uvec4(floor(texelFetch(source, min(p, ivec2(extent)-1), 0)*255.0+0.5));
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy)*2;
    uvec4 a=pixel(p), b=pixel(p+ivec2(1,0)), c=pixel(p+ivec2(0,1)), d=pixel(p+ivec2(1,1));
    uint alpha=a.a+b.a+c.a+d.a;
    uvec3 color=uvec3(0);
    if(alpha>0u) color=(a.rgb*a.a+b.rgb*b.a+c.rgb*c.a+d.rgb*d.a+alpha/2u)/alpha;
    gl_FragColor=vec4(color,(alpha+2u)/4u)/255.0;
}
)GLSL");
	if (vertex && fragment)
	{
		p.program = glCreateProgram();
		glAttachShader(p.program, vertex);
		glAttachShader(p.program, fragment);
		glLinkProgram(p.program);
		GLint ok = 0;
		glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			glDeleteProgram(p.program);
			p.program = 0;
		}
	}
	if (vertex)
		glDeleteShader(vertex);
	if (fragment)
		glDeleteShader(fragment);
	if (!p.program)
		return false;
	p.extent = glGetUniformLocation(p.program, "extent");
	glGenFramebuffers(1, &p.framebuffer);
	glGenTextures(1, &p.scratch);
	return true;
}
struct State
{
	GLint program = 0, draw = 0, read = 0, unpackBuffer = 0;
	bool separate;
#ifndef __APPLE__
	bool samplers;
	GLint sampler = 0;
#endif
	explicit State(bool separate) : separate(separate)
	{
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &draw);
		if (separate)
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
		glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
		glPushAttrib(GL_ALL_ATTRIB_BITS);
		glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
		glActiveTexture(GL_TEXTURE0);
#ifndef __APPLE__
		samplers = epoxy_gl_version() >= 33 || epoxy_has_gl_extension("GL_ARB_sampler_objects");
		if (samplers)
		{
			glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
			glBindSampler(0, 0);
		}
#endif
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
		glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
		glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DITHER);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_COLOR_LOGIC_OP);
		for (int i = 0; i < 6; ++i)
			glDisable(GL_CLIP_PLANE0 + i);
		if (SDL_GL_ExtensionSupported("GL_EXT_framebuffer_sRGB") ||
			SDL_GL_ExtensionSupported("GL_ARB_framebuffer_sRGB"))
			glDisable(GL_FRAMEBUFFER_SRGB);
#ifndef __APPLE__
		if (epoxy_gl_version() >= 30)
			glDisable(GL_RASTERIZER_DISCARD);
#endif
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}
	~State()
	{
		if (separate)
		{
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
		}
		else
			glBindFramebuffer(GL_FRAMEBUFFER, draw);
		glUseProgram(program);
#ifndef __APPLE__
		if (samplers)
			glBindSampler(0, sampler);
#endif
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
		glPopClientAttrib();
		glPopAttrib();
	}
};
} // namespace
bool prepareNativeMipmaps(unsigned texture, SDL_Surface *surface)
{
	if (!texture || !surface || surface->format != SDL_PIXELFORMAT_ARGB8888 || surface->w <= 0 ||
		surface->h <= 0 || surface->pitch != surface->w * 4 || (surface->w & (surface->w - 1)) ||
		(surface->h & (surface->h - 1)) ||
		(std::getenv("GLOB2_DISABLE_GPU_TEXTURE_PREPARATION") &&
		 *std::getenv("GLOB2_DISABLE_GPU_TEXTURE_PREPARATION")))
		return false;
	auto context = SDL_GL_GetCurrentContext();
	if (!context)
		return false;
	// Temporarily unbinding a deletion-pending program would destroy it.
	GLint program = 0, pending = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	if (program)
		glGetProgramiv(program, GL_DELETE_STATUS, &pending);
	if (pending)
		return false;
	auto &p = pipelines[context];
	if (!initialize(p))
		return false;
	State state(p.separateFramebuffers);
	glBindTexture(GL_TEXTURE_2D, texture);
	GLint base, maximum, filter;
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, &base);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maximum);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
	int width = surface->w, height = surface->h, level = 0;
	// Packed ARGB is a numeric word on either endian architecture.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
				 surface->pixels);
	glUseProgram(p.program);
	glUniform1i(glGetUniformLocation(p.program, "source"), 0);
	glBindFramebuffer(GL_FRAMEBUFFER, p.framebuffer);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	bool ok = true;
	while (width > 1 || height > 1)
	{
		const int nextWidth = std::max(1, width / 2), nextHeight = std::max(1, height / 2);
		glBindTexture(GL_TEXTURE_2D, p.scratch);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nextWidth, nextHeight, 0, GL_RGBA,
					 GL_UNSIGNED_BYTE, nullptr);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.scratch, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			ok = false;
			break;
		}
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, level);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glUniform2f(p.extent, float(width), float(height));
		glViewport(0, 0, nextWidth, nextHeight);
		glBegin(GL_QUADS);
		glVertex2f(-1, -1);
		glVertex2f(1, -1);
		glVertex2f(1, 1);
		glVertex2f(-1, 1);
		glEnd();
		glTexImage2D(GL_TEXTURE_2D, ++level, GL_RGBA8, nextWidth, nextHeight, 0, GL_RGBA,
					 GL_UNSIGNED_BYTE, nullptr);
		glCopyTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, 0, 0, nextWidth, nextHeight);
		width = nextWidth;
		height = nextHeight;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, base);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maximum);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	return ok;
}
void releaseNativeMipmaps(SDL_GLContext context)
{
	auto found = pipelines.find(context);
	if (found == pipelines.end())
		return;
	const auto &p = found->second;
	if (p.program)
		glDeleteProgram(p.program);
	if (p.framebuffer)
		glDeleteFramebuffers(1, &p.framebuffer);
	if (p.scratch)
		glDeleteTextures(1, &p.scratch);
	pipelines.erase(found);
}
} // namespace GAGCore
#else
namespace GAGCore
{
bool prepareNativeMipmaps(unsigned, SDL_Surface *)
{
	return false;
}
void releaseNativeMipmaps(SDL_GLContext) {}
} // namespace GAGCore
#endif
