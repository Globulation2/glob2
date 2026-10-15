// SPDX-License-Identifier: GPL-3.0-or-later
#include "AirGradientRuntime.h"
#include "GradientRuntime.h"
#include "Map.h"
#include "Game.h"
#include "Unit.h"
#include "field/AirGradient.h"
#include "field/GradientCosts.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <algorithm>
#include <limits>

namespace {
using namespace air_guidance;
Uint32 computeGoalDigest(const Output& output)
{
    Uint32 digest=2166136261u;
    const auto word=[&](Uint32 value) {
        for (unsigned shift=0;shift<32;shift+=8) { digest^=(value>>shift)&255; digest*=16777619u; }
    };
    for (const auto& goal:output.goals) { word(goal.x); word(goal.y); word(goal.supplier.gid); word(goal.supplier.generation); }
    return digest;
}
void buildBuilding(const Key& key,const SimulationSnapshot::Handle& snapshot,Uint16* values,Output& output,GradientWorkspace& scratch)
{
    GradientExecutionAudit::record(GradientExecutionAudit::Kind::AirTraversalPlane);
    const auto view=snapshot.view(); const std::size_t cells=std::size_t(snapshot.width)*snapshot.height;
    std::vector<std::uint8_t> passable(cells);
    output.entryCosts.resize(cells);
    std::vector<gradient_kernel::EntrySteps> costs(cells);
    for (std::size_t i=0;i<cells;++i) {
        const auto& terrain=view.terrainProperties(i);
        passable[i]=terrain.flyable;
        costs[i]=field::air::compileSteps(terrain.flyable ? gradient_kernel::scaledTerrainStep(GRADIENT_STEP,terrain.airSpeedQ8) : GRADIENT_STEP);
        output.entryCosts[i]=costs[i].cardinal;
    }
    const field::Grid grid(snapshot.width,snapshot.height);
    const field::air::Goal goal{unsigned(grid.index(key.x,key.y)),0,0};
    field::air::build({grid,passable,costs},std::span(&goal,1),{values,cells},scratch,{},field::air::GoalPolicy::ApproachBlocked);
}
Uint64 charge(const Key& key,std::size_t cells) {
    // Output allocations only: traversal, candidates and queues are private
    // worker scratch. Shared selectors retain no more than one goal per cell.
    const Uint64 distances=cells*sizeof(Uint16);
    const Uint64 output=key.family==Family::Building ? distances+cells*sizeof(Uint8)
        : key.family==Family::ClearChoice ? distances+sizeof(Choice)
        : distances+cells*(sizeof(Uint8)+sizeof(Uint32)+sizeof(Goal));
    return 3*output; // Published, private, and one logically retired generation.
}
bool retained(const Map& map,const AirGradientRuntime::Entry& entry,const GradientRuntime& runtime)
{
    bool pending=false;
    runtime.pipeline.visitLogical([&](auto* slot,unsigned,bool) { if (slot==&entry.slot) pending=true; });
    return pending || (entry.unread && Uint32(map.game->stepCounter-entry.publishedTick)<256);
}
}

