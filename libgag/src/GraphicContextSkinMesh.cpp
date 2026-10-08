// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphicContextPrivate.h"
#include <SkinMesh.h>
#include <SkinModel.h>
#include <SkinShapeModel.h>
#include <SkinDeformation.h>
#include <glob2/SkinMaterials.h>
#include <PerformanceTelemetry.h>
#include <Toolkit.h>
#include <FileManager.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <stdexcept>
#include <utility>

namespace GAGCore
{
std::unique_ptr<DrawableSurface> loadSkinMaterialMap(const std::string &path)
{
    auto &loader = Toolkit::assets();
    auto image = loader.requestImage(path);
    auto normalized = loader.requestEstimated<AssetImage>("material:" + path, {image.dependency()}, [image] {
        const auto *input = image.get()->surface;
        auto *output = SDL_CreateSurface(input->w, input->h, SDL_PIXELFORMAT_ARGB8888);
        if (!output) throw std::runtime_error(SDL_GetError());
        auto result = std::make_shared<AssetImage>(output);
        for (int y = 0; y < input->h; ++y) {
            const auto *source = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(input->pixels) + y * input->pitch);
            auto *dest = reinterpret_cast<Uint32*>(static_cast<Uint8*>(output->pixels) + y * output->pitch);
            for (int x = 0; x < input->w; ++x) {
                const Uint32 id = (source[x] >> 16) & 255;
                dest[x] = 0xff000000u | (id << 16) | (id << 8) | id;
            }
        }
        return result;
    }, [image] { const auto *surface = image.get()->surface; return size_t(surface->pitch) * surface->h; });
    if (!loader.wait(normalized)) return nullptr;
    SDL_Surface *surface = nullptr;
    if (auto value = normalized.take()) surface = value->releaseSurface();
    else surface = SDL_DuplicateSurface(normalized.get()->surface);
    return surface ? std::make_unique<DrawableSurface>(surface, DrawableSurface::AdoptPixels{}) : nullptr;
}
bool skinMaterialShells(unsigned id)
{
    return id < SKIN_MATERIAL_COUNT && SkinMaterials[id].shells;
}
std::array<bool, 4> skinShellRegions(DrawableSurface &material)
{
    std::array<bool, 4> result{};
    auto *source = material.getSDLSurface();
    if (!source || source->w != 512 || source->h != 512) return result;
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> rgba(
        SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!rgba || !SDL_LockSurface(rgba.get())) return result;
    const auto *pixels = static_cast<const Uint8 *>(rgba->pixels);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x)
            if (skinMaterialShells(pixels[y*rgba->pitch + x*4]))
                result[(y >= 256 ? 2 : 0) + (x >= 256 ? 1 : 0)] = true;
    SDL_UnlockSurface(rgba.get());
    return result;
}
}

