"""Build identity for presentation-only skin derivatives."""
import hashlib
import json
from pathlib import Path
from SCons.Scanner import Scanner


def configure(env):
    root = Path(env.Dir('#').abspath)
    inputs = [root / 'tools/image_encoding.json',
              root / 'libgag/src/GraphicContextSkinMesh.cpp',
              root / 'src/app/cli/RenderSkin.cpp',
              root / 'src/online/SkinSpriteManifest.cpp',
              root / 'src/online/SkinSpriteManifest.h',
              root / 'libgag/src/SkinMesh.cpp',
              root / 'libgag/include/SkinMesh.h',
              root / 'libgag/include/SkinModel.h',
              root / 'libgag/src/SkinModel.cpp',
              root / 'libgag/include/SkinDeformation.h',
              root / 'src/online/SkinViewTransforms.h',
              root / 'src/online/SwarmMeshCatalog.h',
              root / 'src/unit/render/UnitAnimation.h',
              root / 'src/unit/render/ColonySkinPreview.cpp',
              root / 'libgag/shaders/skin-materials.json',
              root / 'libgag/shaders/skin-material.glsl']
    for pattern in ('*.gsk', '*.gsr', '*.view.json'):
        inputs += sorted((root / 'data/skins/colony-v1').glob(pattern))

    def generate(target, source, env):
        recipe = json.loads(inputs[0].read_text())
        if recipe['exact'] is not True or recipe['alphaQuality'] != 100:
            raise ValueError('Skin sheets require exact alpha')
        version = [int(part) for part in recipe['webpVersion'].split('.')]
        encoder_version = (version[0] << 16) | (version[1] << 8) | version[2]
        digest = hashlib.sha256()
        for path in inputs:
            digest.update(path.relative_to(root).as_posix().encode())
            digest.update(path.read_bytes())
        Path(str(target[0])).write_text(
            '#pragma once\n#define SKIN_RENDER_REVISION "' + digest.hexdigest() + '"\n' +
            '#define SKIN_WEBP_QUALITY ' + str(recipe['quality']) + '\n' +
            '#define SKIN_WEBP_METHOD ' + str(recipe['method']) + '\n' +
            '#define SKIN_WEBP_LOSSLESS_QUALITY ' + str(recipe['losslessQuality']) + '\n' +
            '#define SKIN_WEBP_LOSSLESS_METHOD ' + str(recipe['losslessMethod']) + '\n' +
            '#define SKIN_WEBP_ENCODER_VERSION ' + str(encoder_version) + '\n' +
            '#define SKIN_WEBP_VERSION "' + recipe['webpVersion'] + '"\n')
        return 0

    header = env.Command('SkinRenderRecipe.h', [str(p) for p in inputs], generate,
                         source_scanner=Scanner(lambda node, env, path: []))
    env.Append(CPPPATH=[env.Dir('.').abspath])
    env.Depends(env.Object('app/cli/RenderSkin.cpp'), header)