AirGradientRuntime::Dependencies AirGradientRuntime::dependencies(const Map& map,const Key& key) const
{
    const auto generations=map.snapshotGenerations();
    switch (key.family) {
    case Family::Material: return {generations[1],generations[3],generations[4],map.gradientRuntime->stockRevision[key.team][key.material]};
    case Family::Guard: return {0,generations[3],0,0};
    case Family::ClearZone: return {generations[1],generations[3],0,0,0};
    case Family::ClearChoice: return {generations[1],generations[3],0,0,claimRevision[key.team]};
    case Family::Building: return {};
    }
    throw std::logic_error("Unknown air dependency family");
}
bool AirGradientRuntime::valid(const Map& map,const Key& key) const
{
    if (unsigned(key.family)>unsigned(Family::ClearChoice) || !map.game || key.team<0 || key.team>=map.game->teamsCount() || key.material<0 || key.material>=int(MaterialCount)) return false;
    if ((key.family==Family::Building || key.family==Family::ClearZone) && key.building.empty()) return false;
    if (key.family==Family::ClearChoice && key.unit.empty()) return false;
    if (!key.building.empty()) {
        const auto* b=map.game->resolveBuilding(key.building);
        if (!b) return false;
        if (key.family==Family::Building && !((key.x==map.normalizeX(b->posX) && key.y==map.normalizeY(b->posY))
            || (key.x==map.normalizeX(b->posX+b->type->width/2) && key.y==map.normalizeY(b->posY+b->type->height/2)))) return false;
        if (key.family==Family::ClearZone
            && (key.x!=b->posX || key.y!=b->posY || key.range!=b->unitStayRange || key.materialMask!=b->clearingMaterialMask)) return false;
    }
    if (!key.consumer.empty()) {
        const auto* consumer=map.game->resolveBuilding(key.consumer);
        if (!consumer) return false;
        if (key.family==Family::Material && key.epoch!=((unsigned(consumer->typeNum)<<1)|unsigned(consumer->usesTeamMaterials()))) return false;
    }
    if (!key.unit.empty()) {
        const auto* unit=map.game->resolveUnit(key.unit);
        if (!unit || unit->isDead || unit->posX!=key.sourceX || unit->posY!=key.sourceY) return false;
        if (key.family==Family::ClearChoice && (Game::refOf(unit->attachedBuilding)!=key.building
            || key.epoch!=(unsigned(unit->activity)|(unsigned(unit->displacement)<<8)|(unsigned(unit->medical)<<16)))) return false;
    }
    return true;
}
void AirGradientRuntime::invalidate(Map& map)
{
    for (auto& [id,entry]:entries) {
        map.gradientRuntime->pipeline.invalidate(&entry.slot);
        if (entry.values) retired.push_back({id,charge(entry.key,map.size)/3,entry.values,entry.output});
        entry.values.reset(); entry.output.reset(); entry.slot=nullptr; entry.unread=false;
    }
}
unsigned AirGradientRuntime::idForSlot(Uint16* const* slot) const
{
    const auto found=slotIndex.find(slot);
    return found==slotIndex.end()?0:found->second;
}
FieldReadiness AirGradientRuntime::request(Map& map,const Key& key,bool refresh)
{
    if (!map.compute.insideJob()) { width=map.getW(); height=map.getH(); }
    if (!valid(map,key)) return FieldReadiness::Pending;
    if (!map.compute.insideJob() && !map.gradientRuntime->pipeline.enabled()) map.configureGradientPipeline(0,8);
    const auto indexed=keyIndex.find(key);
    auto found=indexed==keyIndex.end()?entries.end():entries.find(indexed->second);
    if (map.compute.insideJob()) return found!=entries.end() && found->second.values ? FieldReadiness::Ready : FieldReadiness::Pending;
    if (found==entries.end()) {
        if (charge(key,map.size)>Budget) throw std::runtime_error("Air destination field exceeds the deterministic session budget");
        if (entries.size()>=MaxEntries) return FieldReadiness::Pending;
        unsigned id;
        if (freeIds.empty()) id=nextId++;
        else { id=*freeIds.begin(); freeIds.erase(freeIds.begin()); }
        found=entries.try_emplace(id).first; found->second.key=key; keyIndex.emplace(key,id); slotIndex.emplace(&found->second.slot,id);
    }
    auto& entry=found->second;
    if (!entry.values || refresh || entry.dependencies!=dependencies(map,key) || entry.topology!=map.topologyGeneration || Uint32(map.game->stepCounter-entry.capturedTick)>=128)
        map.requestGradientField(&entry.slot,int(gradient_preparation::Kind::Materials),key.team,key.material,0);
    return entry.values ? FieldReadiness::Ready : FieldReadiness::Pending;
}
void AirGradientRuntime::dropPublished(Map& map,unsigned id,Entry& entry)
{
    // Transfer logical retirement attribution before releasing the charged slot.
    for (auto& old:retired) if (old.entry==id) { old.entry=0; retiredBytes+=old.bytes; }
    if (entry.values) {
        const auto bytes=charge(entry.key,map.size)/3;
        retired.push_back({0,bytes,entry.values,entry.output}); retiredBytes+=bytes;
    }
    entry.values.reset(); entry.output.reset(); entry.slot=nullptr; entry.unread=false; entry.charged=false;
}
bool AirGradientRuntime::tryAdmit(Map& map,Uint16** slot)
{
    const auto id=idForSlot(slot);
    if (!id) return true;
    auto& entry=entries.at(id);
    if (entry.charged) return true;
    const auto needed=charge(entry.key,map.size);
    if (needed>Budget) throw std::runtime_error("Air destination field exceeds the deterministic session budget");
    const auto total=[&] { Uint64 bytes=retiredBytes; for (const auto& [number,current]:entries) if (current.charged) bytes+=charge(current.key,map.size); return bytes; };
    while (total()+needed>Budget) {
        auto victim=std::find_if(entries.begin(),entries.end(),[&](const auto& item) {
            return item.first!=id && item.second.charged && !retained(map,item.second,*map.gradientRuntime);
        });
        if (victim==entries.end()) return false;
        // A queued warm refresh retains its FIFO metadata, not the old buffer.
        dropPublished(map,victim->first,victim->second);
    }
    entry.charged=true;
    return true;
}
void AirGradientRuntime::prune(Map& map)
{
    for (auto entry=entries.begin();entry!=entries.end();) {
        if (valid(map,entry->second.key)) { ++entry; continue; }
        auto* slot=&entry->second.slot;
        map.gradientRuntime->pipeline.invalidate(slot);
        std::erase_if(map.gradientRuntime->fieldRequests,[&](const auto& request){return request.slot==slot;});
        bool pending=false;
        map.gradientRuntime->pipeline.visitLogical([&](auto* destination,unsigned,bool){if (destination==slot) pending=true;});
        if (pending) { ++entry; continue; }
        dropPublished(map,entry->first,entry->second);
        keyIndex.erase(entry->second.key); slotIndex.erase(&entry->second.slot); freeIds.insert(entry->first);
        entry=entries.erase(entry);
    }
}
Published AirGradientRuntime::lookup(const Key& key) const
{
    const auto indexed=keyIndex.find(key);
    if (indexed==keyIndex.end()) return {};
    const auto& entry=entries.at(indexed->second);
    if (!entry.values) return {};
    return {FieldReadiness::Ready,entry.values,entry.output,entry.captureRevision};
}
void AirGradientRuntime::restoreJob(Map& map,GradientPipeline::Job& job,std::shared_ptr<Payload> payload)
{
    const unsigned id=idForSlot(job.slot);
    job.payload=payload;
    job.customPublish=[this,&map,id,payload](auto& completed) {
        auto found=entries.find(id);
        if (found==entries.end() || !(found->second.key==payload->key) || !valid(map,payload->key)) return;
        auto& entry=found->second;
        if (entry.values) retired.push_back({id,charge(entry.key,map.size)/3,entry.values,entry.output});
        entry.values=std::shared_ptr<const Uint16[]>(completed.data.release());
        entry.slot=const_cast<Uint16*>(entry.values.get()); entry.output=payload->output;
        entry.captureRevision=payload->captureRevision; entry.capturedTick=payload->capturedTick; entry.dependencies=payload->dependencies;
        entry.topology=payload->topology; entry.publishedTick=map.game->stepCounter; entry.unread=true;
    };
}
void AirGradientRuntime::prepare(Map& map,GradientPipeline::Job& job,const SimulationSnapshot::Handle& snapshot)
{
    const auto id=idForSlot(job.slot); auto& entry=entries.at(id);
    if (!entry.charged) throw std::logic_error("Uncharged air field admission");
    auto payload=std::make_shared<Payload>(); payload->key=entry.key;
    payload->captureRevision=snapshot.observationRevision; payload->capturedTick=snapshot.tick; payload->topology=map.topologyGeneration;
    payload->dependencies=dependencies(map,entry.key);
    restoreJob(map,job,payload);
    job.snapshotLease=snapshot.gradientView();
    job.customWork=[payload](auto& work,auto& scratch) {
        if (payload->key.family==Family::Building) buildBuilding(payload->key,*work.snapshotLease,work.data.get(),*payload->output,scratch);
        else {
            if (payload->key.family==Family::ClearChoice) std::fill_n(work.data.get(),std::size_t(work.snapshotLease->width)*work.snapshotLease->height,GRADIENT_UNREACHABLE);
            buildSelection(payload->key,*work.snapshotLease,work.data.get(),*payload->output,scratch);
        }
        payload->output->goalDigest=computeGoalDigest(*payload->output);
    };
    map.gradientRuntime->pipeline.prepare(&job,[](auto&){});
}
RouteResult AirGradientRuntime::query(Map& map,const Key& key,int x,int y,Sample* result,bool leaveBlocked)
{
    if (request(map,key)==FieldReadiness::Pending) return RouteResult::Pending;
    const auto published=lookup(key);
    const auto indexed=keyIndex.find(key);
    auto found=indexed==keyIndex.end()?entries.end():entries.find(indexed->second);
    if (!map.compute.insideJob() && found!=entries.end()) found->second.unread=false;
    if (key.family==Family::ClearChoice) {
        if (!published.output->choice.available) return RouteResult::Unreachable;
        if (result) *result=published.output->choice.sample;
        return RouteResult::Reachable;
    }
    unsigned bestRank=UINT_MAX;
    const auto consider=[&](int px,int py,unsigned extra,Sample& best,bool& available) {
        const auto cell=map.coordToIndex(px,py); const auto value=field::air::decode(published.values[cell]);
        if (!value || extra>unsigned(gradient_kernel::COST_LIMIT)-*value) return;
        const unsigned cost=*value+extra;
        const auto rank=published.output->labels.empty()?0:published.output->labels[cell];
        if (key.family!=Family::Building && rank>=published.output->goals.size()) return;
        const auto& goal=key.family==Family::Building ? Goal{key.x,key.y,{}} : published.output->goals[rank];
        if (!available || cost<best.cost || (cost==best.cost && rank<bestRank)) {
            best={goal.x,goal.y,cost,goal.supplier}; available=true; bestRank=rank;
        }
    };
    Sample best; bool available=false;
    if (published.values[map.coordToIndex(x,y)]!=GRADIENT_FORBIDDEN) consider(x,y,0,best,available);
    else if (leaveBlocked) for (const auto offset:field::Surrounding) {
        const int nx=x+offset.x,ny=y+offset.y; const auto cell=map.coordToIndex(nx,ny);
        if (published.values[cell]==GRADIENT_FORBIDDEN) continue;
        const auto edge=field::air::compileSteps(published.output->entryCosts.at(cell));
        consider(nx,ny,offset.x&&offset.y?edge.diagonal:edge.cardinal,best,available);
    }
    if (!available) return RouteResult::Unreachable;
    if (result) *result=best;
    return RouteResult::Reachable;
}
FieldReadiness Map::requestAirField(const air_guidance::Key& key,bool refresh) { return gradientRuntime->air.request(*this,key,refresh); }
air_guidance::Published Map::lookupAirField(const air_guidance::Key& key) const { return gradientRuntime->air.valid(*this,key)?gradientRuntime->air.lookup(key):air_guidance::Published{}; }
RouteResult Map::queryAirDestination(const air_guidance::Key& key,int x,int y,air_guidance::Sample* sample,bool leaveBlocked) { return gradientRuntime->air.query(*this,key,x,y,sample,leaveBlocked); }
RouteResult Map::queryAirBuildingDistance(Building* building,bool midpoint,bool leaveBlocked,int x,int y,int* cost)
{
    air_guidance::Key key; key.team=building->owner->teamNumber; key.building=Game::refOf(building);
    key.x=normalizeX(building->posX+(midpoint?building->type->width/2:0)); key.y=normalizeY(building->posY+(midpoint?building->type->height/2:0));
    air_guidance::Sample sample; const auto result=queryAirDestination(key,x,y,&sample,leaveBlocked);
    if (result==RouteResult::Reachable && cost) *cost=int(sample.cost);
    return result;
}