#ifdef HAVE_OPENGL
// The desktop compatibility context exposes the EXT framebuffer entry points.
#ifndef GLOB2_WEBGL2
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
#undef glGenRenderbuffers
#define glGenRenderbuffers glGenRenderbuffersEXT
#undef glDeleteRenderbuffers
#define glDeleteRenderbuffers glDeleteRenderbuffersEXT
#undef glBindRenderbuffer
#define glBindRenderbuffer glBindRenderbufferEXT
#undef glRenderbufferStorage
#define glRenderbufferStorage glRenderbufferStorageEXT
#undef glFramebufferRenderbuffer
#define glFramebufferRenderbuffer glFramebufferRenderbufferEXT
#endif
namespace GAGCore
{
namespace
{
constexpr unsigned TileSize = 128, AtlasSize = 2048;
constexpr unsigned Columns = AtlasSize / TileSize, SlotsPerPage = Columns * Columns;
constexpr unsigned MaxPages = 4, MaxSlots = SkinAtlasCache::Capacity;
constexpr float Padding = 1.25f;
GLuint compileShader(GLenum type, const std::string &text)
{
    const char *source = text.c_str();
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(shader); return 0; }
    return shader;
}
// Packed rest vertex: position, normal, UV, four joint indices, four weights.
constexpr unsigned RigVertexFloats = 18;
template <std::size_t N>
std::array<float, N * N> transpose(const std::array<float, N * N> &rowMajor)
{
	std::array<float, N * N> columnMajor;
	for (unsigned y = 0; y < N; ++y)
		for (unsigned x = 0; x < N; ++x)
			columnMajor[x * N + y] = rowMajor[y * N + x];
	return columnMajor;
}
GLuint linkSkinProgram(GLuint vertex, GLuint fragment, bool rig = false)
{
	const auto program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	if (rig)
	{
		const char *attributes[] = {"position", "surfaceNormal", "texcoord", "joints", "weights"};
		for (unsigned i = 0; i < 5; ++i)
			glBindAttribLocation(program, i, attributes[i]);
	}
	glLinkProgram(program);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (ok)
		return program;
	char log[2048]{};
	glGetProgramInfoLog(program, sizeof(log), nullptr, log);
	std::cerr << "Skin " << (rig ? "rig" : "mesh") << " shader link failed: " << log << '\n';
	glDeleteProgram(program);
	return 0;
}
struct SkinGLState
{
    // Texture unit 1 holds the material map during rasterization. Its binding
    // is restored explicitly on both paths; glState only tracks unit 0.
    GLint framebuffer, renderbuffer, program, arrayBuffer, elementBuffer, materialTexture;
#ifdef GLOB2_WEBGL2
    GLint vao, activeTexture, texture, viewport[4], depthFunction, scissorBox[4];
    GLfloat clearColor[4], clearDepth;
    GLboolean depthMask, colorMask[4], blend, depth, scissor, cull;
#endif
    SkinGLState()
    {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
#ifdef GLOB2_WEBGL2
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &materialTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        blend=glIsEnabled(GL_BLEND); depth=glIsEnabled(GL_DEPTH_TEST);
        scissor=glIsEnabled(GL_SCISSOR_TEST); cull=glIsEnabled(GL_CULL_FACE);
#else
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        // GL_TEXTURE_BIT covers per-unit bindings, but restore unit 1 explicitly
        // in case a driver's attribute stack omits it.
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &materialTexture);
        glActiveTexture(GL_TEXTURE0);
        glClientActiveTexture(GL_TEXTURE0);
#endif
    }
    ~SkinGLState()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glUseProgram(program);
#ifdef GLOB2_WEBGL2
        glBindVertexArray(vao);
#else
        glPopClientAttrib();
#endif
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBuffer);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, materialTexture);
#ifdef GLOB2_WEBGL2
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(activeTexture);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        glDepthFunc(depthFunction);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        glClearDepthf(clearDepth); glDepthMask(depthMask);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        auto enable=[](GLenum cap, GLboolean on){ if(on)glEnable(cap);else glDisable(cap); };
        enable(GL_BLEND,blend); enable(GL_DEPTH_TEST,depth);
        enable(GL_SCISSOR_TEST,scissor); enable(GL_CULL_FACE,cull);
#else
        glActiveTexture(GL_TEXTURE0);
        glPopAttrib();
#endif
    }
};
auto keyFor(const SkinMeshRequest &request)
{
    return SkinAtlasCache::key(request.mesh->identity, request.frame,
        request.texture->lifetimeIdentity(), request.texture->contentRevision(),
        request.material->lifetimeIdentity(), request.material->contentRevision(), request.region);
}
bool valid(const SkinMeshRequest &request)
{
	if (!request.mesh || !request.texture || !request.material || request.region > SkinRegionSwarm)
		return false;
	const auto &mesh = *request.mesh;
	const bool hasGeometry = mesh.model	 ? mesh.clip < mesh.model->clips().size()
							 : mesh.shapes ? mesh.clip < mesh.shapes->clips().size()
										   : !mesh.poses.empty();
	return mesh.identity && request.frame < mesh.frames && hasGeometry &&
		   request.texture->getSDLSurface() && request.material->getSDLSurface();
}
// colony-v2 quadrant offsets: worker, warrior / explorer, swarm.
void regionOffset(std::uint8_t region, float &u, float &v)
{
	u = (region & 1) ? 0.5f : 0.f;
    v = (region & 2) ? 0.5f : 0.f;
}
}
void GraphicContext::SkinResources::Uniforms::initialize(unsigned program)
{
	// Sampler units are invariant for the lifetime of this linked program.
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "paint"), 0);
	glUniform1i(glGetUniformLocation(program, "material"), 1);
	region = glGetUniformLocation(program, "region");
	bones = glGetUniformLocation(program, "bones[0]");
	view = glGetUniformLocation(program, "view");
	normalView = glGetUniformLocation(program, "normalView");
	shell = glGetUniformLocation(program, "shell");
	glUniform1f(glGetUniformLocation(program, "furLength"), SKIN_FUR_LENGTH);
	glUniform1f(glGetUniformLocation(program, "shellDepth"), SKIN_SHELL_DEPTH);
}

