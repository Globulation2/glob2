# Physical-phone text sharpness

Fix: 2ae1a0e84. Device: Samsung SM-A065M, Android 16, arm64.

The portable renderer returned text raster scale 1, ignoring drawable density and local UI magnification. The 13px in-game font was enlarged after rasterization. Main menus also benefited from the fix, though their larger authored fonts made the defect less obvious.

Screen glyphs now use drawable scale × UI transform. Layout metrics remain authored. Integer-size font/string caches retain alternating resolutions, and the first draw selects its correct raster. Offscreen surfaces keep logical resolution.

Actual device captures: before-menu.png vs after-menu.png; after-landscape.png; before-current.png vs after-main.png. Same saved game/menu, with changing live simulation behind it. APK installed in place; saves preserved.

Validation: GameGUITouchHarness passes on host and physical Android, including new first-draw resolution, stable metrics, alternating cache reuse and offscreen checks. Host PortableRendererHarness passes clipping, alpha, scaling, device reset, dirty textures and resize checks. Physical Activity inspected in portrait and landscape. Logs in this directory. No simulation or persistence changes.
