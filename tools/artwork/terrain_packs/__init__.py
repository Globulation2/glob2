# SPDX-License-Identifier: GPL-3.0-or-later
"""One module per terrain material; importing the package registers them all.

Each module defines one material with `@recipe` (terrain_core) and can be tuned
on its own: a material's provenance records its module, the family helpers it
imports (the `_`-prefixed modules) and the shared library, so changing one
material leaves every other material's recorded inputs unchanged.
"""
import importlib
import pkgutil

for _module in pkgutil.iter_modules(__path__):
    if not _module.name.startswith("_"):
        importlib.import_module(f"{__name__}.{_module.name}")
