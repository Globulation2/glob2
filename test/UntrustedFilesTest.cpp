// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Version.h"
#include "AI.h"
#include "AICabino.h"
#include "NetEngine.h"
#include "shared_runtime/Runtime.h"
#include "Order.h"
#include "FileFormatVersions.h"
#include <GzipUtil.h>
#include <filesystem>
#include <fstream>
#include <BinaryStream.h>
#include <map>

namespace {
struct ReadCondition : AISharedRuntime::Conditions::Condition {
    using Condition::load_condition;
};
struct Fields : GAGCore::BinaryOutputStream {
    std::map<std::string, std::pair<size_t, size_t>> fields;
    explicit Fields(GAGCore::MemoryStreamBackend* backend) : BinaryOutputStream(backend) {}
    void writeEndianIndependent(const void* value, size_t size, const std::string name) override {
        fields.try_emplace(name, getPosition(), size);
        BinaryOutputStream::writeEndianIndependent(value, size, name);
    }
};
std::unique_ptr<GAGCore::BinaryInputStream> input(const std::string& bytes) {
    auto* backend = new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size());
    backend->seekFromStart(0);
    return std::make_unique<GAGCore::BinaryInputStream>(backend);
}
std::string poisoned(const std::string& bytes, const Fields& out, const char* field, Uint32 value) {
    auto result = bytes;
    const auto [offset, size] = out.fields.at(field);
    for (size_t i=0; i<size; ++i) result[offset+i] = char(value >> (8*(size-i-1)));
    return result;
}
}
TEST_SUITE("UntrustedFiles") {
TEST_CASE("voice payload bounds preserve recorder overshoot and reject excessive work") {
    std::vector<Uint8> bytes(OrderVoiceData::MAX_ENCODED_BYTES + 6, 0);
    bytes[4] = OrderVoiceData::MAX_FRAMES;
    OrderVoiceData order;
    // Old producers flush after crossing their target, not before it.
    CHECK(order.setData(bytes.data(), 2054, VERSION_MINOR));
    CHECK(order.setData(bytes.data(), OrderVoiceData::MAX_ENCODED_BYTES + 5, VERSION_MINOR));
    CHECK_FALSE(order.setData(bytes.data(), bytes.size(), VERSION_MINOR));
    bytes[4] = OrderVoiceData::MAX_FRAMES + 1;
    CHECK_FALSE(order.setData(bytes.data(), 6, VERSION_MINOR));
    CHECK_FALSE(order.setData(bytes.data(), 4, VERSION_MINOR));
}

TEST_CASE("every native AI accepts its initial saved state") {
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options; options.header=true; options.loadDefaultRace=true;
    glob2test::HeadlessGame world(options);
    world.addBuilding("swarm",4,4);
    for (int id=AI::NONE; id<=AI::CABINO; ++id) {
        INFO(id);
        world.game.gameHeader.setAIConfig(0, "");
        AI original(static_cast<AI::ImplementationID>(id),world.game.players[0]);
        auto* backend=new GAGCore::MemoryStreamBackend;
        Fields out(backend);
        original.save(&out);
        auto stream=input(backend->takeContents());
        GAGCore::BinaryInputStream::CheckedReads checked(stream.get());
        AI restored(AI::NONE,world.game.players[0]);
        CHECK(restored.load(stream.get(),VERSION_MINOR));
    }
}
TEST_CASE("AI tags collection counts and recursive predicates are bounded") {
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options; options.header=true;
    glob2test::HeadlessGame world(options);
    auto* player=world.game.players[0];
    auto* backend=new GAGCore::MemoryStreamBackend;
    Fields out(backend);
    AI ai(AI::NONE, player);
    ai.save(&out);
    const auto bytes=backend->takeContents();
    auto bad=input(poisoned(bytes,out,"implementitionID",0xffffffff));
    CHECK_FALSE(ai.load(bad.get(),VERSION_MINOR));
    auto tag=input(std::string(4, char(0xff)));
    CHECK_THROWS_AS(ReadCondition::load_condition(tag.get(),player,VERSION_MINOR),std::runtime_error);
    tag=input(std::string(4, char(0xff)));
    CHECK_THROWS_AS(tag->readCount("size"),std::runtime_error);
    std::string nested;
    for (int i=0;i<100;++i) nested.append("\0\0\0\3",4);
    tag=input(nested);
    CHECK_THROWS_AS(ReadCondition::load_condition(tag.get(),player,VERSION_MINOR),std::runtime_error);
}

TEST_CASE("untrusted names cannot escape the map output directory") {
    CHECK(glob2NameToFilename("maps","Balanced Map","map")=="maps/Balanced_Map.map");
    for (const std::string name : {std::string("../../outside"),std::string("..\\..\\outside"),
            std::string("/tmp/outside"),std::string("C:outside"),std::string("outside\0ignored",15),
            std::string("CON"),std::string("NUL.txt")}) {
        const auto filename=glob2NameToFilename("maps",name,"map");
        CHECK(std::filesystem::path(filename).parent_path()==std::filesystem::path("maps"));
        CHECK(filename.find('\0')==std::string::npos);
        CHECK(filename.find('\\')==std::string::npos);
        CHECK(filename.find(':')==std::string::npos);
        CHECK(std::filesystem::path(filename).extension()==".map");
    }
    CHECK(glob2NameToFilename("maps","CON","map")=="maps/_CON.map");
}
TEST_CASE("network state rejects invalid slots before queue indexing") {
    CHECK_THROWS_AS(NetEngine(-1,0), std::runtime_error);
    CHECK_THROWS_AS(NetEngine(1,1), std::runtime_error);
    NetEngine engine(1,0);
    engine.pushOrder(std::make_shared<NullOrder>(),99,false);
    CHECK_FALSE(engine.orderReceived(99));
    CHECK_FALSE(engine.retrieveOrder(99));
    CHECK_FALSE(engine.allOrdersReceived());
    CHECK_NOTHROW(engine.clearTopOrders());
    engine.pushOrder(std::make_shared<NullOrder>(),0,false);
    CHECK(engine.allOrdersReceived());
    engine.clearTopOrders();
    CHECK_FALSE(engine.allOrdersReceived());
}
TEST_CASE("legacy create order reads only its twenty byte payload") {
    const Uint8 bytes[20] = {};
    auto order = OrderCreate::deserialize(bytes, sizeof(bytes), FILE_FORMAT_VERSION_ORDER_CREATE_FLAG_RADIUS - 1);
    REQUIRE(order);
    CHECK(order->unitWorkingFuture == order->unitWorking);
}
TEST_CASE("terrain loaders reject invalid resources occupants and sector dimensions [save-format]") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    auto* backend = new GAGCore::MemoryStreamBackend;
    Fields out(backend);
    world.game.map.save(&out);
    const auto bytes = backend->takeContents();
    auto header = world.game.mapHeader;
    // Exercise semantic validation after decoding, independent of the tile encoding.
    const auto original=world.game.map.tiles[0];
    for(int field=0;field<5;++field) {
        auto& tile=world.game.map.tiles[0]; tile=original;
        if(field==0) tile.terrain=272;
        if(field==1) tile.building=65534;
        if(field==2) tile.resource.type=254;
        if(field==3) tile.groundUnit=65534;
        if(field==4) tile.airUnit=65534;
        auto* storage=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream writer(storage);
        world.game.map.save(&writer);
        auto stream=input(storage->takeContents());
        Map restored;
        CHECK_FALSE(restored.load(stream.get(),header,&world.game));
    }
    world.game.map.tiles[0]=original;
	auto bad = poisoned(bytes, out, "encoding", 254); // First packed array is the undermap.
	auto stream = input(bad);
	Map restored;
    CHECK_FALSE(restored.load(stream.get(),header,&world.game));
	for (const char *field : {"chunks", "length"})
	{
		auto malformed = input(poisoned(bytes, out, field, 0xffffffff));
		Map map;
		CHECK_FALSE(map.load(malformed.get(), header, &world.game));
	}
	for (const char *field : {"wSector", "hSector"})
	{
		auto stream = input(poisoned(bytes, out, field, 0));
        Map restored;
        CHECK_FALSE(restored.load(stream.get(), header, &world.game));
	}
}
TEST_CASE("compressed file size is checked before allocation") {
    glob2test::TempDir directory;
    const auto path = directory.path / "oversized.game.gz";
    { std::ofstream file(path, std::ios::binary); file.put('x'); }
    std::filesystem::resize_file(path, GAGCore::MAX_COMPRESSED_GAME_FILE_BYTES + 1);
    std::unique_ptr<GAGCore::StreamBackend> stream(GAGCore::openInflatingFileStreamBackend(path.string()));
    CHECK_FALSE(stream->isValid());
}
TEST_CASE("complete saved game rejects poisoned unit state and retains a reproducible fixture [save-format][artifacts]") {
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options; options.header = true;
    glob2test::HeadlessGame world(options);
    world.addBuilding("swarm", 4, 4);
    world.addUnit(WORKER);
    auto* backend = new GAGCore::MemoryStreamBackend;
    Fields out(backend);
    world.game.save(&out, false, "Untrusted file regression");
    const auto bytes = backend->takeContents();
    const auto bad = poisoned(bytes, out, "typeNum", 0x7fffffffu);
    glob2test::writeFile(glob2test::artifactDir()/"valid.game", bytes);
    glob2test::writeFile(glob2test::artifactDir()/"invalid-unit-type.game", bad);
    Game restored(nullptr);
    auto valid = input(bytes);
    REQUIRE(restored.load(valid.get()));
    auto malformed = input(bad);
    CHECK_THROWS_AS(restored.load(malformed.get()), std::runtime_error);
    auto again = input(bytes);
    CHECK(restored.load(again.get()));
}
TEST_CASE("entity loaders reject malicious indices before using them [save-format]") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    auto* unit = world.addUnit(WORKER);
    auto* building = world.addBuilding("swarm", 4, 4);
    REQUIRE(building);
    auto* backend = new GAGCore::MemoryStreamBackend;
    Fields out(backend);
    unit->save(&out);
    const std::string bytes = backend->takeContents();
    for (const auto& [field, value] : std::vector<std::pair<const char*, Uint32>>{
            {"typeNum", 0xffffffff}, {"typeNum", NB_UNIT_TYPE}, {"level", 0xffffffff},
            {"level", NB_UNIT_LEVELS}, {"carriedRessource", 0x7fffffff}, {"destinationPurpose", 0x7fffffff}, {"gid", 0xffff}, {"action", 0xffffffff}, {"medical", 0xffffffff}}) {
        INFO(field); INFO(value);
        auto stream = input(poisoned(bytes, out, field, value));
        CHECK_THROWS_AS(Unit(stream.get(), world.team, VERSION_MINOR), std::runtime_error);
    }
    auto* buildingBackend = new GAGCore::MemoryStreamBackend;
    Fields buildingOut(buildingBackend);
    building->save(&buildingOut);
    const auto buildingBytes = buildingBackend->takeContents();
    for (const auto& [field, value] : std::vector<std::pair<const char*, Uint32>>{
            {"typeNum", 0xffffffff}, {"typeNum", Uint32(globals->buildingsTypes.size())}, {"gid", 0xffff},
            {"unitStayRange", 0xffffffff}, {"minLevelToFlag", 0xffffffff}, {"ratio[0]", 0x7fffffff},
            {"buildingState", 0xffffffff}, {"constructionResultState", 0xffffffff}, {"clearingRessources[3]", 1}}) {
        auto stream = input(poisoned(buildingBytes, buildingOut, field, value));
        CHECK_THROWS_AS(Building(stream.get(), &globals->buildingsTypes, world.team, VERSION_MINOR), std::runtime_error);
    }
}
TEST_CASE("legacy saved instruction pointers must identify statement boundaries [save-format]") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    auto& script = world.game.sgslScript;
    script.sourceCode = "timer(7) space";
    REQUIRE(script.compileScript(&world.game).type == ErrorReport::ET_OK);
    auto* backend = new GAGCore::MemoryStreamBackend;
    Fields out(backend);
    script.save(&out, &world.game);
    const auto bytes = backend->takeContents();
    for (Uint32 pc : {1u, 0xffffffffu, 0x7fffffffu}) {
        INFO(pc);
        MapScriptSGSL restored;
        auto stream = input(poisoned(bytes, out, "ProgramCounter", pc));
        CHECK_FALSE(restored.load(stream.get(), &world.game));
    }
    auto stream = input(bytes);
    MapScriptSGSL restored;
    CHECK(restored.load(stream.get(), &world.game));
}
TEST_CASE("map scripts cannot load local files") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    world.game.mapscript.setMapScript("load(\"/dev/zero\")");
    CHECK_FALSE(world.game.mapscript.compileCode());
}
TEST_CASE("invalid replay order references are ignored before indexing state") {
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options; options.header = true;
    glob2test::HeadlessGame world(options);
    auto* swarm = world.addBuilding("swarm",4,4);
    REQUIRE(swarm);
    Sint32 ratios[NB_UNIT_TYPE] = {0x7fffffff,1,1};
    auto badRatio=std::make_shared<OrderModifySwarm>(swarm->gid,ratios);
    badRatio->sender=0;
    const int oldRatio=swarm->ratio[0];
    world.game.executeOrder(badRatio,0);
    CHECK(swarm->ratio[0]==oldRatio);
    auto cancel=std::make_shared<OrderCancelConstruction>(swarm->gid,1);
    cancel->sender=0;
    CHECK_NOTHROW(world.game.executeOrder(cancel,0));
    auto order = std::make_shared<OrderCreate>(0, 4, 4, -1, 1, 1);
    order->sender = 0;
    CHECK_NOTHROW(world.game.executeOrder(order, 0));
    order->sender = 255;
    CHECK_NOTHROW(world.game.executeOrderAndNotify(order, 0));
    auto quit = std::make_shared<PlayerQuitsGameOrder>(0x7fffffff);
    quit->sender = 0;
    CHECK_NOTHROW(world.game.executeOrder(quit, 0));
    auto alliance = std::make_shared<SetAllianceOrder>(0xffffffff, 0, 0, 0, 0, 0);
    alliance->sender = 0;
    CHECK_NOTHROW(world.game.executeOrder(alliance, 0));
}
}

TEST_CASE("Cabino inn history accepts append positions and rejects invalid ring bounds" * doctest::test_suite("UntrustedFiles"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;options.header=true;
    glob2test::HeadlessGame world(options);
    Cabino::AICabino controller(world.game.players[0]);
    for(const auto [size,pos]:{std::pair<unsigned,unsigned>{0,0},{1,1},{9,9},{10,0},{10,9},{0,1},{9,10},{10,10},{11,0}})
    {
        CAPTURE(size);CAPTURE(pos);
        auto* memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(memory);
        out.writeUint32(1,"inns");out.writeUint32(0,"gid");out.writeUint32(pos,"pos");out.writeUint32(size,"size");
        for(unsigned i=0;i<size;++i) out.writeUint32(i+20,"food_amount");
        auto bytes=memory->takeContents();auto in=input(bytes);
        auto* manager=new Cabino::InnManager(controller); // Registered with, and owned by, controller.
        const bool valid=size<=10 && pos<=size && pos<10;
        REQUIRE(manager->load(in.get(),world.game.players[0],VERSION_MINOR)==valid);
        if(valid) {CHECK(manager->inns[0].pos==pos);CHECK(manager->inns[0].records.size()==size);}
    }
}