void GraphicContext::SkinResources::prepareRigShader()
{
	rigAttempted = true;
	// 32 mat4 bones, mat4 camera, mat3 normal camera, plus driver headroom.
	GLint available = 0;
#ifdef GLOB2_WEBGL2
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &available);
#else
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &available);
	available /= 4;
#endif
	const char *mode = std::getenv("GLOB2_SKIN_DEFORMATION");
	if (!fragment || available < 144 || (mode && std::strcmp(mode, "cpu") == 0))
		return;
#ifdef GLOB2_WEBGL2
	const std::string header =
		"#version 300 es\nprecision highp float;\n"
		"in vec3 position;in vec3 surfaceNormal;in vec4 texcoord;in vec4 joints;in vec4 weights;"
		"out vec2 uv;out vec2 detailUV;out vec3 normal;\n";
#else
	const std::string header =
		"#version 120\nvarying vec2 uv;varying vec2 detailUV;varying vec3 normal;\n"
		"#define position gl_Vertex.xyz\n#define surfaceNormal gl_Normal\n"
		"#define texcoord gl_MultiTexCoord0\n#define joints gl_MultiTexCoord1\n"
		"#define weights gl_MultiTexCoord2\n";
#endif
	const auto vertex = compileShader(GL_VERTEX_SHADER, header + SkinDeformationGLSL);
	if (!vertex)
		return;
	rigProgram = linkSkinProgram(vertex, fragment, true);
	glDeleteShader(vertex);
	if (rigProgram)
		rigUniforms.initialize(rigProgram);
}

const GraphicContext::SkinResources::RigBuffers &
GraphicContext::SkinResources::restBuffers(const SkinMesh &mesh)
{
	const auto &model = *mesh.model;
	if (const auto found = rigs.find(model.identity()); found != rigs.end())
		return found->second;

	// Allow room for the three animated models and static catalog. Bound independently
	// of pose/paint atlas entries, evicting the oldest model generation first.
	constexpr unsigned MaxModels = 16;
	if (rigs.size() >= MaxModels)
	{
		const auto oldest = rigs.begin();
#ifdef GLOB2_WEBGL2
		glDeleteVertexArrays(1, &oldest->second.vao);
#endif
		glDeleteBuffers(1, &oldest->second.vertices);
		glDeleteBuffers(1, &oldest->second.indices);
		rigs.erase(oldest);
	}
	RigBuffers buffers;
#ifdef GLOB2_WEBGL2
	glGenVertexArrays(1, &buffers.vao);
	glBindVertexArray(buffers.vao);
#endif
	glGenBuffers(1, &buffers.vertices);
	glGenBuffers(1, &buffers.indices);
	uploadScratch.resize(model.vertices() * RigVertexFloats);
	for (unsigned v = 0; v < model.vertices(); ++v)
	{
		auto *dest = uploadScratch.data() + v * RigVertexFloats;
		std::copy_n(model.rest().data() + v * 6, 6, dest);
		std::copy_n(model.uv().data() + v * 2, 2, dest + 6);
        std::copy_n((mesh.detailUV.empty() ? mesh.uv : mesh.detailUV).data() + v * 2, 2, dest + 8);
		for (unsigned j = 0; j < 4; ++j)
		{
			dest[10 + j] = model.influences()[v].bones[j];
			dest[14 + j] = model.influences()[v].weights[j];
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, buffers.vertices);
	glBufferData(GL_ARRAY_BUFFER, uploadScratch.size() * sizeof(float), uploadScratch.data(),
				 GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.indices);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices().size() * sizeof(std::uint32_t),
				 model.indices().data(), GL_STATIC_DRAW);
	return rigs.emplace(model.identity(), buffers).first->second;
}

