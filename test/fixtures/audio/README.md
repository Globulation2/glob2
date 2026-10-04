Audio decoder fixtures use synthetic sine waves, not game music.

- `trimmed.opus`: 4813 stereo frames at 48 kHz, 440 Hz left and 660 Hz right,
  encoded with `tools/encode_music.py`. Its length exercises pre-skip and an end
  trim that falls inside an Opus packet.
- `damaged-packet.opus`: the same container with the second audio packet's first
  two bytes replaced by `ff 3f` and its Ogg page CRC recalculated. It remains
  openable and exercises playback of damaged data.
- `unsupported-vorbis.ogg`: the same synthetic PCM encoded as Vorbis, used solely
  to check that the runtime rejects the retired codec. It is not a runtime asset.
