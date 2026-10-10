// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental offline kernel. All concurrent distance accesses are atomic.
// Positive costs, immutable zero obstacles and monotone improvements give the
// same least-cost fixed point. A changed vertex schedules its outgoing edges
// for the next dispatch even if another work-item already observed its new value.
__kernel void frontier(__global uint *value, __global const uint *cost,
 __global const uint *active, __global uint *next, __global uint *changed,
 uint w, uint h, uint cap)
{
 uint i=get_global_id(0); if(i>=w*h||!active[i])return;
 uint source=atomic_or(value+i,0u); if(source<=1)return;
 int x=i%w,y=i/w; uint packed=cost[i];
 int threshold=max(2,65535-(int)cap);
 for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy){
  uint j=((y+dy+(int)h)%h)*w+(x+dx+(int)w)%w;
  uint step=(dx&&dy)?packed>>16:packed&65535;
  int candidate=(int)source-(int)step;
  if(candidate<threshold||!atomic_or(value+j,0u))continue;
  uint previous=atomic_max(value+j,(uint)candidate);
  if((uint)candidate>previous){atomic_or(next+j,1u);atomic_or(changed,1u);}
 }
}
