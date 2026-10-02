#include "AIMaximaDistanceField.h"
#include <ctime>
#include <iostream>
#include <vector>
#ifdef COMPACT
using Field=AIMaximaPlacement::DistanceField;
#else
using Field=std::vector<int>;
#endif
int main(){
 const int w=512,size=w*w;Field f;std::vector<int> q;q.reserve(size);
 auto bfs=[&]{f.assign(size,INT_MAX);q.clear();for(int i=0;i<size;i+=1009){f[i]=0;q.push_back(i);}for(size_t head=0;head<q.size();++head){int c=q[head],x=c%w,y=c/w;int n[4]={y*w+((x-1)&511),y*w+((x+1)&511),((y-1)&511)*w+x,((y+1)&511)*w+x};for(int d:n)if(f[d]==INT_MAX){f[d]=f[c]+1;q.push_back(d);}}};
 for(int i=0;i<8;++i)bfs();auto t=std::clock();for(int i=0;i<128;++i)bfs();double cpu=double(std::clock()-t)/CLOCKS_PER_SEC;uint64_t hash=1469598103934665603ull;for(int i=0;i<size;++i){hash^=int(f[i]);hash*=1099511628211ull;}std::cout<<cpu<<" "<<hash<<"\n";
}