namespace {
void writeRef(GAGCore::OutputStream* s,BuildingRef ref) { s->writeUint16(ref.gid,"gid"); s->writeUint32(ref.generation,"identity"); }
BuildingRef readRef(GAGCore::InputStream* s) { return {s->readUint16("gid"),s->readUint32("identity")}; }
void writeKey(GAGCore::OutputStream* s,const Key& key) {
    s->writeUint8(unsigned(key.family),"family"); s->writeSint32(key.team,"team"); s->writeSint32(key.material,"material");
    s->writeUint32(key.modes,"modes"); s->writeUint8(key.withMarkets,"withMarkets");
    s->writeEnterSection("building"); writeRef(s,key.building); s->writeLeaveSection();
    s->writeEnterSection("consumer"); writeRef(s,key.consumer); s->writeLeaveSection();
    s->writeEnterSection("unit"); writeRef(s,{key.unit.gid,key.unit.generation}); s->writeLeaveSection();
    s->writeSint32(key.x,"x"); s->writeSint32(key.y,"y"); s->writeSint32(key.range,"range"); s->writeUint32(key.materialMask,"materialMask");
    s->writeSint32(key.sourceX,"sourceX"); s->writeSint32(key.sourceY,"sourceY"); s->writeUint32(key.epoch,"epoch");
}
bool flag(GAGCore::InputStream* s,const char* name) { const auto value=s->readUint8(name); if (value>1) throw std::runtime_error("Invalid air flag"); return value; }
Key readKey(GAGCore::InputStream* s) {
    Key key; key.family=Family(s->readUint8("family")); key.team=s->readSint32("team"); key.material=s->readSint32("material");
    key.modes=s->readUint32("modes"); key.withMarkets=flag(s,"withMarkets");
    s->readEnterSection("building"); key.building=readRef(s); s->readLeaveSection();
    s->readEnterSection("consumer"); key.consumer=readRef(s); s->readLeaveSection();
    s->readEnterSection("unit"); const auto unit=readRef(s); key.unit={unit.gid,unit.generation}; s->readLeaveSection();
    key.x=s->readSint32("x"); key.y=s->readSint32("y"); key.range=s->readSint32("range"); key.materialMask=s->readUint32("materialMask");
    key.sourceX=s->readSint32("sourceX"); key.sourceY=s->readSint32("sourceY"); key.epoch=s->readUint32("epoch");
    if (unsigned(key.family)>unsigned(Family::ClearChoice) || key.team<0 || key.team>=Team::MAX_COUNT || key.material<0 || key.material>=int(MaterialCount)
        || key.modes>3 || key.range<0 || (key.materialMask&~AllMaterials)) throw std::runtime_error("Invalid air key");
    const auto checkRef=[](auto ref,unsigned count) { return ref.gid==0xFFFF ? ref.generation==0 : unsigned(ref.gid)<count && ref.generation!=0; };
    if (!checkRef(key.building,Team::MAX_COUNT*Building::MAX_COUNT) || !checkRef(key.consumer,Team::MAX_COUNT*Building::MAX_COUNT)
        || !checkRef(key.unit,Team::MAX_COUNT*Unit::MAX_COUNT)) throw std::runtime_error("Invalid air entity identity");
    if ((key.family==Family::Building || key.family==Family::ClearZone) && key.building.empty()) throw std::runtime_error("Missing air building identity");
    if (key.family==Family::ClearChoice && key.unit.empty()) throw std::runtime_error("Missing air unit identity");
    return key;
}
void writeOutput(GAGCore::OutputStream* s,const Output& output) {
    s->writeUint32(output.goalDigest,"goalDigest");
    s->writeUint32(output.entryCosts.size(),"entryCosts");
    s->writeEnterSection("entryCosts"); for (unsigned i=0;i<output.entryCosts.size();++i) { s->writeEnterSection(i); s->writeUint8(output.entryCosts[i],"value"); s->writeLeaveSection(); } s->writeLeaveSection();
    s->writeUint32(output.labels.size(),"labels");
    s->writeEnterSection("labels"); for (unsigned i=0;i<output.labels.size();++i) { s->writeEnterSection(i); s->writeUint32(output.labels[i],"value"); s->writeLeaveSection(); } s->writeLeaveSection();
    s->writeUint32(output.goals.size(),"goals");
    s->writeEnterSection("goals"); for (unsigned i=0;i<output.goals.size();++i) { s->writeEnterSection(i); const auto& goal=output.goals[i]; s->writeSint32(goal.x,"x"); s->writeSint32(goal.y,"y"); writeRef(s,goal.supplier); s->writeLeaveSection(); } s->writeLeaveSection();
    s->writeUint8(output.choice.available,"choice"); s->writeSint32(output.choice.sample.x,"choiceX"); s->writeSint32(output.choice.sample.y,"choiceY");
    s->writeUint32(output.choice.sample.cost,"choiceCost"); s->writeEnterSection("choiceSupplier"); writeRef(s,output.choice.sample.supplier); s->writeLeaveSection();
}
std::shared_ptr<Output> readOutput(GAGCore::InputStream* s,std::size_t cells) {
    auto output=std::make_shared<Output>(); output->goalDigest=s->readUint32("goalDigest");
    const auto costs=s->readUint32("entryCosts");
    if (costs && costs!=cells) throw std::runtime_error("Invalid air entry-cost count");
    output->entryCosts.resize(costs); s->readEnterSection("entryCosts"); for (unsigned i=0;i<costs;++i) { s->readEnterSection(i); const auto value=s->readUint8("value"); field::air::compileSteps(value); output->entryCosts[i]=value; s->readLeaveSection(); } s->readLeaveSection();
    const auto labels=s->readUint32("labels");
    if (labels && labels!=cells) throw std::runtime_error("Invalid air label count");
    output->labels.resize(labels); s->readEnterSection("labels"); for (unsigned i=0;i<labels;++i) { s->readEnterSection(i); output->labels[i]=s->readUint32("value"); s->readLeaveSection(); } s->readLeaveSection();
    const auto goals=s->readUint32("goals"); if (goals>cells) throw std::runtime_error("Invalid air goal count");
    output->goals.resize(goals); s->readEnterSection("goals"); for (unsigned i=0;i<goals;++i) { s->readEnterSection(i); auto& goal=output->goals[i]; goal.x=s->readSint32("x"); goal.y=s->readSint32("y"); goal.supplier=readRef(s); s->readLeaveSection(); } s->readLeaveSection();
    for (const auto rank:output->labels) if (rank!=field::air::NoGoal && rank>=goals) throw std::runtime_error("Invalid air goal label");
    output->choice.available=flag(s,"choice"); output->choice.sample.x=s->readSint32("choiceX"); output->choice.sample.y=s->readSint32("choiceY"); output->choice.sample.cost=s->readUint32("choiceCost");
    s->readEnterSection("choiceSupplier"); output->choice.sample.supplier=readRef(s); s->readLeaveSection();
    if (output->choice.sample.cost>unsigned(gradient_kernel::COST_LIMIT)) throw std::runtime_error("Invalid air choice cost");
    // Loading is an explicit full-state verification boundary. Normal live
    // checksums consume this worker-produced digest without enumerating goals.
    if (output->goalDigest!=computeGoalDigest(*output)) throw std::runtime_error("Invalid air goal digest");
    return output;
}
}
void AirGradientRuntime::checkPayload(const Payload& payload,std::size_t cells,bool complete,const Uint16* values) const
{
    const auto& output=*payload.output;
    const auto refValid=[](BuildingRef ref) { return ref.empty()?ref.generation==0 : ref.gid<Team::MAX_COUNT*Building::MAX_COUNT && ref.generation!=0; };
    const auto positionValid=[&](int x,int y) { return x>=0 && y>=0 && x<width && y<height; };
    for (const auto& goal:output.goals) if (!positionValid(goal.x,goal.y) || !refValid(goal.supplier)) throw std::runtime_error("Invalid air goal");
    if (!positionValid(output.choice.sample.x,output.choice.sample.y) || !refValid(output.choice.sample.supplier)) throw std::runtime_error("Invalid air choice goal");
    if (!complete) return;
    if (values) for (std::size_t cell=0;cell<cells;++cell) {
        const auto value=values[cell];
        if (value>GRADIENT_UNREACHABLE && !field::air::decode(value)) throw std::runtime_error("Invalid bounded air value");
        if (!output.labels.empty() && ((value>GRADIENT_UNREACHABLE)!=(output.labels[cell]!=field::air::NoGoal))) throw std::runtime_error("Air label/value mismatch");
    }
    if (payload.key.family==Family::ClearChoice) {
        if (!output.entryCosts.empty() || !output.labels.empty() || !output.goals.empty()) throw std::runtime_error("Invalid private air choice output");
    } else {
        if (output.entryCosts.size()!=cells) throw std::runtime_error("Incomplete air traversal metadata");
        if (payload.key.family==Family::Building) {
            if (!output.labels.empty() || !output.goals.empty()) throw std::runtime_error("Invalid building air labels");
        } else if (output.labels.size()!=cells) throw std::runtime_error("Incomplete air goal labels");
    }
}
void AirGradientRuntime::savePayload(const Map& map,GAGCore::OutputStream* s,const Payload& payload,std::size_t) const {
    const auto current=dependencies(map,payload.key);
    s->writeEnterSection("dependencies");
    for (unsigned i=0;i<3;++i) { s->writeEnterSection(i); s->writeUint8(payload.dependencies[i]==current[i],"current"); s->writeLeaveSection(); }
    s->writeUint32(payload.dependencies[3]>>32,"stockHigh"); s->writeUint32(payload.dependencies[3],"stockLow");
    s->writeUint32(payload.dependencies[4]>>32,"claimsHigh"); s->writeUint32(payload.dependencies[4],"claimsLow");
    s->writeLeaveSection();
    writeKey(s,payload.key); s->writeUint32(payload.capturedTick,"capturedTick"); s->writeUint32(payload.topology,"topology");
    s->writeUint32(payload.captureRevision>>32,"revisionHigh"); s->writeUint32(payload.captureRevision,"revisionLow"); writeOutput(s,*payload.output);
}
std::shared_ptr<AirGradientRuntime::Payload> AirGradientRuntime::loadPayload(const Map& map,GAGCore::InputStream* s,std::size_t cells) {
    std::array<bool,3> current;
    s->readEnterSection("dependencies");
    for (unsigned i=0;i<3;++i) { s->readEnterSection(i); current[i]=flag(s,"current"); s->readLeaveSection(); }
    Uint64 stock=Uint64(s->readUint32("stockHigh"))<<32; stock|=s->readUint32("stockLow");
    Uint64 claims=Uint64(s->readUint32("claimsHigh"))<<32; claims|=s->readUint32("claimsLow"); s->readLeaveSection();
    auto payload=std::make_shared<Payload>(); payload->key=readKey(s);
    if (payload->key.team>=map.game->teamsCount()) throw std::runtime_error("Invalid air dependency team");
    payload->dependencies=dependencies(map,payload->key);
    for (unsigned i=0;i<3;++i) if (!current[i]) payload->dependencies[i]=0;
    payload->dependencies[3]=stock; payload->dependencies[4]=claims; payload->capturedTick=s->readUint32("capturedTick"); payload->topology=s->readUint32("topology");
    payload->captureRevision=Uint64(s->readUint32("revisionHigh"))<<32; payload->captureRevision|=s->readUint32("revisionLow"); payload->output=readOutput(s,cells); return payload;
}
void AirGradientRuntime::save(Map& map,GAGCore::OutputStream* s) const {
    s->writeEnterSection("airFields"); s->writeUint32(nextId,"nextId"); s->writeUint32(entries.size(),"count"); s->writeUint32(retiredBytes,"retiredBytes");
    s->writeUint32(retired.size(),"retiredCount"); s->writeEnterSection("retired");
    for (unsigned i=0;i<retired.size();++i) { s->writeEnterSection(i); s->writeUint32(retired[i].entry,"entry"); s->writeUint32(retired[i].bytes,"bytes"); s->writeLeaveSection(); }
    s->writeLeaveSection();
    s->writeEnterSection("claimRevisions");
    for (unsigned i=0;i<claimRevision.size();++i) { s->writeEnterSection(i); s->writeUint32(claimRevision[i]>>32,"high"); s->writeUint32(claimRevision[i],"low"); s->writeLeaveSection(); }
    s->writeLeaveSection();
    unsigned index=0; for (const auto& [id,entry]:entries) {
        s->writeEnterSection(index++); s->writeUint32(id,"id");
        Payload payload; payload.key=entry.key; payload.capturedTick=entry.capturedTick; payload.topology=entry.topology; payload.captureRevision=entry.captureRevision; payload.dependencies=entry.dependencies;
        if (entry.output) payload.output=std::const_pointer_cast<Output>(entry.output);
        savePayload(map,s,payload,map.size); s->writeUint32(entry.publishedTick,"publishedTick"); s->writeUint8(entry.unread,"unread"); s->writeUint8(entry.charged,"charged");
        s->writeUint8(bool(entry.values),"published");
        if (entry.values) s->writeUint16Sections(entry.values.get(),map.size,"value");
        s->writeLeaveSection();
    }
    s->writeLeaveSection();
}
void AirGradientRuntime::load(Map& map,GAGCore::InputStream* s,bool) {
    width=map.getW(); height=map.getH();
    entries.clear(); keyIndex.clear(); freeIds.clear(); slotIndex.clear(); s->readEnterSection("airFields"); nextId=s->readUint32("nextId"); const auto count=s->readUint32("count"); retired.clear(); retiredBytes=s->readUint32("retiredBytes");
    if (retiredBytes>Budget || !nextId || nextId>MaxEntries+1 || count>MaxEntries) throw std::runtime_error("Invalid air field inventory");
    const auto retiredCount=s->readUint32("retiredCount"); if (retiredCount>MaxEntries*3) throw std::runtime_error("Invalid air retirement count");
    s->readEnterSection("retired"); for (unsigned i=0;i<retiredCount;++i) { s->readEnterSection(i); Retired item; item.entry=s->readUint32("entry"); item.bytes=s->readUint32("bytes"); if (!item.bytes || item.bytes>Budget) throw std::runtime_error("Invalid air retirement charge"); retired.push_back(item); s->readLeaveSection(); } s->readLeaveSection();
    s->readEnterSection("claimRevisions");
    for (unsigned i=0;i<claimRevision.size();++i) { s->readEnterSection(i); claimRevision[i]=Uint64(s->readUint32("high"))<<32; claimRevision[i]|=s->readUint32("low"); s->readLeaveSection(); }
    s->readLeaveSection();
    std::map<Key,bool> keys;
    for (unsigned i=0;i<count;++i) {
        s->readEnterSection(i); const auto id=s->readUint32("id"); auto payload=loadPayload(map,s,map.size);
        if (!id || id>MaxEntries || id>=nextId || entries.contains(id) || keys.contains(payload->key)) throw std::runtime_error("Invalid air field identity");
        const auto& key=payload->key;
        if (key.team>=map.game->teamsCount() || key.x<0 || key.x>=map.w || key.y<0 || key.y>=map.h || key.sourceX<0 || key.sourceX>=map.w || key.sourceY<0 || key.sourceY>=map.h)
            throw std::runtime_error("Invalid air field coordinates");
        keys.emplace(key,true); keyIndex.emplace(key,id); auto& entry=entries[id]; slotIndex.emplace(&entry.slot,id); entry.key=key; entry.capturedTick=payload->capturedTick; entry.topology=payload->topology; entry.captureRevision=payload->captureRevision; entry.dependencies=payload->dependencies;
        entry.publishedTick=s->readUint32("publishedTick"); entry.unread=flag(s,"unread"); entry.charged=flag(s,"charged");
        if (flag(s,"published")) {
            if (!entry.charged) throw std::runtime_error("Uncharged published air field");
            checkPayload(*payload,map.size,true);
            auto data=std::make_shared_for_overwrite<Uint16[]>(map.size);
            for (std::size_t cell=0;cell<map.size;++cell) { s->readEnterSection(cell); data[cell]=s->readUint16("value"); s->readLeaveSection(); }
            for (std::size_t cell=0;cell<map.size;++cell) {
                const auto value=data[cell]; if (value>GRADIENT_UNREACHABLE && !field::air::decode(value)) throw std::runtime_error("Invalid bounded air value");
                if (!payload->output->labels.empty() && ((value>GRADIENT_UNREACHABLE)!=(payload->output->labels[cell]!=field::air::NoGoal))) throw std::runtime_error("Air label/value mismatch");
            }
            entry.values=std::move(data); entry.slot=const_cast<Uint16*>(entry.values.get()); entry.output=payload->output;
        } else if (entry.unread) throw std::runtime_error("Unread air field has no published output");
        s->readLeaveSection();
    }
    s->readLeaveSection();
    for (unsigned id=1;id<nextId;++id) if (!entries.contains(id)) freeIds.insert(id);
    Uint64 orphanBytes=0;
    for (const auto& old:retired) {
        if (old.entry) { const auto entry=entries.find(old.entry); if (entry==entries.end() || !entry->second.charged || old.bytes!=charge(entry->second.key,map.size)/3) throw std::runtime_error("Invalid attributed air retirement"); }
        else orphanBytes+=old.bytes;
    }
    if (orphanBytes!=retiredBytes) throw std::runtime_error("Air retirement budget mismatch");
    Uint64 total=retiredBytes; for (const auto& [id,entry]:entries) if (entry.charged) total+=charge(entry.key,map.size);
    if (total>Budget) throw std::runtime_error("Air field budget exceeded");
}
std::uint32_t AirGradientRuntime::checksum(const Map& map,std::uint32_t checksum,bool heavy) const {
    const auto mix=[&](Uint32 value) { checksum=(checksum<<1|checksum>>31)^value; };
    mix(nextId); mix(entries.size()); mix(retiredBytes);
    for (const auto revision:claimRevision) { mix(revision); mix(revision>>32); }
    mix(retired.size()); for (const auto& old:retired) { mix(old.entry); mix(old.bytes); }
    for (const auto& [id,entry]:entries) {
        mix(id); mix(unsigned(entry.key.family)); mix(entry.key.team); mix(entry.key.material); mix(entry.key.modes); mix(entry.key.withMarkets);
        for (const auto ref:{entry.key.building,entry.key.consumer,BuildingRef{entry.key.unit.gid,entry.key.unit.generation}}) { mix(ref.gid); mix(ref.generation); }
        mix(entry.key.x); mix(entry.key.y); mix(entry.key.range); mix(entry.key.materialMask); mix(entry.key.sourceX); mix(entry.key.sourceY); mix(entry.key.epoch);
        const auto current=dependencies(map,entry.key);
        for (unsigned i=0;i<3;++i) mix(entry.dependencies[i]==current[i]);
        mix(entry.dependencies[3]); mix(entry.dependencies[3]>>32); mix(entry.dependencies[4]); mix(entry.dependencies[4]>>32);
        mix(entry.capturedTick); mix(entry.publishedTick); mix(entry.topology); mix(entry.unread); mix(entry.charged); mix(bool(entry.values));
        if (entry.output) {
            mix(entry.output->choice.available); mix(entry.output->choice.sample.x); mix(entry.output->choice.sample.y); mix(entry.output->choice.sample.cost);
            mix(entry.output->choice.sample.supplier.gid); mix(entry.output->choice.sample.supplier.generation);
            mix(entry.output->goalDigest); mix(entry.output->goals.size());
            if (heavy) {
                for (const auto& goal:entry.output->goals) { mix(goal.x); mix(goal.y); mix(goal.supplier.gid); mix(goal.supplier.generation); }
                for (const auto label:entry.output->labels) mix(label);
                for (const auto cost:entry.output->entryCosts) mix(cost);
            }
        }
        if (heavy && entry.values) for (std::size_t i=0;i<entry.output->entryCosts.size();++i) mix(entry.values[i]);
    }
    return checksum;
}
