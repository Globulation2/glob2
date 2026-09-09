#include "MaximaExperimentAudit.h"
#include "AI.h"
#include "AIMaxima.h"
#include "Game.h"
#include "Player.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>

namespace {
class AuditBackend : public GAGCore::MemoryStreamBackend {
public:
    Uint32 getHash() {
        Uint32 hash=2166136261u;
        for(size_t i=0;i<getPosition();++i) { hash^=static_cast<unsigned char>(getBuffer()[i]); hash*=16777619u; }
        return hash;
    }
};
std::string auditRandomState() { std::ostringstream state; state<<randomGenerator; return state.str(); }
}
namespace MaximaExperimentAudit
{
namespace
{
std::ofstream sink;
unsigned long long sequence=0;
bool running=false;
std::set<int> disabledPlayers;
}
void disableOrders(int player) { disabledPlayers.insert(player); }
bool ordersDisabled(int player) { return disabledPlayers.count(player)!=0; }
bool enabled() { return sink.is_open(); }
bool started() { return running; }
std::string quote(const std::string& text)
{
    std::ostringstream out;
    out<<'"';
    for(unsigned char c: text)
    {
        if(c=='"' || c=='\\') out<<'\\'<<c;
        else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;
        else out<<c;
    }
    out<<'"';
    return out.str();
}
void write(const std::string& type, const std::string& fields)
{
    if(!enabled()) return;
    sink<<"{\"schema\":1,\"sequence\":"<<sequence++<<",\"type\":"<<quote(type)
        <<fields<<"}\n";
    sink.flush();
    if(!sink.good()) { std::fprintf(stderr,"Experiment audit write failed\n"); std::exit(2); }
}
void open(const std::string& path, const std::string& identities)
{
    // Refuse to overwrite evidence, including incomplete evidence from a crash.
    std::ifstream existing(path.c_str());
    if(existing.good() || enabled()) { std::fprintf(stderr,"Audit path already exists\n"); std::exit(2); }
    sink.open(path.c_str(),std::ios::out);
    if(!sink.good()) { std::fprintf(stderr,"Cannot open experiment audit\n"); std::exit(2); }
    write("identity",",\"identities\":"+quote(identities));
}
void state(Game& game, const std::string& phase)
{
    if(!disabledPlayers.empty())
    {
        if(!enabled()) { std::fprintf(stderr,"No-orders control requires an audit\n"); std::exit(2); }
        for(int player: disabledPlayers)
            if(player>=game.gameHeader.getNumberOfPlayers() || !game.players[player]->ai ||
               game.players[player]->ai->implementationID!=AI::MAXIMA)
            { std::fprintf(stderr,"No-orders control requires a valid Maxima player\n"); std::exit(2); }
    }
    if(!enabled()) return;
    std::ostringstream out;
    std::vector<Uint32> components;
    const Uint32 rawChecksum=game.checkSum(&components,nullptr,nullptr,true);
    // MapHeader's file-format version changes when an old map is saved. It is
    // storage metadata, not world state. Keep it visible, but exclude component
    // zero from the canonical state checksum used for continuation audits.
    // The normal engine checksum intentionally skips map contents in local
    // matches. Audit every tile, including exploration and ownership masks;
    // these affect AI eligibility even before any unit checksum changes.
    AuditBackend* mapBackend=new AuditBackend;
    GAGCore::BinaryOutputStream mapStream(mapBackend);
    for(size_t i=0;i<game.map.tiles.size();++i)
    {
        const auto& tile=game.map.tiles[i];
        mapStream.writeUint32(tile.terrain,"terrain");
        mapStream.writeUint32(tile.building,"building");
        mapStream.writeUint32(tile.resource.getUint32(),"resource");
        mapStream.writeUint32(tile.groundUnit,"groundUnit");
        mapStream.writeUint32(tile.airUnit,"airUnit");
        mapStream.writeUint32(tile.forbidden,"forbidden");
        mapStream.writeUint32(tile.guardArea,"guardArea");
        mapStream.writeUint32(tile.clearArea,"clearArea");
        mapStream.writeUint32(tile.scriptAreas,"scriptAreas");
        mapStream.writeUint32(tile.canResourcesGrow,"canResourcesGrow");
        mapStream.writeUint32(tile.fertility,"fertility");
        mapStream.writeUint32(game.map.mapDiscovered[i],"discovered");
    }
    components.push_back(mapBackend->getHash());
    Uint32 worldChecksum=2166136261u;
    for(size_t i=1;i<components.size();++i) { worldChecksum^=components[i]; worldChecksum*=16777619u; }
    out<<",\"world_components\":[";
    for(size_t i=0;i<components.size();++i) { if(i) out<<','; out<<components[i]; }
    out<<']';
    out<<",\"tick\":"<<game.stepCounter<<",\"world_checksum\":"<<worldChecksum
       <<",\"engine_checksum\":"<<rawChecksum
       <<",\"map_header_checksum\":"<<components[0]
       <<",\"rng\":"<<quote(auditRandomState())
       <<",\"game_ended\":"<<(game.isGameEnded?"true":"false")
       <<",\"prestige_reached\":"<<(game.totalPrestigeReached?"true":"false")
       <<",\"players\":[";
    for(int i=0;i<game.gameHeader.getNumberOfPlayers();++i)
    {
        if(i) out<<',';
        Player* player=game.players[i];
        AI* ai=player->ai;
        int population=0;
        for(int u=0;u<Unit::MAX_COUNT;++u)
            if(player->team->myUnits[u]) ++population;
        const std::string implementation=ai ? ai->implementationIdentity() : "human";
        out<<"{\"player\":"<<i<<",\"team\":"<<player->team->teamNumber
           <<",\"population\":"<<population
           <<",\"allies\":"<<player->team->allies
           <<",\"orders_disabled\":"<<(ordersDisabled(i)?"true":"false")
           <<",\"ai_id\":"<<(ai?int(ai->implementationID):-1)
           <<",\"implementation\":"<<quote(implementation)
           <<",\"alive\":"<<(player->team->isAlive?"true":"false")
           <<",\"won\":"<<(player->team->hasWon?"true":"false")
           <<",\"lost\":"<<(player->team->hasLost?"true":"false");
        if(ai)
        {
            AuditBackend* backend=new AuditBackend;
            GAGCore::BinaryOutputStream stream(backend);
            ai->save(&stream);
            out<<",\"serialized_ai_checksum\":"<<backend->getHash();
            AIMaxima::Maxima* maxima=dynamic_cast<AIMaxima::Maxima*>(ai->aiImplementation);
            if(maxima)
            {
                out<<",\"requested\":"<<AIMaxima::StrategyResolver::resolvedJson(
                    game.resolveMaximaStrategy(i))
                   <<",\"actual\":"<<maxima->auditStrategyJson();
            }
        }
        out<<'}';
    }
    out<<']';
    if(phase=="terminal") out<<",\"termination_reason\":"<<quote(
        game.totalPrestigeReached ? "prestige" : game.isGameEnded ? "formal_end" : "unfinished");
    write(phase,out.str());
    if(phase=="start") running=true;
}
void order(Game& game,int player,Order& order,const std::string& phase)
{
    if(!enabled() || order.getOrderType()==ORDER_NULL) return;
    std::ostringstream out,bytes;
    const unsigned char* data=order.getData();
    for(unsigned i=0;i<order.getDataLength();++i)
        bytes<<std::hex<<std::setw(2)<<std::setfill('0')<<int(data[i]);
    out<<",\"tick\":"<<game.stepCounter<<",\"player\":"<<player
       <<",\"team\":"<<game.players[player]->team->teamNumber
       <<",\"order_type\":"<<int(order.getOrderType())<<",\"data_hex\":"<<quote(bytes.str());
    write(phase,out.str());
}
}
