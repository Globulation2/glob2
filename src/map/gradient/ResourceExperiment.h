// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Disposable research instrumentation. No network/replay/save-state contract.
#include "BuildingGradientSearch.h"
#include "ResourceExperimentTree.h"
#include "AdaptiveResourceModel.h"
#include "ResourceSnapshotExperiment.h"
#include <deque>
#include <fstream>
#include <array>
#include <vector>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <chrono>
#include <functional>
namespace ResourceExperiment {
inline unsigned setting(const char* key,unsigned fallback) { const char* s=std::getenv(key);return s ? unsigned(std::stoul(s)) : fallback; }
enum class Reader {Bulk,Movement,Scalar,Export};
inline thread_local Reader reader=Reader::Bulk;
struct ReadScope { Reader before; explicit ReadScope(Reader r):before(std::exchange(reader,r)){}~ReadScope(){reader=before;} };
struct Demand {
 struct Query {unsigned tick;int cost;};
 unsigned captured=0,serving=0,lastSubmitted=0; bool requested=false;
 unsigned lastQueryTick=UINT32_MAX,lastQueryGap=0;
 AdaptiveResource::Sources servingSources{};
 std::array<std::uint64_t,5> servingChanges{}; std::deque<Query> queries;
 void queried(unsigned tick,int value) {if(lastQueryTick!=tick){lastQueryGap=lastQueryTick==UINT32_MAX?0:tick-lastQueryTick;lastQueryTick=tick;}queries.push_back({tick,value<=1?-1:65535-value});while(!queries.empty()&&tick-queries.front().tick>=128)queries.pop_front();}
 std::vector<int> features(unsigned tick,int material,int swim,int units,const std::array<std::uint64_t,5>& changes)const {
  int n32=0,n128=0,near=0,sum=0,goals=0,unreachable=0;
  for(const auto& q:queries)if(tick-q.tick<128){++n128;if(tick-q.tick<32){++n32;near+=q.cost>=0&&q.cost<=50;sum+=q.cost<0?10000:q.cost;goals+=q.cost==0;unreachable+=q.cost<0;}}
  auto delta=[&](size_t i){return int(std::min<std::uint64_t>(1000000,changes[i]>=servingChanges[i]?changes[i]-servingChanges[i]:0));};
  return {int(tick-serving),n32,n128,near,n32?sum/n32:0,goals,unreachable,material,swim,units,delta(1),delta(2),delta(3),delta(4)};
 }
};
struct Lazy {
 std::unique_ptr<BuildingGradientSearch> search;
 std::vector<Uint16> shadow;
 Uint16* data=nullptr; unsigned capture=0,published=0;int key=0,required=0,width=0,height=0;
 unsigned movement=0,scalar=0,bulk=0,exports=0;
 std::vector<int> features;
 std::function<void(size_t)> resolveOwner;
 std::uint64_t resumeNs=0;
};
}
