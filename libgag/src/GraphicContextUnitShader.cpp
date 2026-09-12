// SPDX-License-Identifier: GPL-3.0-or-later

// GLSL 1.20 program that recolors a sprite's unrotated team layer on the GPU
// (the same HSV hue shift as DrawableSurface::shiftHSV) and alpha-composites it
// over the base layer in one quad, so GPU unit rendering never creates a
// team-coloured or composite texture. Created with the GL context in
// GraphicContext::setRes and destroyed before that context is torn down; a
// compile/link failure disables it (hasUnitShader() stays false) and the
// caller falls back to two ordinary drawSurface calls through the CPU cache.

#include "GraphicContextPrivate.h"
#include <cstdlib>
#include <iostream>

#ifdef HAVE_OPENGL

namespace GAGCore
{
	namespace
	{
		const char *unitShaderVertexSource =
			"#version 120\n"
			"varying vec2 vBaseUV;\n"
			"varying vec2 vTeamUV;\n"
			"void main()\n"
			"{\n"
			"	vBaseUV = gl_MultiTexCoord0.st;\n"
			"	vTeamUV = gl_MultiTexCoord1.st;\n"
			"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
			"}\n";

		// Mirrors SupportFunctions.cpp's RGBtoHSV/HSVtoRGB exactly (only h is ever
		// shifted here; s and v pass through unchanged, as Sprite::applyTeamHueShift
		// always shifts hue alone), with divide-by-zero guarded rather than relied
		// upon to produce a masked NaN as the CPU version does.
		const char *unitShaderFragmentSource =
			"#version 120\n"
			"uniform sampler2D uBase;\n"
			"uniform sampler2D uTeam;\n"
			"uniform bool uHasBase;\n"
			"uniform bool uHasTeam;\n"
			"uniform float uHueShift;\n"
			"uniform float uAlpha;\n"
			"varying vec2 vBaseUV;\n"
			"varying vec2 vTeamUV;\n"
			"vec3 rgb2hsv(vec3 c)\n"
			"{\n"
			"	float mx = max(c.r, max(c.g, c.b));\n"
			"	float mn = min(c.r, min(c.g, c.b));\n"
			"	float delta = mx - mn;\n"
			"	float s = (mx != 0.0) ? delta / mx : 0.0;\n"
			"	float h = 0.0;\n"
			"	if (delta > 0.0)\n"
			"	{\n"
			"		if (c.r == mx) h = (c.g - c.b) / delta;\n"
			"		else if (c.g == mx) h = 2.0 + (c.b - c.r) / delta;\n"
			"		else h = 4.0 + (c.r - c.g) / delta;\n"
			"		h *= 60.0;\n"
			"		if (h < 0.0) h += 360.0;\n"
			"	}\n"
			"	return vec3(h, s, mx);\n"
			"}\n"
			"vec3 hsv2rgb(vec3 hsv)\n"
			"{\n"
			"	float h = hsv.x, s = hsv.y, v = hsv.z;\n"
			"	if (s == 0.0) return vec3(v, v, v);\n"
			"	h /= 60.0;\n"
			"	float fi = floor(h);\n"
			"	float f = h - fi;\n"
			"	float p = v * (1.0 - s);\n"
			"	float q = v * (1.0 - s * f);\n"
			"	float t = v * (1.0 - s * (1.0 - f));\n"
			"	int sector = int(mod(fi, 6.0));\n"
			"	if (sector == 0) return vec3(v, t, p);\n"
			"	if (sector == 1) return vec3(q, v, p);\n"
			"	if (sector == 2) return vec3(p, v, t);\n"
			"	if (sector == 3) return vec3(p, q, v);\n"
			"	if (sector == 4) return vec3(t, p, v);\n"
			"	return vec3(v, p, q);\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	vec4 base = uHasBase ? texture2D(uBase, vBaseUV) : vec4(0.0);\n"
			"	vec4 team = vec4(0.0);\n"
			"	if (uHasTeam)\n"
			"	{\n"
			"		vec4 raw = texture2D(uTeam, vTeamUV);\n"
			"		vec3 hsv = rgb2hsv(raw.rgb);\n"
			"		float h = mod(hsv.x + uHueShift, 360.0);\n"
			"		if (h < 0.0) h += 360.0;\n"
			"		team = vec4(hsv2rgb(vec3(h, hsv.y, hsv.z)), raw.a);\n"
			"	}\n"
			// Team layer over base layer (the same order Sprite::getColoredSurface's
			// caller draws them in), combined in premultiplied space and divided
			// back out once: needed because base and team can each be partially
			// transparent at the same edge texel, so neither can be dropped from
			// the premultiply. The pose's shutter weight is then applied to alpha
			// only: straight, non-premultiplied output for the fixed GL_SRC_ALPHA /
			// GL_ONE_MINUS_SRC_ALPHA blend already set for sprites.
			"	vec3 premultRGB = team.rgb * team.a + base.rgb * base.a * (1.0 - team.a);\n"
			"	float a = team.a + base.a * (1.0 - team.a);\n"
			"	vec3 rgb = (a > 0.0) ? premultRGB / a : vec3(0.0);\n"
			"	gl_FragColor = vec4(rgb, a * uAlpha);\n"
			"}\n";

