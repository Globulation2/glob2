// SPDX-License-Identifier: GPL-3.0-or-later

// The opening cloud bank (IntroCover) drawn per pixel by a GLSL 1.20 program.
// The CPU supplies only a small lattice of noise ranks and heights for both
// layers; carving cloud out of it, shading and compositing happen here, so the
// edges are as sharp as the screen and the main thread does no per-pixel work.
// IntroCover::computeLayers and IntroCover::composite are the reference: every
// step below follows them, apart from computing at each pixel rather than at
// the texels of a stretched texture.

#include "GraphicContextPrivate.h"
#include <RenderBatch.h>
#include <cstdlib>
#include <iostream>

#if defined(HAVE_OPENGL) && !defined(GLOB2_WEBGL2)

namespace GAGCore
{
	namespace
	{
		const char *cloudCoverVertexSource =
			"#version 120\n"
			"varying vec2 vPosition;\n"
			"void main()\n"
			"{\n"
			"	vPosition = gl_Vertex.xy;\n"
			"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
			"}\n";

		const char *cloudCoverFragmentSource =
			"#version 120\n"
			"uniform sampler2D uLattice;\n"
			"uniform vec2 uOrigin;\n"
			"uniform float uCell;\n"
			"uniform vec2 uLatticeSize;\n"
			"uniform vec3 uCentre;\n" // x, y, 1 / maximum distance
			"uniform vec4 uOffsets;\n" // shadow x, y; light x, y
			"uniform vec4 uNear;\n" // threshold, bias, opacity, haze
			"uniform vec4 uFar;\n"
			"uniform vec2 uShadows;\n" // near, far
			"uniform vec4 uShade;\n" // x, y, 1 / rx, 1 / ry
			"uniform float uShadeAlpha;\n"
			"varying vec2 vPosition;\n"
			// Lattice point (i, j) is at the centre of texel (i, j).
			"vec4 lattice(vec2 p)\n"
			"{\n"
			"	return texture2D(uLattice, ((p - uOrigin) / uCell + 0.5) / uLatticeSize);\n"
			"}\n"
			// smoothstep, defined for either order of edges.
			"float ramp(float e0, float e1, float x)\n"
			"{\n"
			"	float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);\n"
			"	return t * t * (3.0 - 2.0 * t);\n"
			"}\n"
			"vec4 colour;\n" // premultiplied
			"void over(float alpha, vec3 c)\n"
			"{\n"
			"	colour = vec4(colour.rgb * (1.0 - alpha) + c * alpha, colour.a + alpha * (1.0 - colour.a));\n"
			"}\n"
			// One layer: its shadow, then the cloud graded from shaded creases to sunlit tops.
			"void layer(vec4 settings, float shadowOpacity, float centre, float rank, float rankShadow, float height,\n"
			"           float slope)\n"
			"{\n"
			"	float th = settings.x + centre * settings.y;\n"
			"	float a = ramp(th - 0.15, th + 0.35, rank);\n"
			"	over(0.35 * ramp(th - 0.15, th + 0.35, rankShadow) * shadowOpacity, vec3(20.0, 28.0, 44.0) / 255.0);\n"
			"	float tone = clamp(0.5 + 0.3 * height + 1.2 * slope + 0.25 * ramp(th, th + 0.5, rank) - 0.1, 0.0, 1.0);\n"
			"	vec3 c = tone < 0.5 ? mix(vec3(176.0, 182.0, 196.0), vec3(218.0, 222.0, 230.0), tone * 2.0)\n"
			"	                    : mix(vec3(218.0, 222.0, 230.0), vec3(250.0, 250.0, 248.0), tone * 2.0 - 1.0);\n"
			"	c = mix(c, vec3(176.0, 188.0, 212.0), settings.w) / 255.0;\n"
			"	over(a * settings.z, c);\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	vec2 p = vPosition;\n"
			"	vec4 here = lattice(p);\n"
			"	vec4 shadow = lattice(p - uOffsets.xy);\n"
			"	vec4 slope = lattice(p + uOffsets.zw) - lattice(p - uOffsets.zw);\n"
			"	float centre = 1.0 - length(p - uCentre.xy) * uCentre.z;\n"
			"	colour = vec4(0.0);\n"
			"	layer(uFar, uShadows.y, centre, here.b, shadow.b, here.a, slope.a);\n"
			"	layer(uNear, uShadows.x, centre, here.r, shadow.r, here.g, slope.g);\n"
			"	if (uShadeAlpha > 0.0)\n"
			"		over(0.32 * uShadeAlpha * ramp(1.2, 0.2, length((p - uShade.xy) * uShade.zw)), vec3(52.0, 64.0, 88.0) / 255.0);\n"
			// Straight alpha for the usual GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA blend.
			"	gl_FragColor = vec4(colour.a > 0.0 ? colour.rgb / colour.a : vec3(0.0), colour.a);\n"
			"}\n";

