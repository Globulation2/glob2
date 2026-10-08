// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <AssetLoader.h>

namespace GAGCore
{
class SkinModel;
class SkinShapeModel;
class StreamBackend;
class DrawableSurface;
// Every unit clip holds 256 frames: eight headings of 32 animation phases, in
// the order unitAnimationFrame() produces.
constexpr unsigned SkinClipFrames = 256;
// Renderer adapter over the three unit mesh formats. GSK1 stores every posed
// vertex; GSR1 is a bone rig and GSB1 a blend-shape clip, both evaluated into
// the same per-pose layout on request. Positions are in orthographic clip
// space with camera-space normals. Loading is transactional and bounded,
// including failure.
struct SkinMesh
{
	std::uint64_t identity = 0; // fresh on every successful load
	std::uint32_t vertices = 0, frames = 0, logicalSize = 0;
	std::vector<float> uv;
	// Optional GUV1 sidecar: procedural detail can use a clean unwrap while
	// saved paint keeps the established atlas coordinates. Empty uses uv.
	std::vector<float> detailUV;
	std::vector<std::uint32_t> indices;
	std::vector<float> poses; // GSK1 only: xyz, normal xyz; frame-major
	// GSR1 and GSB1 meshes keep their immutable model and the chosen clip
	// instead of poses; several clip views may share one model. Identity
	// includes the chosen clip.
	std::shared_ptr<const SkinModel> model;
	std::shared_ptr<const SkinShapeModel> shapes;
	unsigned clip = 0;
	// Views of one clip of a model; frame selection stays in the draw request.
	static SkinMesh fromModel(std::shared_ptr<const SkinModel> model, unsigned clip);
	static SkinMesh fromShapes(std::shared_ptr<const SkinShapeModel> shapes, unsigned clip);
	// Writes xyz, normal xyz per vertex for one frame into caller storage,
	// evaluating rigs and blend shapes or copying a baked pose. Invalid
	// requests fail without modifying output.
	bool evaluate(unsigned frame, std::vector<float> &output) const;
	// Cosmetic camera rotation; returns a separately identified static mesh.
	SkinMesh rotatedView(unsigned angle, const std::array<float, 16> &inverse,
						 const std::array<float, 16> &projection,
						 const std::array<float, 9> &normals) const;
	bool load(const std::string &path, std::string &error);
	bool load(StreamBackend &input, std::string &error);
	bool loadDetailUV(StreamBackend &input, std::string &error);
};
AssetLoader::Handle<SkinMesh> requestSkinMesh(AssetLoader &loader, const std::string &path);
// Unit clips animate from fitted rigs: GSB1 blend shapes for workers and
// warriors, the GSR1 bone rig for the explorer; every other clip is baked.
// GLOB2_SKIN_RIGS=0 selects the baked GSK1 clips for comparison in gameplay
// and the preview tools; sprite publishing always renders the rigs.
bool skinRigsDisabled();
std::string skinClipFile(const std::string &clip, bool baked = false);
// colony-v2 skins share one 512x512 colour atlas and one material-id map per
// team; each model's UVs map into its own quadrant (see SkinRegion).
enum SkinRegion : std::uint8_t
{
	SkinRegionWorker = 0,
	SkinRegionWarrior = 1,
	SkinRegionExplorer = 2,
	SkinRegionSwarm = 3
};
// Loads a colony-v2 material-id map. Each texel's id (0..255) is written as
// an opaque grey (r=g=b=id) so the GPU sampler reads it back exactly.
// Greyscale PNGs decode to INDEX8 surfaces whose generated palette is not an
// exact identity ramp in every SDL_image build, so grey-ramp palettes use the
// pixel index itself. Returns null when the file is missing or undecodable.
std::unique_ptr<DrawableSurface> loadSkinMaterialMap(const std::string &path);
// Whether a material id grows fur shells (libgag/shaders/skin-materials.json).
bool skinMaterialShells(unsigned id);
// Which quadrants of a 512x512 material map contain a fur material.
std::array<bool, 4> skinShellRegions(DrawableSurface &material);
struct SkinMeshRequest
{
	const SkinMesh *mesh;
	unsigned frame;
	DrawableSurface *texture;  // colour atlas
	DrawableSurface *material; // material ids, see libgag/shaders/skin-materials.json
	std::uint8_t region;	   // SkinRegion: the atlas quadrant this mesh samples
};
} // namespace GAGCore
