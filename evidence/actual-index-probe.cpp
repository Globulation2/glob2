#include "/home/bradley/.codex/worktrees/307ed29a-915d-4451-b89a-dd866c5cb3af/glob2/artifacts/ci-recovery/android-index-repair/platforms/mobile/AssetIndex.h"
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc,char**argv){if(argc!=2)return 2;auto*s=SDL_IOFromFile(argv[1],"rb");if(!s)return 3;auto bytes=MobileAssetIndex::read(s);SDL_CloseIO(s);std::ifstream f(argv[1],std::ios::binary);std::string expected((std::istreambuf_iterator<char>(f)),{});if(bytes!=expected)return 4;std::cout<<bytes.size()<<" bytes match the complete packaged index\n";}