		unsigned compileShader(unsigned type, const char *source, const char *what)
		{
			unsigned shader = glCreateShader(type);
			glShaderSource(shader, 1, &source, nullptr);
			glCompileShader(shader);
			GLint ok = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
			if (!ok)
			{
				char log[1024];
				GLsizei len = 0;
				glGetShaderInfoLog(shader, sizeof(log) - 1, &len, log);
				log[len] = '\0';
				std::cerr << "Unit shader " << what << " compile failed: " << log << std::endl;
				glDeleteShader(shader);
				return 0;
			}
			return shader;
		}
	}

	void GraphicContext::createUnitShader()
	{
		// Test-only escape hatch for the CPU/software fallback path, exercised the
		// same way a real compile/link failure would be. Same override style as
		// Sprite.cpp's GLOB2_EXPERIMENT_TEXTURE_DIR.
		if (std::getenv("GLOB2_DISABLE_UNIT_SHADER"))
		{
			if (!unitShaderFailureLogged)
			{
				std::cerr << "Unit shader disabled via GLOB2_DISABLE_UNIT_SHADER, using the CPU team-color cache" << std::endl;
				unitShaderFailureLogged = true;
			}
			unitShaderProgram = 0;
			return;
		}
		unsigned vertex = compileShader(GL_VERTEX_SHADER, unitShaderVertexSource, "vertex");
		unsigned fragment = vertex ? compileShader(GL_FRAGMENT_SHADER, unitShaderFragmentSource, "fragment") : 0;
		unsigned program = 0;
		if (vertex && fragment)
		{
			program = glCreateProgram();
			glAttachShader(program, vertex);
			glAttachShader(program, fragment);
			glLinkProgram(program);
			GLint ok = GL_FALSE;
			glGetProgramiv(program, GL_LINK_STATUS, &ok);
			if (!ok)
			{
				char log[1024];
				GLsizei len = 0;
				glGetProgramInfoLog(program, sizeof(log) - 1, &len, log);
				log[len] = '\0';
				if (!unitShaderFailureLogged)
				{
					std::cerr << "Unit shader link failed, using the CPU team-color cache: " << log << std::endl;
					unitShaderFailureLogged = true;
				}
				glDeleteProgram(program);
				program = 0;
			}
		}
		else if (!unitShaderFailureLogged)
		{
			std::cerr << "Unit shader unavailable, using the CPU team-color cache" << std::endl;
			unitShaderFailureLogged = true;
		}
		// Shader objects are refcounted by the program once attached+linked; safe
		// to delete our handles either way (link failure leaves them attached to
		// a program we are about to delete too).
		if (vertex) glDeleteShader(vertex);
		if (fragment) glDeleteShader(fragment);

		unitShaderProgram = program;
		if (!program)
		{
			unitShaderLocBase = unitShaderLocTeam = -1;
			unitShaderLocHasBase = unitShaderLocHasTeam = -1;
			unitShaderLocHueShift = unitShaderLocAlpha = -1;
			return;
		}
		unitShaderLocBase = glGetUniformLocation(program, "uBase");
		unitShaderLocTeam = glGetUniformLocation(program, "uTeam");
		unitShaderLocHasBase = glGetUniformLocation(program, "uHasBase");
		unitShaderLocHasTeam = glGetUniformLocation(program, "uHasTeam");
		unitShaderLocHueShift = glGetUniformLocation(program, "uHueShift");
		unitShaderLocAlpha = glGetUniformLocation(program, "uAlpha");
	}