void GraphicContext::SkinResources::bindRigGeometry(const RigBuffers &buffers)
{
#ifdef GLOB2_WEBGL2
	glBindVertexArray(buffers.vao);
#endif
	glBindBuffer(GL_ARRAY_BUFFER, buffers.vertices);
	constexpr unsigned stride = RigVertexFloats * sizeof(float);
#ifdef GLOB2_WEBGL2
	constexpr unsigned sizes[] = {3, 3, 4, 4, 4};
	constexpr unsigned offsets[] = {0, 3, 6, 10, 14};
	for (unsigned i = 0; i < 5; ++i)
	{
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride,
							  reinterpret_cast<void *>(offsets[i] * sizeof(float)));
	}
#else
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glVertexPointer(3, GL_FLOAT, stride, nullptr);
	glNormalPointer(GL_FLOAT, stride, reinterpret_cast<void *>(3 * sizeof(float)));
	// Compatibility GL consumes UV, joints and weights as texture coordinates.
	constexpr unsigned offsets[] = {6, 10, 14};
	for (unsigned i = 0; i < 3; ++i)
	{
		glClientActiveTexture(GL_TEXTURE0 + i);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(4, GL_FLOAT, stride,
						  reinterpret_cast<void *>(offsets[i] * sizeof(float)));
	}
	glClientActiveTexture(GL_TEXTURE0);
#endif
}

void GraphicContext::SkinResources::finishRigGeometry()
{
	// The following baked draw uses fewer arrays. SkinGLState restores the
	// caller's complete state when the atlas batch ends.
#ifdef GLOB2_WEBGL2
	for (unsigned i = 0; i < 5; ++i)
		glDisableVertexAttribArray(i);
#else
	for (unsigned i = 1; i < 3; ++i)
	{
		glClientActiveTexture(GL_TEXTURE0 + i);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	glClientActiveTexture(GL_TEXTURE0);
#endif
}

void GraphicContext::SkinResources::uploadRigPalette(const SkinMesh &mesh, unsigned sample)
{
	if (paletteIdentity == mesh.identity && paletteFrame == sample)
		return;
	SkinPalette palette;
	if (!mesh.model->paletteForFrame(mesh.clip, sample, palette))
		return; // valid() admitted the request, so the frame and clip exist
	std::array<float, 32 * 16> positions{};
	for (unsigned b = 0; b < palette.count; ++b)
	{
		const auto columnMajor = transpose<4>(palette.positions[b]);
		std::copy(columnMajor.begin(), columnMajor.end(), positions.begin() + b * 16);
	}
	glUniformMatrix4fv(rigUniforms.bones, palette.count, GL_FALSE, positions.data());
	const auto &clip = mesh.model->clips()[mesh.clip];
	const auto camera = transpose<4>(clip.modelToClip);
	const auto normals = transpose<3>(clip.normalToCamera);
	glUniformMatrix4fv(rigUniforms.view, 1, GL_FALSE, camera.data());
	glUniformMatrix3fv(rigUniforms.normalView, 1, GL_FALSE, normals.data());
	paletteIdentity = mesh.identity;
	paletteFrame = sample;
}

void GraphicContext::SkinResources::bindCpuGeometry(const SkinMesh &mesh, unsigned sample)
{
#ifdef GLOB2_WEBGL2
	glBindVertexArray(vao);
#endif
	if (meshIdentity != mesh.identity)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(std::uint32_t),
					 mesh.indices.data(), GL_STATIC_DRAW);
#ifndef GLOB2_WEBGL2
		glBindBuffer(GL_ARRAY_BUFFER, uv);
		uploadScratch.resize(mesh.vertices * 4);
        for (unsigned v = 0; v < mesh.vertices; ++v) {
            std::copy_n(mesh.uv.data() + v * 2, 2, uploadScratch.data() + v * 4);
            std::copy_n((mesh.detailUV.empty() ? mesh.uv : mesh.detailUV).data() + v * 2, 2, uploadScratch.data() + v * 4 + 2);
        }
        glBufferData(GL_ARRAY_BUFFER, uploadScratch.size() * sizeof(float), uploadScratch.data(), GL_STATIC_DRAW);
#endif
		meshIdentity = mesh.identity;
		frame = ~0u;
	}
	glBindBuffer(GL_ARRAY_BUFFER, poses);
	if (frame != sample)
	{
		const float *pose;
		if (mesh.model || mesh.shapes)
		{
			mesh.evaluate(sample, cpuPose);
			pose = cpuPose.data();
		}
		else
			pose = mesh.poses.data() + std::size_t(sample) * mesh.vertices * 6;
#ifdef GLOB2_WEBGL2
		// Emscripten's legacy VAO implementation supports a single vertex buffer.
		uploadScratch.resize(std::size_t(mesh.vertices) * 10);
		auto &vertices = uploadScratch;
		for (std::size_t v = 0; v < mesh.vertices; ++v)
		{
			std::copy_n(pose + v * 6, 6, vertices.data() + v * 10);
			std::copy_n(mesh.uv.data() + v * 2, 2, vertices.data() + v * 10 + 6);
            std::copy_n((mesh.detailUV.empty() ? mesh.uv : mesh.detailUV).data() + v * 2, 2, vertices.data() + v * 10 + 8);
		}
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(),
					 GL_STREAM_DRAW);
