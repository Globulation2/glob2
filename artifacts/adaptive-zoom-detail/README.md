# Adaptive zoom detail: review evidence

Before/after captures for the adaptive zoom detail pull request. "Before" is the
same build with `PROFILE_ADAPTIVE_ZOOM=0` (uniform scaling); "after" has it on.
Software renderer at 1280x800 unless the name says OpenGL.

| File | Shows |
| --- | --- |
| 01-zoom-500.jpg | Bars and zone outlines no longer scale 5x at the new 500% cap |
| 02-zoom-50-bars.jpg | Only problem bars remain at 50% (3x magnified detail) |
| 03-zoom-35.jpg | Zone pattern fading to tint; fog-shade seam grid gone |
| 04-zoom-25.jpg | Flat terrain overview fading in; zones as tint |
| 05-zoom-14-minimum.jpg | Strategic view at the 256x256 minimum zoom |
| 06-zoom-18-eleven-teams.jpg | Eleven-team game, 1,359 units |
| 07-zoom-7-512-map.jpg | 512x512 map at its minimum zoom |
| 08-opengl-minimum.jpg | OpenGL renderer, high-density display |
| 09-icons.png | The icon set on four team colours at three sizes |
