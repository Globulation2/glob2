#include "MusicStream.h"
#include <fstream>
int main(int argc, char **argv) {
  Music::Preview preview;
  std::string root = argv[1];
  if (!preview.open({root+"/a1.opus",root+"/a2.opus",root+"/a3.opus"})) return 1;
  preview.playing=true;
  std::ofstream out(argv[2],std::ios::binary);
  std::int16_t pcm[960];
  for (unsigned chunk=0;chunk<3600;chunk++) {
    if (chunk % 600 == 0) preview.setMood((chunk / 600) % 3);
    preview.render(pcm,480);
    if (preview.failed()) return 2;
    out.write(reinterpret_cast<const char *>(pcm),sizeof(pcm));
  }
}
