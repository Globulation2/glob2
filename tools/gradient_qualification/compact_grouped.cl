// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only compact vertex worklist with device-resident queue counts.
// Separate input/output lists and in-order kernel boundaries publish every
// atomic relaxation before the next dispatch; no global spin barrier is used.
// metadata: two uint counts, then ulong processed/highwater/empty-rounds,
// followed by an atomic uint error flag and one reserved uint.
__kernel void compact_grouped(__global uint *value,__global const uint *cost,
 __global const uint *current,__global uint *next,__global uint *stamp,
 __global uint *metadata,uint currentSlot,uint epoch,uint w,uint h,uint cap)
{
 const uint count=metadata[currentSlot],cells=w*h;
 if(get_global_id(0)==0){
  __global ulong *statistics=(__global ulong *)(metadata+2);
  statistics[0]+=(ulong)count;
  statistics[1]=max(statistics[1],(ulong)count);
  statistics[2]+=(ulong)(count==0);
  if(count>cells)atomic_or(metadata+8,1u);
 }
 if(count>cells)return;
 const int threshold=max(2,65535-(int)cap);
 // A fixed bounded grid processes the compact input list in strides, without
 // a host count read or a launch covering every field cell. Extra work-items
 // simply return when the frontier is smaller than the launch.
 for(size_t q=get_global_id(0);q<count;q+=get_global_size(0)){
  const uint i=current[q];
  if(i>=cells){atomic_or(metadata+8,2u);continue;}
  const uint source=atomic_or(value+i,0u);if(source<=1)continue;
  const int x=i%w,y=i/w;const uint packed=cost[i];
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy){
   const uint j=((y+dy+(int)h)%h)*w+(x+dx+(int)w)%w;
   const uint step=(dx&&dy)?packed>>16:packed&65535;
   const int candidate=(int)source-(int)step;
   if(candidate<threshold || !atomic_or(value+j,0u))continue;
   const uint previous=atomic_max(value+j,(uint)candidate);
   // All overlapping value accesses are atomic. One enqueue per destination
   // and epoch bounds next[] to cells entries. A later improvement still
   // survives because the following dispatch reads the latest atomic value.
   if((uint)candidate>previous && atomic_xchg(stamp+j,epoch)!=epoch){
    const uint position=atomic_inc(metadata+(1u-currentSlot));
    if(position<cells)next[position]=j;
    else atomic_or(metadata+8,4u);
   }
  }
 }
}
