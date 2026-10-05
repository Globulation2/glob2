# Mobile skin recipe platform guard verification

Final tested head6efc34db31c7ae5ff03daff7e0c79103268545b0; base42c7c07a51efe62bceebb36c425416704e8bc5a1. Fresh master fetched before final checks still the same base. Original hosted master run37271070460 jobs111640754649/111640754639/111640754590 fail ARMv7/ARM64/x86_64 respectively.

Cause: scons/mobile_build.py defines GLOB2_MOBILE only in generated glob2/BuildConfig.h, while RenderSkin.cpp tested its desktop-only recipe-header guard before reading that header. All Android ABIs incorrectly require absent SkinRenderRecipe.h. Move the existing configuration include before the guard; no source logic or rendering changes.

Linux x86_64 Ubuntu26.04.1; actual pinned Android NDK28.2.13676358 Clang compilers with API24 sysroots and libc++ for all three supported ABIs. Headers are the pinned SDL3.4.16-family native SDK (platform selection by actual Android compiler), repository nlohmann/libgag headers. Mobile BuildConfig matches current scons/mobile_build.py emitted configuration, PACKAGE_VERSION0.10.0.1. Web configuration matches scons/web_build.py with fresh pinned Emscripten4.0.15 WASM SDL SDK headers. Full mobile vcpkg library linking is not claimed. Native GCC15.2.0 with actual native generated configuration and desktop SkinRenderRecipe.

Before-command.json invokes actual ARMv7 NDK compiler against the original translation unit with HAVE_CONFIG_H and mobile-generated configuration. FAIL exact missing SkinRenderRecipe.h; before.log retained. Final object-commands.json contains exact real compile commands and exit codes for ARMv7, ARM64, x86_64 (-std=gnu++20 -fexceptions -fPIC -DHAVE_CONFIG_H -O2 -c): all exit0. Output ELF architectures checked: EABI5 ARM32, ARM aarch64 ELF64, x86-64 ELF64.

Native desktop command in native-command.json: object compile PASS exit0. Original source compiled with otherwise identical native inputs also PASS; native-before.o and native-render-skin.o are byte-for-byte identical. SHA256 in object-checksums.json. This directly confirms unchanged desktop CLI/render code generation rather than claiming broad engine execution.

Serial and pthread WASM translation unit commands in wasm-commands.json: actual Emscripten compiler, generated web config and pinned WASM SDK, both -c exit0. Logs retained; no full browser link/startup claim.

Coverage focuses the changed platform-selection boundary: complete actual translation unit compilation on all Android ABIs, native desktop code identity, serial/threaded WASM compilation. Full APK link/package/sign/install, iOS SDK execution, full native/browser runtime matrix and simulation comparisons omitted; no function body, simulation/save/replay/network or game-feel changes. Author accepts focused final-head evidence under AGENTS.md; next full hosted master builds confirm APK integration asynchronously.