#else
		glBufferData(GL_ARRAY_BUFFER, mesh.vertices * 6 * sizeof(float), pose, GL_STREAM_DRAW);
#endif
		frame = sample;
	}
#ifdef GLOB2_WEBGL2
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), nullptr);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float),
						  reinterpret_cast<void *>(3 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float),
						  reinterpret_cast<void *>(6 * sizeof(float)));
#else
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), nullptr);
	glNormalPointer(GL_FLOAT, 6 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
	glBindBuffer(GL_ARRAY_BUFFER, uv);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(4, GL_FLOAT, 0, nullptr);
#endif
}

void GraphicContext::destroySkinRenderer()
{
	auto &r = skinResources;
	if (r.program)
		glDeleteProgram(r.program);
	if (r.rigProgram)
		glDeleteProgram(r.rigProgram);
	if (r.fragment)
		glDeleteShader(r.fragment);
	for (const auto &[identity, buffers] : r.rigs)
	{
#ifdef GLOB2_WEBGL2
		glDeleteVertexArrays(1, &buffers.vao);
#endif
		glDeleteBuffers(1, &buffers.vertices);
		glDeleteBuffers(1, &buffers.indices);
	}
	if (r.framebuffer)
		glDeleteFramebuffers(1, &r.framebuffer);
	if (r.depth)
		glDeleteRenderbuffers(1, &r.depth);
	for (auto color : r.colors)
		glDeleteTextures(1, &color);
#ifdef GLOB2_WEBGL2
	if (r.vao)
		glDeleteVertexArrays(1, &r.vao);
#endif
	for (auto buffer : {r.poses, r.uv, r.indices})
		if (buffer)
			glDeleteBuffers(1, &buffer);
	r = {};
}
void GraphicContext::prepareSkinMeshes(const std::vector<SkinMeshRequest> &requests)
{
	PERF_SCOPE_TIME(SkinPrepare);
	auto &r = skinResources;
	if (!context || renderer || requests.empty() || (r.attempted && !r.program))
		return;
#ifndef GLOB2_WEBGL2
	if (glState.isTextureSRectangle)
		return;
	const auto *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
	if (!extensions || !std::strstr(extensions, "GL_EXT_framebuffer_object"))
		return;
#endif
    // Sort by mesh and pose so team variants reuse geometry uploads. Wrapped map
    // copies and identical units share one live rasterization for this frame.
    std::map<SkinResources::Key, SkinMeshRequest> unique;
    for (const auto &request : requests)
        if (valid(request)) unique.emplace(keyFor(request), request);
    if (unique.empty()) return;
    // Keep the working set bounded; protect all visible hits before replacing
    // old tiles. Overflow draws take the same eviction path on demand.
    while (unique.size() > MaxSlots) unique.erase(std::prev(unique.end()));
    for (auto it = unique.begin(); it != unique.end(); )
        if (r.slots.touch(it->first)) it = unique.erase(it); else ++it;
    if (unique.empty()) return;
    Sprite::flushBatches(this);
    for (const auto &[key, request] : unique)
        for (auto *surface : {request.texture, request.material})
            if (surface->glUploadedRevision != surface->contentRevision())
                surface->uploadToTexture();
    SkinGLState saved;
    if (!r.attempted)
    {
        r.attempted = true;
        GLint limit = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (limit < static_cast<int>(AtlasSize)) return;
        // The body fills the tile inside its 1.25 padding; fur shells add an
        // offset that is already in NDC (SKIN_FUR_LENGTH, 3.2 px of the 128 px
        // tile) and sit slightly nearer so strands win the depth test.
        const std::string place =
            "gl_Position=vec4(position.xy/1.25+normalize(surfaceNormal).xy*shell*furLength,position.z-shell*shellDepth,1.0);";
        // Shell passes keep only strands; the body pass never discards.
        const std::string shade = "vec4 shaded=skinShadeAtlas(paint,material,region,normal,uv,detailUV,shell);if(shaded.a<0.5)discard;";
#ifdef GLOB2_WEBGL2
        const auto vertex = compileShader(GL_VERTEX_SHADER,
            "#version 300 es\nprecision highp float;\n"
            "layout(location=0) in vec3 position;layout(location=1) in vec3 surfaceNormal;layout(location=2) in vec4 texcoord;\n"
            "uniform float shell;uniform float furLength;uniform float shellDepth;\n"
            "out vec2 uv;out vec2 detailUV;out vec3 normal;void main(){uv=texcoord.xy;detailUV=texcoord.zw;normal=surfaceNormal;" + place + "}\n");
        const auto fragment = compileShader(GL_FRAGMENT_SHADER,
            std::string("#version 300 es\nprecision highp float;\n#define SKIN_TEXTURE texture\n")+
            "uniform sampler2D paint;uniform sampler2D material;uniform vec2 region;uniform float shell;in vec2 uv;in vec2 detailUV;in vec3 normal;out vec4 color;\n"
            + std::string(SkinMaterialGLSL) +
            "void main(){" + shade + "color=vec4(shaded.rgb,1.0);}\n");
#else
        const auto vertex = compileShader(GL_VERTEX_SHADER,
            "#version 120\nuniform float shell;uniform float furLength;uniform float shellDepth;varying vec2 uv;varying vec2 detailUV;varying vec3 normal;\n"
            "void main(){uv=gl_MultiTexCoord0.xy;detailUV=gl_MultiTexCoord0.zw;normal=gl_Normal;vec3 position=gl_Vertex.xyz;vec3 surfaceNormal=gl_Normal;" + place + "}\n");
        const auto fragment = compileShader(GL_FRAGMENT_SHADER,
            std::string("#version 120\n#define SKIN_TEXTURE texture2D\n")+
            "uniform sampler2D paint;uniform sampler2D material;uniform vec2 region;uniform float shell;varying vec2 uv;varying vec2 detailUV;varying vec3 normal;\n"
            + std::string(SkinMaterialGLSL) +
            "void main(){" + shade + "gl_FragColor=vec4(shaded.rgb,1.0);}\n");
#endif
		if (vertex && fragment)
			r.program = linkSkinProgram(vertex, fragment);
		if (vertex)
			glDeleteShader(vertex);
		r.fragment = fragment;
		if (!r.program)
			return;
		r.uniforms.initialize(r.program);
		glGenFramebuffers(1, &r.framebuffer);
		glGenRenderbuffers(1, &r.depth);
		glBindRenderbuffer(GL_RENDERBUFFER, r.depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, AtlasSize, AtlasSize);
		glGenBuffers(1, &r.poses);
		glGenBuffers(1, &r.indices);
#ifdef GLOB2_WEBGL2
		glGenVertexArrays(1, &r.vao);
#else
		glGenBuffers(1, &r.uv);
#endif
	}
	// Baked-only clients never compile or allocate rig resources.
	if (!r.rigAttempted && std::any_of(unique.begin(), unique.end(), [](const auto &entry)
									   { return bool(entry.second.mesh->model); }))
		r.prepareRigShader();
	const unsigned pages = std::min(
		MaxPages,
		static_cast<unsigned>((r.slots.size() + unique.size() + SlotsPerPage - 1) / SlotsPerPage));
	while (r.colors.size() < pages)
	{
		GLuint color = 0;
		glGenTextures(1, &color);
		glBindTexture(GL_TEXTURE_2D, color);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, AtlasSize, AtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
					 nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		r.colors.push_back(color);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, r.framebuffer);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r.depth);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 0);