	void GraphicContext::destroyUnitShader()
	{
		if (unitShaderProgram)
			glDeleteProgram(unitShaderProgram);
		unitShaderProgram = 0;
	}

	bool GraphicContext::drawTeamColoredQuad(DrawableSurface *base, DrawableSurface *team, float x, float y, float w, float h, Uint8 alpha, float hueShift)
	{
		if (!unitShaderProgram)
			return false;
		if (base && base->dirty) base->uploadToTexture();
		if (team && team->dirty) team->uploadToTexture();
		if ((base && !base->texture) || (team && !team->texture))
			return false;

		auto uv = [](DrawableSurface *s, float &u0, float &v0, float &u1, float &v1)
		{
			const float biasX = 0.00141421356f * s->texMultX;
			const float biasY = 0.00141421356f * s->texMultY;
			u0 = static_cast<float>(s->getTexX()) * s->texMultX + biasX;
			u1 = static_cast<float>(s->getTexX() + s->getW()) * s->texMultX + biasX;
			v0 = static_cast<float>(s->getTexY()) * s->texMultY + biasY;
			v1 = static_cast<float>(s->getTexY() + s->getH()) * s->texMultY + biasY;
		};
		float bu0 = 0, bv0 = 0, bu1 = 0, bv1 = 0, tu0 = 0, tv0 = 0, tu1 = 0, tv1 = 0;
		if (base) uv(base, bu0, bv0, bu1, bv1);
		if (team) uv(team, tu0, tv0, tu1, tv1);

		glUseProgram(unitShaderProgram);
		glActiveTexture(GL_TEXTURE0);
		glState.setTexture(base ? base->texture : (team ? team->texture : 0));
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, team ? team->texture : (base ? base->texture : 0));
		glActiveTexture(GL_TEXTURE0);

		glUniform1i(unitShaderLocBase, 0);
		glUniform1i(unitShaderLocTeam, 1);
		glUniform1i(unitShaderLocHasBase, base ? 1 : 0);
		glUniform1i(unitShaderLocHasTeam, team ? 1 : 0);
		glUniform1f(unitShaderLocHueShift, hueShift);
		glUniform1f(unitShaderLocAlpha, alpha / 255.0f);

		glState.doBlend(true);
		++drawCalls;
		glBegin(GL_QUADS);
		glMultiTexCoord2f(GL_TEXTURE0, bu0, bv0); glMultiTexCoord2f(GL_TEXTURE1, tu0, tv0); glVertex2f(x, y);
		glMultiTexCoord2f(GL_TEXTURE0, bu1, bv0); glMultiTexCoord2f(GL_TEXTURE1, tu1, tv0); glVertex2f(x + w, y);
		glMultiTexCoord2f(GL_TEXTURE0, bu1, bv1); glMultiTexCoord2f(GL_TEXTURE1, tu1, tv1); glVertex2f(x + w, y + h);
		glMultiTexCoord2f(GL_TEXTURE0, bu0, bv1); glMultiTexCoord2f(GL_TEXTURE1, tu0, tv1); glVertex2f(x, y + h);
		glEnd();

		// Restore fixed-function state: every other draw call in this codebase
		// assumes no program and unit 0 active.
		glUseProgram(0);
		// The direct GL_TEXTURE1 bind above bypassed glState's single-unit cache;
		// nothing else uses unit 1, so leaving it bound is harmless, but the
		// active unit itself must be unit 0 again for glState.setTexture to work.
		return true;
	}
}

#else // !HAVE_OPENGL

namespace GAGCore
{
	void GraphicContext::createUnitShader() { }
	void GraphicContext::destroyUnitShader() { }
	bool GraphicContext::drawTeamColoredQuad(DrawableSurface *, DrawableSurface *, float, float, float, float, Uint8, float) { return false; }
}

#endif // HAVE_OPENGL
