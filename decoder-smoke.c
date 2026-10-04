#include <opusfile.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "/audio";
    char path[1024]; int error; opus_int16 pcm[2048];
    snprintf(path, sizeof(path), "%s/trimmed.opus", root);
    OggOpusFile *track = op_open_file(path, &error);
    if (!track || op_pcm_total(track, -1) != 4813 || op_pcm_seek(track, 4800)) return 1;
    int frames = 0, n;
    while ((n = op_read_stereo(track, pcm, 2048)) > 0) frames += n;
    if (frames != 13 || n < 0) return 2;
    if (op_pcm_seek(track, 0)) return 3;
    frames = 0;
    while ((n = op_read_stereo(track, pcm, 2048)) > 0) frames += n;
    if (frames != 4813 || n < 0) return 4;
    op_free(track);
    snprintf(path, sizeof(path), "%s/unsupported-vorbis.ogg", root);
    track = op_open_file(path, &error);
    if (track) return 5;
    snprintf(path, sizeof(path), "%s/damaged-packet.opus", root);
    track = op_open_file(path, &error);
    if (!track) return 6;
    for (int i = 0; i < 100; ++i) {
        n = op_read_stereo(track, pcm, 2048);
        if (n == 0) { op_free(track); puts("PASS trimmed lengths, seeking, retired codec, damaged packet"); return 0; }
    }
    op_free(track); return 7;
}
