// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental global-memory Jacobi, with production's tile activity contract.
// No local sweeps, shared storage or barriers: one exact path edge per dispatch.
inline int wrap(int x,uint n){x%=(int)n;return x<0?x+n:x;}
__kernel void propagate(__global const ushort *a,__global ushort *b,
 __global const uint *c0,__global const uint *c1,__global const uint *c2,__global const uint *c3,
 __global const uint *c4,__global const uint *c5,__global const uint *c6,__global const uint *c7,
 __global uint *changed,__global const uint *desc,
 __global const uint *active,__global uint *nextActive,uint stride,uint pitch)
{
 uint f=get_group_id(2),d=f*8,w=desc[d],h=desc[d+1],base=desc[d+2],cb=desc[d+3];
 __global const uint *costs=cb==0?c0:cb==1?c1:cb==2?c2:cb==3?c3:cb==4?c4:cb==5?c5:cb==6?c6:c7;
 uint gx=get_group_id(0),gy=get_group_id(1),tile=f*stride+gy*pitch+gx;
 if(gx>=desc[d+6]||gy>=desc[d+7]||!desc[d+5])return;
 int threshold=max(2,65535-(int)desc[d+4]);uint any=0;
 for(uint o=get_local_id(0);o<256;o+=128){
  uint x=gx*16+o%16,y=gy*16+o/16;if(x>=w||y>=h)continue;
  uint i=base+y*w+x,val=a[i];int best=0;
  if(active[tile]&&val)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy){
   uint k=wrap((int)y+dy,h)*w+wrap((int)x+dx,w),c=costs[k];
   best=max(best,(int)a[base+k]-(int)((dx&&dy)?c>>16:c&65535));
  }
  if(best>=threshold)val=max(val,(uint)best);b[i]=val;any|=val!=a[i];
 }
 if(any){
  atomic_or(changed+f,1u);
  int rx=(w%16&&w>16)?2:1,ry=(h%16&&h>16)?2:1;
  for(int dy=-ry;dy<=ry;++dy)for(int dx=-rx;dx<=rx;++dx)
   atomic_or(nextActive+f*stride+wrap((int)gy+dy,desc[d+7])*pitch+wrap((int)gx+dx,desc[d+6]),1u);
 }
}
