// SPDX-License-Identifier: GPL-3.0-or-later
// Development-only exact vertex worklist. The ordered kernel boundary makes
// appended indices visible before the next dispatch. All distance and stamp
// accesses that overlap within a dispatch are atomic. One stamp per epoch
// bounds the next list to cells entries without clearing a full activity plane.
__kernel void compact(__global uint *value,__global const uint *cost,
 __global const uint *current,__global uint *next,__global uint *stamp,
 __global uint *count,uint currentCount,uint epoch,uint w,uint h,uint cap)
{
 uint q=get_global_id(0);if(q>=currentCount)return;
 uint i=current[q],source=atomic_or(value+i,0u);if(source<=1)return;
 int x=i%w,y=i/w;uint packed=cost[i];
 int threshold=max(2,65535-(int)cap);
 for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy){
  uint j=((y+dy+(int)h)%h)*w+(x+dx+(int)w)%w;
  uint step=(dx&&dy)?packed>>16:packed&65535;
  int candidate=(int)source-(int)step;
  if(candidate<threshold || !atomic_or(value+j,0u))continue;
  uint previous=atomic_max(value+j,(uint)candidate);
  // Even when another item has already read the improved value in this same
  // dispatch, enqueueing it next guarantees every outgoing edge is revisited.
  if((uint)candidate>previous && atomic_xchg(stamp+j,epoch)!=epoch){
   uint position=atomic_inc(count);next[position]=j;
  }
 }
}
