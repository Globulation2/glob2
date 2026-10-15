from pathlib import Path
import subprocess
out=Path(__file__).parent
prefix=r'''
#include <array>
#include <list>
#include <cassert>
#include <iostream>
#include "src/common/Integrity.h"
#include "src/team/LiveSlotList.h"
struct Unit { static constexpr int MAX_COUNT=1024; bool valid=true; int calls=0; bool integrity(){++calls;return valid;} };
struct Type { bool isVirtual=true; };
struct Runtime { bool attractsRole(int)const{return true;} };
struct Building { static constexpr int MAX_COUNT=1024; bool valid=true; int calls=0; int gid=0; Type* type=nullptr; Runtime* runtime=nullptr; bool integrity(){++calls;return valid;} static int GIDtoID(int id){return id;} };
struct Team {
 int noMoreBuildingSitesCountdown=0; static constexpr int noMoreBuildingSitesCountdownMax=200;
 std::array<Unit*,1024> myUnits{};std::array<Building*,1024> myBuildings{};
 LiveSlotList<Unit> liveUnits;LiveSlotList<Building> liveBuildings;
 std::list<Building*> virtualBuildings,clearingFlags;
 bool integrity();
 void rebuild(){liveUnits.rebuild(myUnits.data(),1024);liveBuildings.rebuild(myBuildings.data(),1024);}
};
'''
suffix=r'''
struct Fixture {
 Team t; Unit a,b;Building x,y;
 Fixture(){ t.myUnits[0]=&a;t.myUnits[3]=&b;t.myBuildings[0]=&x;t.myBuildings[3]=&y;t.rebuild();assert(t.integrity()); }
};
int main(){
 int passed=0;
 {Team t;assert(t.integrity());++passed;}
 {Fixture f;f.t.myUnits[1023]=&f.b;f.t.myUnits[3]=nullptr;f.t.myBuildings[1023]=&f.y;f.t.myBuildings[3]=nullptr;f.t.rebuild();assert(f.t.integrity());++passed;}
 {Team t;std::array<Unit,1024> u;std::array<Building,1024>b;for(int i=0;i<1024;++i){t.myUnits[i]=&u[i];t.myBuildings[i]=&b[i];}t.rebuild();assert(t.integrity());for(int i=0;i<1024;++i){assert(u[i].calls==1);assert(b[i].calls==1);}++passed;}
 for(int k=0;k<8;++k){Fixture f;switch(k){
 case 0:f.t.liveUnits.clear();break;
 case 1:f.t.liveUnits.detach(0);f.t.liveUnits.attach(1,&f.a);break;
 case 2:f.t.liveUnits.attach(0,&f.b);break;
 case 3:f.t.liveUnits.attach(1023,&f.a);break;
 case 4:f.t.liveBuildings.clear();break;
 case 5:f.t.liveBuildings.detach(0);f.t.liveBuildings.attach(1,&f.x);break;
 case 6:f.t.liveBuildings.attach(0,&f.y);break;
 case 7:f.t.liveBuildings.attach(1023,&f.x);break;
 }assert(!f.t.integrity());f.t.rebuild();assert(f.t.integrity());++passed;}
 {Fixture f;f.a.valid=false;assert(!f.t.integrity());++passed;}
 {Fixture f;f.x.valid=false;assert(!f.t.integrity());++passed;}
 std::cout<<passed<<" focused integrity scenarios passed\n";
}
'''
for name,source in [('baseline',subprocess.check_output(['git','show','HEAD^:src/team/Team.cpp'],text=True)),('candidate',Path('src/team/Team.cpp').read_text())]:
 start=source.index('bool Team::integrity(void)');begin=source.index('{',start);level=1;i=begin+1
 while level:
  if source[i]=='{':level+=1
  elif source[i]=='}':level-=1
  i+=1
 code=prefix+source[start:i]+suffix
 cpp=out/(name+'-harness.cpp');cpp.write_text(code)
 exe=out/(name+'-harness')
 cmd=['clang++','-std=c++20','-O0','-g0','-I.',str(cpp),'-o',str(exe)]
 r=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'-harness-build.log')).write_text(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(exe)],capture_output=True,text=True);(out/(name+'-harness.log')).write_text(r.stdout+r.stderr);print(name,r.stdout.strip());r.check_returncode()