#ifdef GLOB2_WEBGL2
	glClearDepthf(1);
	glBindVertexArray(r.vao);
#else
	glClearDepth(1);
#endif
	unsigned boundPage = ~0u;
	for (const auto &[key, request] : unique)
	{
		if (!request.texture->texture || !request.material->texture)
			continue;
		const unsigned slot = r.slots.reserve(key);
		const unsigned page = slot / SlotsPerPage;
		if (page != boundPage)
		{
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
								   r.colors[page], 0);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				destroySkinRenderer();
				skinResources.attempted = true;
				return;
			}
			boundPage = page;
		}
		// Preserve other cached poses, including the transparent tile border.
		const unsigned tile = slot % SlotsPerPage;
		glEnable(GL_SCISSOR_TEST);
		glScissor((tile % Columns) * TileSize, (tile / Columns) * TileSize, TileSize, TileSize);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		const auto &mesh = *request.mesh;
		PerformanceTelemetry::Scope geometryTime(PerformanceTelemetry::Id::SkinGeometry);
		const bool gpuRig = mesh.model && r.rigProgram;
		const auto program = gpuRig ? r.rigProgram : r.program;
		glUseProgram(program);
		unsigned indexBuffer = r.indices;
		if (gpuRig)
		{
			const auto &buffers = r.restBuffers(mesh);
			r.bindRigGeometry(buffers);
			r.uploadRigPalette(mesh, request.frame);
			indexBuffer = buffers.indices;
		}
		else
			r.bindCpuGeometry(mesh, request.frame);
		// Fur uses the same material-region cache for baked and rig geometry.
		const auto shellKey = std::make_pair(request.material->lifetimeIdentity(), request.material->contentRevision());
		auto shells = r.shellRegions.find(shellKey);
		if (shells == r.shellRegions.end())
		{
			if (r.shellRegions.size() >= 64) r.shellRegions.clear();
			shells = r.shellRegions.emplace(shellKey, skinShellRegions(*request.material)).first;
		}
		const unsigned passes = shells->second[request.region] ? SKIN_MATERIAL_SHELLS : 0;
		geometryTime.stop();
		PERF_SCOPE_TIME(SkinRaster);
		glViewport((tile % Columns) * TileSize, (tile / Columns) * TileSize, TileSize, TileSize);
		// Material ids must never blend: sample them unfiltered on unit 1.
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, request.material->texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, request.texture->texture);
		float regionU, regionV;
		regionOffset(request.region, regionU, regionV);
		const auto &uniforms = gpuRig ? r.rigUniforms : r.uniforms;
		glUniform2f(uniforms.region, regionU, regionV);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
		for (unsigned shell = 0; shell <= passes; ++shell)
		{
			glUniform1f(uniforms.shell, static_cast<float>(shell) / SKIN_MATERIAL_SHELLS);
			glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, nullptr);
			++drawCalls;
		}
		if (gpuRig)
			r.finishRigGeometry();
	}
}
bool GraphicContext::readSkinMesh(const SkinMeshRequest &request, std::vector<std::uint8_t> &rgba)
{
    if (!valid(request)) return false;
    prepareSkinMeshes({request});
    auto &r = skinResources;
    const auto found = r.slots.find(keyFor(request));
    if (!found) return false;
    SkinGLState saved;
    const unsigned slot = *found, tile = slot % SlotsPerPage;
    glBindFramebuffer(GL_FRAMEBUFFER, r.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.colors[slot/SlotsPerPage], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    std::vector<std::uint8_t> bottom(TileSize*TileSize*4);
    glReadPixels((tile%Columns)*TileSize, (tile/Columns)*TileSize, TileSize, TileSize,
                 GL_RGBA, GL_UNSIGNED_BYTE, bottom.data());
    rgba.resize(bottom.size());
    for (unsigned y=0;y<TileSize;++y) for (unsigned x=0;x<TileSize;++x) {
        const auto *in = bottom.data()+((TileSize-1-y)*TileSize+x)*4;
        auto *out = rgba.data()+(y*TileSize+x)*4;
        out[3]=in[3];
        for (unsigned c=0;c<3;++c) out[c]=in[3] ? std::min(255u,(unsigned(in[c])*255+in[3]/2)/in[3]) : 0;
    }
    return true;
}
bool GraphicContext::drawSkinMesh(const SkinMesh &mesh, unsigned frame, DrawableSurface &texture,
                                  DrawableSurface &material, std::uint8_t region,
                                  float x, float y, float w, float h, DrawableSurface *underlay, Uint8 alpha)
{
    PERF_SCOPE_TIME(SkinComposite);
    const SkinMeshRequest request{&mesh, frame, &texture, &material, region};
    if (!valid(request)) return false;
    if (alpha == 0) return true;
    const auto key = keyFor(request);
    auto &r = skinResources;
    auto found = r.slots.find(key);
    if (!found)
    {
        // Standalone and overflow draws evict one least-recently-used tile.
        prepareSkinMeshes({request});
        found = r.slots.find(key);
        if (!found) return false;
    }
    // Draw the original ground shadow only after confirming the mesh can
    // render. A failed mesh draw must leave the classic fallback untouched.
    if (underlay) drawSurface(x, y, w, h, underlay, alpha);
    Sprite::flushBatches(this);
    const unsigned slot = *found, tile = slot % SlotsPerPage;
    const float u = float(tile%Columns)/Columns, v = float(tile/Columns)/Columns;
    const float extent = 1.f/Columns;
    glUseProgram(0); glState.doTexture(true); glState.setTexture(r.colors[slot/SlotsPerPage]);
    glState.doBlend(true); glState.blendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    x-=w*(Padding-1)/2; y-=h*(Padding-1)/2; w*=Padding; h*=Padding;
    // The atlas uses premultiplied alpha: fade RGB together with alpha.
    const float opacity = alpha / 255.f;
    glBegin(GL_QUADS);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u,v+extent); glVertex2f(x,y);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u+extent,v+extent); glVertex2f(x+w,y);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u+extent,v); glVertex2f(x+w,y+h);
    glColor4f(opacity,opacity,opacity,opacity); glTexCoord2f(u,v); glVertex2f(x,y+h);
    glEnd(); glColor4f(1,1,1,1); glState.blendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); ++drawCalls;
    return true;
}
}
#else
namespace GAGCore
{
bool GraphicContext::readSkinMesh(const SkinMeshRequest &, std::vector<std::uint8_t> &) { return false; }
void GraphicContext::destroySkinRenderer() { skinResources = {}; }
void GraphicContext::prepareSkinMeshes(const std::vector<SkinMeshRequest> &) {}
bool GraphicContext::drawSkinMesh(const SkinMesh &, unsigned, DrawableSurface &, DrawableSurface &, std::uint8_t, float,float,float,float, DrawableSurface *, Uint8) { return false; }
}
#endif
