"""Install a private Linux decoder once, preserving its ELF soname aliases."""

from pathlib import Path


def install_runtime(env, prefix):
    library = Path(prefix) / "lib"
    directory = Path(env["BINDIR"]).parent / "lib/glob2"
    sources = sorted(library.glob("libSDL2_image.so*"))
    if not sources:
        raise ValueError("Lean image prefix contains no Linux SDL_image runtime")
    real = {path.resolve() for path in sources}
    if len(real) != 1:
        raise ValueError("Lean image prefix contains ambiguous runtime versions")
    canonical = next(iter(real))
    installed = env.Install(str(directory), str(canonical))

    def alias(target, source, env):
        path = Path(target[0].abspath)
        path.unlink(missing_ok=True)
        path.symlink_to(Path(source[0].abspath).name)
        return 0

    aliases = [
        env.Command(str(directory / path.name), installed, alias)
        for path in sources
        if path.name != canonical.name
    ]
    env.Alias("install", [installed, *aliases])
