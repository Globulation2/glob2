// SPDX-License-Identifier: GPL-3.0-or-later
// Independent heap Dijkstra: no production traversal or GPU code.
#include <algorithm>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>
extern "C" void oracle(const uint16_t* seeds,const uint32_t* costs,uint16_t* out,int w,int h,int cap) {
 std::copy(seeds,seeds+w*h,out);
 using Item=std::pair<int,int>;
 std::priority_queue<Item,std::vector<Item>,std::greater<Item>> queue;
 for(int i=0;i<w*h;i++)if(out[i]>1)queue.push({65535-out[i],i});
 while(!queue.empty()) {
  auto [dist,i]=queue.top();queue.pop();if(dist!=65535-out[i])continue;
  int x=i%w,y=i/w;
  for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(dx||dy){
   int j=((y+dy+h)%h)*w+(x+dx+w)%w;
   int step=(dx&&dy)?costs[i]>>16:costs[i]&65535,nd=dist+step;
   if(out[j]&&nd<=cap&&nd<65534&&nd<65535-out[j]){out[j]=65535-nd;queue.push({nd,j});}
  }
 }
}
