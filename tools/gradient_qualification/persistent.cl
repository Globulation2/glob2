// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only: exactly ONE work-group owns one field. Every queue epoch
// has uniformly reached global/local barriers. Relaxed legacy atomics provide
// only within-group maxima/deduplication; they do not publish queue payloads.
// metadata: status (0 complete, 1 bounded, 2 invalid), rounds, pops, highwater,
// updates, initial count, remaining frontier, error bits.
__kernel void persistent(__global const ushort *seeds,__global uint *value,
 __global const uint *cost,__global uint *first,__global uint *second,
 __global uint *stamp,__global uint *metadata,uint w,uint h,uint cap,
 uint popLimit,uint epochLimit)
{
 const uint lid=get_local_id(0),threads=get_local_size(0),cells=w*h;
 __local uint counts[2],slot,active,remaining,rounds,pops,highwater,updates,initial,done,errors;
 if(lid==0){counts[0]=counts[1]=0;slot=0;remaining=popLimit;rounds=pops=highwater=updates=0;done=errors=0;}
 for(uint i=lid;i<cells;i+=threads){value[i]=seeds[i];stamp[i]=0;}
 barrier(CLK_GLOBAL_MEM_FENCE|CLK_LOCAL_MEM_FENCE);
 for(uint i=lid;i<cells;i+=threads)if(seeds[i]>1){
  const uint position=atomic_inc(counts);first[position]=i;
 }
 barrier(CLK_GLOBAL_MEM_FENCE|CLK_LOCAL_MEM_FENCE);
 if(lid==0)initial=counts[0];
 barrier(CLK_LOCAL_MEM_FENCE);
 for(uint epoch=1;epoch<=epochLimit;++epoch){
  if(lid==0){
   active=counts[slot];highwater=max(highwater,active);counts[1u-slot]=0;
   if(active>cells || errors)done=3;
   else if(!active)done=1;
   else if(active>remaining)done=2;
  }
  barrier(CLK_GLOBAL_MEM_FENCE|CLK_LOCAL_MEM_FENCE);
  if(done)break; // Shared uniform condition. No work-item exits alone.
  __global uint *current=slot ? second : first,*next=slot ? first : second;
  const int threshold=max(2,65535-(int)cap);
  for(uint q=lid;q<active;q+=threads){
   const uint i=current[q];
   if(i>=cells){atomic_or(&errors,1u);continue;}
   const uint source=atomic_or(value+i,0u),packed=cost[i];
   if(source<=1)continue;
   const int x=i%w,y=i/w;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy){
    const uint j=((y+dy+(int)h)%h)*w+(x+dx+(int)w)%w;
    const uint step=(dx&&dy)?packed>>16:packed&65535;
    if(!step){atomic_or(&errors,2u);continue;}
    const int candidate=(int)source-(int)step;
    if(candidate<threshold || !atomic_or(value+j,0u))continue;
    const uint previous=atomic_max(value+j,(uint)candidate);
    if((uint)candidate>previous){
     atomic_inc(&updates);
     if(atomic_xchg(stamp+j,epoch)!=epoch){
      const uint position=atomic_inc(counts+(1u-slot));
      if(position<cells)next[position]=j;else atomic_or(&errors,4u);
     }
    }
   }
  }
  // All ordinary queue writes are published BEFORE reading their count or
  // consuming their payload on the next epoch, within this same work-group.
  barrier(CLK_GLOBAL_MEM_FENCE|CLK_LOCAL_MEM_FENCE);
  if(lid==0){pops+=active;remaining-=active;++rounds;slot=1u-slot;}
  barrier(CLK_LOCAL_MEM_FENCE);
 }
 if(lid==0){
  const uint frontier=counts[slot];
  metadata[0]=errors || frontier>cells ? 2u : frontier ? 1u : 0u;
  metadata[1]=rounds;metadata[2]=pops;metadata[3]=max(highwater,frontier);metadata[4]=updates;
  metadata[5]=initial;metadata[6]=frontier;metadata[7]=errors;
 }
}
