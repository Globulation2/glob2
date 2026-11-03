// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace AIMaxima::Prestige
{
enum State { Hold, Prepare, Invest, Finish };
enum Reason { Disabled, Emergency, Unavailable, CannotCatchUp, Prerequisites, Developing, WinningFinish };
struct Standing { int prestige=0; bool alive=false, sharesVictory=false; };
struct Race
{
    State state=Hold;
    Reason reason=Disabled;
    int own=0, leader=0, remaining=0, pending=0, gain=0;
    int catchUpSteps=0, finishSteps=0, priority=0, buildingType=-1;
    bool ready=false;
};
// Ephemeral explanation of one capacity decision; never part of saved state.
struct PipelineCapacity
{
    bool sampled=false;
    int qualifiedIdle=-1,rootAssigned=-1,ordinaryUnmet=-1,unobservedCrews=-1;
    int trainingReserve=0,crew=1,workerLimit=-1,materialLimit=-1,remainingSteps=0;
};
template<class A> void fields(A& a,Race& r)
{
    a("state",r.state);a("reason",r.reason);a("own",r.own);a("leader",r.leader);
    a("remaining",r.remaining);a("pending",r.pending);a("gain",r.gain);
    a("catchUpSteps",r.catchUpSteps);a("finishSteps",r.finishSteps);
    a("priority",r.priority);a("buildingType",r.buildingType);a("ready",r.ready);
}
// Standings are public scores. Hidden construction and opposing stock are not inputs.
inline bool wins(const std::vector<Standing>& standings,unsigned own,int64_t gain)
{
    if(own>=standings.size() || !standings[own].alive)return false;
    int64_t leader=INT64_MIN, friendly=INT64_MIN;
    for(unsigned i=0;i<standings.size();++i)if(standings[i].alive)
    {
        const int64_t score=int64_t(standings[i].prestige)+(i==own?gain:0);
        leader=std::max(leader,score);
        if(i==own || standings[i].sharesVictory)friendly=std::max(friendly,score);
    }
    return friendly==leader;
}
inline bool safe(const std::vector<Standing>& standings,unsigned own,int total,int threshold,int64_t gain)
{ return int64_t(total)+gain<threshold || wins(standings,own,gain); }
inline Race assess(const std::vector<Standing>& standings,unsigned own,int total,int threshold,
    bool enabled,bool emergency,int gain,bool available,bool ready,int pending,
    int technologyPriority,int investmentFloor,int urgencyWeight,int finishPriority)
{
    Race r;r.gain=gain;r.pending=pending;r.ready=ready;
    if(own>=standings.size())return r;
    r.own=standings[own].prestige;
    r.leader=r.own;
    for(const auto& s:standings)if(s.alive)r.leader=std::max(r.leader,s.prestige);
    r.remaining=int(std::clamp<int64_t>(int64_t(threshold)-total,0,INT32_MAX));
    if(!enabled || threshold<=0)return r;
    if(emergency){r.reason=Emergency;return r;}
    if(!available || gain<=0 || r.remaining==0){r.reason=Unavailable;return r;}
    r.catchUpSteps=int(std::min<int64_t>(INT32_MAX,(std::max<int64_t>(0,int64_t(r.leader)-r.own)+gain-1)/gain));
    r.finishSteps=int((int64_t(r.remaining)+gain-1)/gain);
    const int64_t closingGain=int64_t(r.finishSteps)*gain;
    if(!wins(standings,own,closingGain)){r.reason=CannotCatchUp;return r;}
    const int progress=int(std::clamp<int64_t>(int64_t(total)*100/std::max(1,threshold),0,100));
    r.priority=std::clamp(std::max(technologyPriority,investmentFloor+progress*urgencyWeight/100),0,100);
    r.state=ready?Invest:Prepare;r.reason=ready?Developing:Prerequisites;
    if(!ready)r.priority=std::clamp(technologyPriority,0,100);
    if(ready && int64_t(total)+gain>=threshold && wins(standings,own,gain))
    {r.state=Finish;r.reason=WinningFinish;r.priority=std::clamp(finishPriority,0,100);}
    return r;
}
struct Cancellation
{
    int buildingId=-1,actionId=-1;
    uint32_t generation=0;
    bool acknowledged=false;
};
template<class A> void fields(A& a,Cancellation& c)
{a("buildingId",c.buildingId);a("actionId",c.actionId);a("generation",c.generation);a("acknowledged",c.acknowledged);}
}
