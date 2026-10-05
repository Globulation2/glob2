import pathlib, subprocess
old=subprocess.check_output(['git','show','HEAD:src/audio/SoundMixer.cpp'],text=True)
read=old[old.index('static void readMusic('):old.index('void SoundMixer::handleVoiceInsertion')]
mix=old[old.index('void mixaudio('):old.index('static void SDLCALL streamAudio')]
header='''#include "MusicProducer.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <cassert>
#include <chrono>
#include <cstdlib>
using Sint16=std::int16_t; using Uint8=unsigned char;
struct Legacy : Music::Producer {unsigned musicVolume=255,voiceVolume=255; void handleVoiceInsertion(int*,int) {}};
#define FADE_SAMPLE_COUNT Music::Producer::FadeSampleCount
#define INTERPOLATION_RANGE 65535
#define INTERPOLATION_BITS 16
'''
main='''
int main() {
  unsigned compared=0;long long legacyUs=0,newUs=0;
  for(auto mode : {Music::Producer::MODE_NORMAL,Music::Producer::MODE_START,Music::Producer::MODE_STOP,Music::Producer::MODE_EARLY_CHANGE}) {
    Legacy old;Music::Producer next;
    for(auto* p : {static_cast<Music::Producer*>(&old),&next}) {
      p->load("data/zik/original/a1.opus",0);p->load("data/zik/original/a2.opus",1);p->load("data/zik/original/a3.opus",2);
      p->actTrack=0;p->nextTrack=mode==Music::Producer::MODE_EARLY_CHANGE?1:0;p->mode=mode;
      if(mode==Music::Producer::MODE_EARLY_CHANGE)p->pendingTrack=2;
    }
    std::array<Sint16,2048> a,b;
    for(unsigned n=0;n<2600 && old.mode!=Music::Producer::MODE_STOPPED;n++) {
      auto t=std::chrono::steady_clock::now();mixaudio(&old,reinterpret_cast<Uint8*>(a.data()),4096);
      auto u=std::chrono::steady_clock::now();next.render(b.data(),1024);auto v=std::chrono::steady_clock::now();
      legacyUs+=std::chrono::duration_cast<std::chrono::microseconds>(u-t).count();newUs+=std::chrono::duration_cast<std::chrono::microseconds>(v-u).count();
      if(a!=b){std::cerr<<"PCM mismatch mode="<<mode<<" block="<<n<<"\\n";return 1;}++compared;
    }
  }
  std::cout<<"Legacy/new exact PCM: "<<compared<<" stereo blocks; legacy render us="<<legacyUs<<"; producer us="<<newUs<<"\\n";
}
'''
pathlib.Path('artifacts/audio/compare-legacy.cpp').write_text(header+(read+mix).replace('SoundMixer','Legacy')+main)
