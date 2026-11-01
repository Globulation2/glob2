"""Opt-in GCC size experiments; normal release optimization stays unchanged."""

import shlex
import subprocess

PROFILES = ("none", "gc", "lto", "size")


def release_defaults(env):
    for flags in ("CXXFLAGS", "LINKFLAGS"):
        # LINKFLAGS can contain SCons Literal nodes (for example $ORIGIN
        # rpaths). Inspect their text without replacing the original nodes,
        # whose literal quoting must survive through command generation.
        if not any(str(flag).startswith("-O") for flag in env.Split(env[flags])):
            env.Append(**{flags: ["-O3"]})


def apply(env, profile):
    if profile == "none":
        return
    compiler = shlex.split(str(env["CXX"]))
    version = subprocess.check_output([*compiler, "--version"], text=True)
    if "Free Software Foundation" not in version:
        raise ValueError("size_optimization experiments currently require GCC/MinGW")
    env.Append(CCFLAGS=["-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=["-Wl,--gc-sections"])
    if profile in ("lto", "size"):
        env.Append(CCFLAGS=["-flto"])
        # GCC's linker plugin determines visibility; whole-program assumptions
        # would change callback/export semantics when LTO is introduced.
        env["LINKFLAGS"] = [
            flag for flag in env.Split(env["LINKFLAGS"]) if flag != "-fwhole-program"
        ]
        env.Append(LINKFLAGS=["-flto"])
    if profile == "size":
        env.Append(CXXFLAGS=["-Os"])
        env.Append(CFLAGS=["-Os"])
        env.Append(LINKFLAGS=["-Os"])
