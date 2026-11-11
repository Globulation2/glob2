"""Object compilation shared by the native, browser and mobile SCons builds."""
from build_timing import record


def unique(nodes):
    from SCons.Script import Flatten
    return list(dict.fromkeys(Flatten(list(nodes))))


def objects(env, sources, target, shared=False):
    """Return a source -> object node mapping.

    target(source) gives each object's path, so callers keep their existing
    object layout.
    """
    mapping = {}
    for source in sources:
        builder = env.SharedObject if shared else env.Object
        node = builder(target(source), source)[0]
        record(env, [node], "compile")
        mapping[source] = node
    return mapping
