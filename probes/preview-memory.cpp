#include "MusicStream.h"
#include <sys/resource.h>
#include <iostream>
int main(int argc,char**argv){if(argc!=2)return 2;std::string p=argv[1];Music::Preview m;if(!m.open({p+"/a1.opus",p+"/a2.opus",p+"/a3.opus"}))return 3;m.playing=true;std::array<std::int16_t,2048>b{};for(int i=0;i<int(m.duration()*48000);i+=1024)m.render(b.data(),1024);rusage u{};getrusage(RUSAGE_SELF,&u);std::cout<<"{\"seconds\":"<<m.duration()<<",\"peakRssKiB\":"<<u.ru_maxrss<<",\"failed\":"<<m.failed()<<"}\n";return m.failed();}