		unsigned compileCloudShader(unsigned type, const char *source)
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
				std::cerr << "Cloud cover shader compile failed, drawing it from a texture: " << log << std::endl;
				glDeleteShader(shader);
				return 0;
			}
			return shader;
		}

		unsigned linkCloudCoverProgram()
		{
			// Exercises the texture fallback the way a compile failure would.
			if (std::getenv("GLOB2_DISABLE_CLOUD_SHADER"))
				return 0;
			const unsigned vertex = compileCloudShader(GL_VERTEX_SHADER, cloudCoverVertexSource);
			const unsigned fragment = vertex ? compileCloudShader(GL_FRAGMENT_SHADER, cloudCoverFragmentSource) : 0;
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
					std::cerr << "Cloud cover shader link failed, drawing it from a texture: " << log << std::endl;
					glDeleteProgram(program);
					program = 0;
				}
			}
			if (vertex)
				glDeleteShader(vertex);
			if (fragment)
				glDeleteShader(fragment);
			return program;
		}
	}

	void GraphicContext::destroyCloudCover()
	{
		if (cloudCoverProgram)
			glDeleteProgram(cloudCoverProgram);
		if (cloudCoverTexture)
			glDeleteTextures(1, &cloudCoverTexture);
		cloudCoverProgram = cloudCoverTexture = 0;
		cloudCoverTextureW = cloudCoverTextureH = 0;
		cloudCoverAttempted = false;
	}

	void GraphicContext::prepareCloudCover()
	{
		// Only desktop OpenGL; the device renderers and the software renderer use a texture.
		if (renderer || !(optionFlags & USEGPU) || cloudCoverAttempted)
			return;
		cloudCoverAttempted = true;
		cloudCoverProgram = linkCloudCoverProgram();
	}

	bool GraphicContext::drawCloudCover(const CloudCoverFrame &frame)
	{
		if (renderer || !(optionFlags & USEGPU) || !frame.lattice || frame.latticeW < 2 || frame.latticeH < 2)
			return false;
		prepareCloudCover();
		if (!cloudCoverProgram)
			return false;

		// Queued draws go first, so the bank covers them in painter order.
		if (renderBatch)
			renderBatch->barrier();
		Sprite::flushBatches(this);

		// The lattice, interpolated by the texture unit between its points.
		glActiveTexture(GL_TEXTURE0);
		if (!cloudCoverTexture)
			glGenTextures(1, &cloudCoverTexture);
		glBindTexture(GL_TEXTURE_2D, cloudCoverTexture);
		// Rows of 16-bit texels, tightly packed; other uploads keep their own unpacking.
		GLint alignment = 4, rowLength = 0;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		if (cloudCoverTextureW != frame.latticeW || cloudCoverTextureH != frame.latticeH)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, frame.latticeW, frame.latticeH, 0, GL_RGBA, GL_UNSIGNED_SHORT,
			             frame.lattice);
			cloudCoverTextureW = frame.latticeW;
			cloudCoverTextureH = frame.latticeH;
		}
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.latticeW, frame.latticeH, GL_RGBA, GL_UNSIGNED_SHORT,
			                frame.lattice);
		glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
		// The direct bind bypassed the texture cache; make its next bind real.
		glState._texture = -1;

		glUseProgram(cloudCoverProgram);
		const auto at = [&](const char *name) { return glGetUniformLocation(cloudCoverProgram, name); };
		glUniform1i(at("uLattice"), 0);
		glUniform2f(at("uOrigin"), frame.originX, frame.originY);
		glUniform1f(at("uCell"), frame.cell);
		glUniform2f(at("uLatticeSize"), float(frame.latticeW), float(frame.latticeH));
		glUniform3f(at("uCentre"), frame.centreX, frame.centreY, frame.maxDistance > 0 ? 1 / frame.maxDistance : 0);
		glUniform4f(at("uOffsets"), frame.shadowX, frame.shadowY, frame.lightX, frame.lightY);
		const auto layer = [&](const char *name, const CloudCoverFrame::Layer &l)
		{ glUniform4f(at(name), l.threshold, l.bias, l.opacity, l.haze); };
		layer("uNear", frame.nearLayer);
		layer("uFar", frame.farLayer);
		glUniform2f(at("uShadows"), frame.nearLayer.shadow, frame.farLayer.shadow);
		glUniform4f(at("uShade"), frame.shadeX, frame.shadeY, frame.shadeRX > 0 ? 1 / frame.shadeRX : 0,
		            frame.shadeRY > 0 ? 1 / frame.shadeRY : 0);
		glUniform1f(at("uShadeAlpha"), frame.shadeAlpha);

		glState.doBlend(true);
		glState.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		++drawCalls;
		glBegin(GL_QUADS);
		glVertex2f(0, 0);
		glVertex2f(frame.width, 0);
		glVertex2f(frame.width, frame.height);
		glVertex2f(0, frame.height);
		glEnd();
		// Every other draw assumes no program.
		glUseProgram(0);
		return true;
	}
}

#else // no desktop OpenGL

namespace GAGCore
{
	void GraphicContext::destroyCloudCover() { }
	void GraphicContext::prepareCloudCover() { }
	bool GraphicContext::drawCloudCover(const CloudCoverFrame &) { return false; }
}

#endif
