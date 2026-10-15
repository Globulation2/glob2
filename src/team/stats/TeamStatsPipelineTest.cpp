// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include <algorithm>
#include "ScopedEnvironment.h"
#include "Engine.h"
#include "Player.h"
#include "AI.h"
#include "AIImplementation.h"
#include "ai/engine/AIDecision.h"
#include "Order.h"
#include "Building.h"
#include "TeamStatsPipeline.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <ThreadSupport.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <map>
#include <tuple>
#include <ChecksumStream.h>
#include "StatsChecksum.h"

namespace
{
auto prepareAIOrders(Game& game, std::span<const unsigned> eligible, bool paused) {
    const auto world=game.observeCompletedWorld();
    game.admitAIOrders(eligible,paused,{},world);
    return game.deliverAIOrders(eligible,paused);
}

SimulationSnapshot::Handle input(glob2test::HeadlessGame &w)
{
	w.team->stats.enableCollection();
	w.game.snapshots().invalidateBoundary();
	return w.game.captureReadBoundary();
}
class StatisticsBoundaryObserver final : public AIImplementation
{
  public:
	std::shared_ptr<const AIEngine::AIWorldView> observation;
	bool load(GAGCore::InputStream *, Player *, Sint32) override { return true; }
	void save(GAGCore::OutputStream *) override {}
	bool supportsObservation() const override { return true; }
	std::shared_ptr<Order> getOrder() override { throw std::logic_error("live statistics observation"); }
	std::shared_ptr<Order> getOrder(const AIEngine::DecisionContext &context) override
	{
		observation = context.observation;
		return std::make_shared<NullOrder>();
	}
};
} // namespace
TEST_SUITE("TeamStatsPipeline")
{
	TEST_CASE("direct scalar checksums match canonical binary bytes")
	{
		auto *backend = new StatsChecksumBackend;
		GAGCore::BinaryOutputStream binary(backend);
		GAGCore::ChecksumStream direct;
		const auto write = [](GAGCore::OutputStream &out) {
			out.writeEnterSection("ignored section");
			out.writeSint8(-128,"signed byte"); out.writeUint8(255,"byte");
			out.writeSint16(-32768,"signed short"); out.writeUint16(65535,"short");
			out.writeSint32(std::numeric_limits<Sint32>::min(),"signed word");
			out.writeUint32(0xffffffffu,"word"); out.writeOffset32(123,"offset");
			out.writeFloat(-0.0f,"float"); out.writeFloat(1.25f,"float");
			out.writeDouble(-3.5,"double");
			out.writeDouble(std::numeric_limits<double>::infinity(),"double");
			out.writeText(std::string("a\0b",3),"text");
			const Uint16 values[]{0,1,65535}; out.writeUint16Sections(values,3,"array");
			out.writeLeaveSection();
		};
		write(binary); write(direct);
		CHECK(direct.value == backend->value);
		CHECK(direct.getPosition() == binary.getPosition());
	}

	TEST_CASE("routine statistics coverage excludes diagnostic ledgers and full checks retain them")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true});
		w.team->stats.enableCollection();
		const auto light=w.game.teamStatsChecksum(), full=w.game.teamStatsChecksum(true);
		++w.team->stats.events().births[WORKER];
		CHECK(w.game.teamStatsChecksum()==light);
		CHECK(w.game.teamStatsChecksum(true)!=full);
		++w.team->stats.getLatestStat()->totalUnit;
		CHECK(w.game.teamStatsChecksum()!=light);
	}
    TEST_CASE("secondary statistics are periodic while verification and entity checks stay immediate")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true, .header=true});
        auto* unit=w.addUnit(WORKER,30,30);
        auto* building=w.addBuilding("swarm",10,10);
        w.game.stepCounter=1;
        const auto light=w.game.teamStatsChecksum(), full=w.game.teamStatsChecksum(true);
        ++w.team->stats.getLatestStat()->totalUnit;
        CHECK(w.game.teamStatsChecksum()==light);
        CHECK(w.game.teamStatsChecksum(true)!=full);
        w.game.stepCounter=32;
        const auto periodic=w.game.teamStatsChecksum();
        ++w.team->stats.getLatestStat()->totalUnit;
        CHECK(w.game.teamStatsChecksum()!=periodic);
        w.game.stepCounter=33;
        const auto before=w.game.checkSum();
        --unit->hp;
        CHECK(w.game.checkSum()!=before);
        const auto beforeRandom=w.game.checkSum(); unit->entityRandom.nextU32();
        CHECK(w.game.checkSum()!=beforeRandom);
        std::vector<Uint32> fields;
        CHECK(unit->checkSum(nullptr)==unit->checkSum(&fields));
        CHECK_FALSE(fields.empty()); fields.clear();
        CHECK(building->checkSum(nullptr)==building->checkSum(&fields));
        CHECK_FALSE(fields.empty());
    }
    TEST_CASE("remote map sweeps are periodic and explicit verification remains exhaustive")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true, .header=true});
        w.game.players[0]->type=BasePlayer::P_IP;
        w.game.stepCounter=1;
        const auto routine=w.game.checkSum(), full=w.game.checkSum(nullptr,nullptr,nullptr,true);
        w.game.map.addForbidden(40,40,0);
        CHECK(w.game.checkSum()==routine);
        CHECK(w.game.checkSum(nullptr,nullptr,nullptr,true)!=full);
        w.game.stepCounter=32;
        const auto periodic=w.game.checkSum();
        w.game.map.removeForbidden(40,40,0);
        CHECK(w.game.checkSum()!=periodic);
    }
	TEST_CASE("raw snapshot samples retain synchronous aggregation and smoothing")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w(
			glob2test::GameOptions{.wDec = 6, .hDec = 6, .loadDefaultRace = true});
		auto *worker = w.addUnit(WORKER, 30, 30);
		w.addUnit(WARRIOR, 35, 30);
		w.addBuilding("inn", 20, 20);
		TeamStats reference;
		for (unsigned tick = 0; tick < 64; ++tick)
		{
			w.game.stepCounter = tick;
			worker->hp -= tick % 2;
			w.game.snapshots().invalidateBoundary();
			const auto snapshot = w.game.captureReadBoundary();
			const auto sample = computeTeamStatsSample(snapshot, 0, tick % 32 == 31);
			// The legacy scan observes this exact world, before any simulation advances.
			w.team->stats.step(w.team);
			reference.applySample(sample, tick, w.team->race.getCatalog());
			CHECK(reference.measurements() == w.team->stats.measurements());
			CHECK(reference.getLatestStat()->totalUnit == w.team->stats.getLatestStat()->totalUnit);
			CHECK(reference.getWorkersBalance() == w.team->stats.getWorkersBalance());
			CHECK(reference.getLatestStat()->totalHP == w.team->stats.getLatestStat()->totalHP);
			CHECK(reference.getLatestStat()->totalAttackPower ==
				  w.team->stats.getLatestStat()->totalAttackPower);
		}
	}

    TEST_CASE("lagged windows include current samples and never feed approximations into the raw ring")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned delay : {0u,1u,2u,8u}) {
            CAPTURE(delay);
            glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true});
            auto* worker=w.addUnit(WORKER,10,10);
            w.game.gameHeader.setTeamStatsDelay(delay);
            TeamStatsPipeline pipeline(w.game.map.computeExecutor());
            for(unsigned tick=0;tick<104;++tick) {
                w.game.stepCounter=tick;
                pipeline.publishDue(w.game);
                worker->medical=Unit::MED_FREE;
                worker->activity=tick==30 ? Unit::ACT_RANDOM : Unit::ACT_FILLING;
                pipeline.prepareBoundary(w.game,input(w));
                pipeline.drain(); // Finished early; publication must still use the captured window.
                pipeline.publishDue(w.game);
                if(tick==31+std::max(1u,delay)) CHECK(w.team->stats.getLatestStat()->totalFree==(delay<=1 ? 1 : 0));
                if(tick==63+std::max(1u,delay)) CHECK(w.team->stats.getLatestStat()->totalFree==(delay==8 ? 1 : 0));
                if(tick==95+std::max(1u,delay)) CHECK(w.team->stats.getLatestStat()->totalFree==0);
            }
        }
    }
    TEST_CASE("format 156 raw aggregate samples retain exact smoothing after loading and resaving")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true});
        auto* worker=w.addUnit(WORKER,10,10);
        for(unsigned tick=0;tick<31;++tick) { w.game.stepCounter=tick; worker->activity=Unit::ACT_FILLING; w.team->stats.step(w.team); }
        w.game.stepCounter=31; worker->activity=Unit::ACT_RANDOM; worker->medical=Unit::MED_FREE;
        TeamStatsPipeline pipeline(w.game.map.computeExecutor());
        auto sample=computeTeamStatsSample(input(w),0,true);
        REQUIRE_FALSE(sample.finalized);
        struct OldOutput : GAGCore::BinaryOutputStream {
            using BinaryOutputStream::BinaryOutputStream;
            void writeUint8(Uint8 value,std::string_view name) override { if(name!="finalized" && name!="completeFrame") BinaryOutputStream::writeUint8(value,name); }
        };
        auto* bytes=new GAGCore::MemoryStreamBackend;
        OldOutput old(bytes); TeamStats::saveSample(&old,sample);
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition())); in.seekFromStart(0);
        auto loaded=TeamStats::loadSample(&in,156);
        CHECK_FALSE(loaded.finalized);
        auto* currentBytes=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream current(currentBytes); TeamStats::saveSample(&current,loaded);
        GAGCore::BinaryInputStream resaved(new GAGCore::MemoryStreamBackend(currentBytes->getBuffer(),currentBytes->getPosition())); resaved.seekFromStart(0);
        auto restored=TeamStats::loadSample(&resaved);
        CHECK_FALSE(restored.finalized);
        w.team->stats.applySample(std::move(restored),39,w.team->race.getCatalog());
        CHECK(w.team->stats.getLatestStat()->totalFree==1);
    }
    TEST_CASE("format 157 pending measurement deltas migrate and resave without early publication")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true});
        w.addUnit(WORKER,10,10);
        w.game.gameHeader.setTeamStatsDelay(8);
        w.team->stats.editMeasurements().labour[GameplayMeasurements::IDLE]=100;
        const auto baseline=w.team->stats.measurements();
        TeamStatsPipeline capture(w.game.map.computeExecutor());
        std::vector<TeamStatsSample> raw;
        for (unsigned tick=0;tick<2;++tick) {
            w.game.stepCounter=tick;
            raw.push_back(computeTeamStatsSample(input(w),0,false));
        }
        struct Format157Output : GAGCore::BinaryOutputStream {
            using BinaryOutputStream::BinaryOutputStream;
            void writeUint8(Uint8 value,std::string_view name) override {
                if(name!="completeFrame") BinaryOutputStream::writeUint8(value,name);
            }
        };
        auto* bytes=new GAGCore::MemoryStreamBackend;
        Format157Output old(bytes);
        old.writeEnterSection("TeamStatsPipeline");
        old.writeUint8(1,"hasLast"); old.writeUint32(1,"last");
        old.writeUint32(1,"teams"); old.writeUint32(2,"phase"); old.writeUint32(2,"pending");
        for (unsigned i=0;i<raw.size();++i) {
            old.writeEnterSection(i); old.writeUint32(i,"tick"); old.writeUint32(i+8,"due");
            TeamStats::saveSample(&old,raw[i]); old.writeLeaveSection();
        }
        old.writeLeaveSection();
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition())); in.seekFromStart(0);
        TeamStatsPipeline loaded(w.game.map.computeExecutor()); loaded.load(w.game,&in,157);
        CHECK(w.team->stats.measurements()==baseline);
        auto* currentBytes=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream current(currentBytes); loaded.save(&current);
        GAGCore::BinaryInputStream resaved(new GAGCore::MemoryStreamBackend(currentBytes->getBuffer(),currentBytes->getPosition())); resaved.seekFromStart(0);
        TeamStatsPipeline restored(w.game.map.computeExecutor()); restored.load(w.game,&resaved);
        CHECK(restored.checkSum(true)==loaded.checkSum(true));
        auto expected=baseline;
        for (unsigned tick=2;tick<=10;++tick) {
            w.game.stepCounter=tick;
            restored.publishDue(w.game);
            if(tick>=8) {
                auto sample=tick<10 ? raw[tick-8] : computeTeamStatsSample(input(w),0,false);
                // The unchanged fixture has the same instantaneous row at source 2 and source 10.
                if(tick==10) { sample.tick=2; sample.measurements.tick=2; }
                TeamStats::completeMeasurements(sample,expected);
                expected=sample.measurements;
                CHECK(w.team->stats.measurements()==expected);
            }
            if(tick<10) restored.prepareBoundary(w.game,input(w));
        }
    }
    TEST_CASE("complete frames preserve cumulative measurements and retained readers")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned workers : {0u,2u}) for (unsigned delay : {0u,1u,2u,8u}) {
            CAPTURE(workers); CAPTURE(delay);
            glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true});
            auto* unit=w.addUnit(WORKER,10,10);
            w.game.map.configureCompute(workers+1);
            w.game.gameHeader.setTeamStatsDelay(delay);
            TeamStatsPipeline pipeline(w.game.map.computeExecutor());
            TeamStats reference;
            std::vector<GameplayMeasurements> expected;
            std::shared_ptr<const GameplayMeasurements> retained;
            GameplayMeasurements retainedValue;
            for (unsigned tick=0;tick<520;++tick) {
                w.game.stepCounter=tick;
                pipeline.publishDue(w.game);
                ++w.team->stats.events().harvested[0];
                unit->hp=100-tick%10;
                const auto snapshot=input(w);
                reference.applySample(computeTeamStatsSample(snapshot,0,tick%32==31),tick,w.team->race.getCatalog());
                expected.push_back(reference.measurements());
                pipeline.prepareBoundary(w.game,snapshot);
                pipeline.publishDue(w.game);
                if (tick>=std::max(1u,delay)) {
                    CHECK(w.team->stats.measurements()==expected[tick-std::max(1u,delay)]);
                    if (!retained) {
                        retained=w.team->stats.publishedMeasurementFrame();
                        REQUIRE(retained);
                        retainedValue=*retained;
                    }
                    CHECK(*retained==retainedValue);
                }
            }
            pipeline.drain();
            // Explicit cold edits detach a retained frame; ordinary readers cannot mutate it.
            const auto latest=w.team->stats.publishedMeasurementFrame();
            const auto before=*latest;
            ++w.team->stats.editMeasurements().harvested[0];
            CHECK(*latest==before);
        }
    }

	TEST_CASE("publication is fixed to source tick plus delay and boundaries are idempotent")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned delay : {0u, 1u, 2u, 8u})
		{
			CAPTURE(delay);
			glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace = true});
			w.addUnit(WORKER, 10, 10);
			w.game.gameHeader.setTeamStatsDelay(delay);
			w.game.map.configureCompute(1);
			TeamStatsPipeline pipeline(w.game.map.computeExecutor());
			for (unsigned tick = 0; tick < 40; ++tick)
			{
				w.game.stepCounter = tick;
				pipeline.publishDue(w.game);
				const auto world = input(w);
				pipeline.prepareBoundary(w.game, world);
				pipeline.prepareBoundary(w.game, world);
				pipeline.drain(); // Early completion cannot publish.
				if (tick < std::max(1u,delay))
					CHECK(w.team->stats.measurements().tick == 0);
				pipeline.publishDue(w.game);
				if (tick >= std::max(1u,delay))
				{
					CHECK(w.team->stats.sourceTick == tick - std::max(1u,delay));
					CHECK(w.team->stats.publishedTick == tick);
				}
				CHECK(pipeline.metrics.submitted == tick + 1);
				CHECK(pipeline.metrics.published == (tick >= std::max(1u,delay) ? tick - std::max(1u,delay) + 1 : 0));
				CHECK(pipeline.metrics.peakPending <= std::max(1u,delay) + 1);
			}
		}
	}
	TEST_CASE("delayed statistics AI and presentation share one capture per boundary")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned participants : {1u, 3u})
		{
			CAPTURE(participants);
			glob2test::HeadlessGame w({.teams = 2, .loadDefaultRace = true, .header = true});
			w.addUnit(WORKER, 10, 10);
			w.addUnit(WORKER, 20, 20, 1);
			REQUIRE(w.game.gameHeader.getTeamStatsDelay() == 8);
			w.game.gameHeader.setAIOrderDelay(0);
			w.game.map.configureCompute(participants);
			std::array<StatisticsBoundaryObserver *, 2> observers;
			constexpr std::array<unsigned, 2> players{0, 1};
			for (auto player : players)
			{
				w.game.players[player]->makeItAI(AI::NUMBI);
				auto &implementation = w.game.players[player]->ai->aiImplementation;
				delete implementation;
				implementation = observers[player] = new StatisticsBoundaryObserver;
			}
			for (unsigned tick = 0; tick < 40; ++tick)
			{
				CAPTURE(tick);
				w.game.stepCounter = tick;
				const auto captures = w.game.snapshots().metrics.captures;
				const auto world = w.game.observeCompletedWorld({.presentation = true});
				REQUIRE(w.game.snapshots().metrics.captures == captures + 1);
				REQUIRE((w.game.admitAIOrders(players,false,{},world), w.game.deliverAIOrders(players,false)).size() == players.size());
				w.game.drainAI();
				REQUIRE(observers[0]->observation);
				CHECK(observers[1]->observation == observers[0]->observation);
				const auto &observed = observers[0]->observation->components();
				CHECK(observed.entities == world.entities);
				CHECK(observed.teams == world.teams);
				CHECK(observed.statisticsInputs == world.statisticsInputs);
				const auto display = world.presentationView();
				CHECK(display.entities == observed.entities);
				CHECK(display.teams == observed.teams);
				CHECK_FALSE(display.statisticsInputs);
				const auto statistics = world.statisticsView();
				CHECK(statistics.entities == observed.entities);
				CHECK(statistics.statisticsInputs == observed.statisticsInputs);
				CHECK_FALSE(statistics.session);
				CHECK_FALSE(statistics.statistics);
				CHECK_FALSE(statistics.resourceFields);
				if (tick)
					CHECK_FALSE(statistics.terrain);
				const auto repeated = w.game.observeCompletedWorld({.presentation = true});
				CHECK(repeated.entities == world.entities);
				CHECK(repeated.teams == world.teams);
				CHECK(w.game.snapshots().metrics.captures == captures + 1);
				if (tick >= 8)
				{
					CHECK(w.team->stats.sourceTick == tick - 8);
					CHECK(w.team->stats.publishedTick == tick);
				}
				if (tick == 39)
				{
					CHECK(world.teams->values[0].statistics.totalUnit == 1);
					CHECK(display.statistics->teams[0]->getLatestStat()->totalUnit == 1);
				}
			}
		}
	}
	TEST_CASE("worker counts produce identical publication and pending sample checksums")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned delay : {0u, 1u, 2u, 8u})
		{
			std::vector<Uint32> expected;
			for (unsigned workers : {0u, 1u, 3u})
			{
				glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace = true});
				auto *unit = w.addUnit(WORKER, 10, 10);
				w.game.map.configureCompute(workers + 1);
				w.game.gameHeader.setTeamStatsDelay(delay);
				TeamStatsPipeline p(w.game.map.computeExecutor());
				std::vector<Uint32> trace;
				for (unsigned tick = 0; tick < 70; ++tick)
				{
					w.game.stepCounter = tick;
					p.publishDue(w.game);
					unit->hp = 100 - tick % 10;
					++w.team->stats.events().harvested[0];
					p.prepareBoundary(w.game, input(w));
					p.publishDue(w.game);
					trace.push_back(p.checkSum());
					const auto light = p.checkSum();
					trace.push_back(p.checkSum(true));
					CHECK(p.checkSum() == light);
					trace.push_back(w.team->stats.checkSum());
				}
				if (!workers)
					expected = trace;
				else
					CHECK(trace == expected);
			}
		}
	}
	TEST_CASE("light checks do not execute jobs and full checks cover computed pending samples")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame a(glob2test::GameOptions{.loadDefaultRace = true});
		glob2test::HeadlessGame b(glob2test::GameOptions{.loadDefaultRace = true});
		a.game.map.configureCompute(1); b.game.map.configureCompute(1);
		a.game.gameHeader.setTeamStatsDelay(8); b.game.gameHeader.setTeamStatsDelay(8);
		a.addUnit(WORKER, 10, 10)->hp = 10;
		b.addUnit(WORKER, 10, 10)->hp = 20;
        CHECK(a.game.checkSum() != b.game.checkSum()); // World state covers input differences.
        ++b.team->stats.events().births[WORKER];
		TeamStatsPipeline pa(a.game.map.computeExecutor()), pb(b.game.map.computeExecutor());
		pa.prepareBoundary(a.game, input(a)); pb.prepareBoundary(b.game, input(b));
		const auto light = pa.checkSum();
		CHECK(light == pb.checkSum());
		CHECK(a.game.map.computeExecutor().metrics().ownerJobs == 0);
		CHECK(pa.checkSum(true) != pb.checkSum(true));
        const auto frozen = pb.checkSum(true);
        ++b.team->stats.events().births[WORKER];
        CHECK(pb.checkSum(true) == frozen);
		CHECK(pa.checkSum() == light);
		CHECK(pa.metrics.published == 0);
		CHECK(pa.checkSum(true) == pa.checkSum(true));
	}
	TEST_CASE("save drains work without moving deadlines and restores pending samples")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace = true});
		w.addUnit(WORKER, 10, 10);
		w.game.gameHeader.setTeamStatsDelay(8);
		w.game.map.configureCompute(2);
		TeamStatsPipeline original(w.game.map.computeExecutor());
		for (unsigned tick = 0; tick < 5; ++tick)
		{
			w.game.stepCounter = tick;
			original.prepareBoundary(w.game, input(w));
		}
		const auto before = original.checkSum();
		const auto fullBefore = original.checkSum(true);
		auto *memory = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(memory);
		original.save(&output);
		CHECK(original.checkSum() == before);
		CHECK(original.metrics.published == 0);
		GAGCore::BinaryInputStream in(
			new GAGCore::MemoryStreamBackend(memory->getBuffer(), memory->getPosition()));
		TeamStatsPipeline restored(w.game.map.computeExecutor());
		in.seekFromStart(0);
		restored.load(w.game, &in);
		CHECK(restored.checkSum(true) == fullBefore);
		CHECK(restored.checkSum() == before);
		w.game.stepCounter = 8;
		CHECK(restored.publishDue(w.game));
		CHECK(w.team->stats.sourceTick == 0);
		CHECK(w.team->stats.publishedTick == 8);
		w.game.stepCounter = 9;
		CHECK(restored.publishDue(w.game));
		CHECK(w.team->stats.sourceTick == 1);
	}

    TEST_CASE("format 155 pending digests are discarded without losing samples")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace=true});
        TeamStatsPipeline original(w.game.map.computeExecutor());
        original.prepareBoundary(w.game,input(w));
        struct LegacyOutput : GAGCore::BinaryOutputStream {
            using BinaryOutputStream::BinaryOutputStream;
            void writeUint8(Uint8 value,std::string_view name) override {
                if(name!="completeFrame" && name!="finalized") BinaryOutputStream::writeUint8(value,name);
            }
            void writeUint32(Uint32 value, std::string_view name) override {
                BinaryOutputStream::writeUint32(value,name);
                if(name=="due") BinaryOutputStream::writeUint32(0xdeadbeef,"inputChecksum");
            }
        };
        auto* memory=new GAGCore::MemoryStreamBackend;
        LegacyOutput output(memory);
        original.save(&output);
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(memory->getBuffer(),memory->getPosition()));
        in.seekFromStart(0);
        TeamStatsPipeline restored(w.game.map.computeExecutor());
        restored.load(w.game,&in,155);
        CHECK(restored.checkSum(true)==original.checkSum(true));
        CHECK(in.getPosition()==memory->getPosition());
    }
	TEST_CASE("inspection admits no work and paused or waiting frames reuse the initial observation")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace = true});
		w.addUnit(WORKER, 10, 10);
		w.game.gameHeader.setTeamStatsDelay(0);
		w.game.captureReadBoundary({.presentation = true});
		CHECK(w.game.teamStatsMetrics().empty());
		const auto initial=w.game.observeCompletedWorld({.presentation = true});
        const auto admitted=w.game.teamStatsMetrics();
        const auto captures=w.game.snapshots().metrics.captures;
        const auto copied=w.game.snapshots().metrics.entityBytesCopied;
        const auto before=w.team->stats.checkSum();
        for(unsigned frame=0;frame<3;++frame) {
            w.game.anyPlayerWaited=frame==1;
            const auto retained=w.game.observeCompletedWorld({.presentation = true});
            CHECK(retained.entities==initial.entities);
            CHECK(w.game.teamStatsMetrics()==admitted);
            CHECK(w.game.snapshots().metrics.captures==captures);
            CHECK(w.game.snapshots().metrics.entityBytesCopied==copied);
        }
        w.game.anyPlayerWaited=false;
		CHECK(w.team->stats.checkSum() == before);
		for (unsigned tick = 0; tick < 33; ++tick)
		{
			w.game.stepCounter = tick;
			w.game.observeCompletedWorld({.presentation = true});
		}
		const auto world = w.game.observeCompletedWorld({.presentation = true});
		CHECK(world.teams->values[0].statistics.totalUnit == 1);
		CHECK(world.statistics->teams[0]->getLatestStat()->totalUnit == 1);
		CHECK(w.team->stats.sourceTick == 31);
		CHECK(w.team->stats.publishedTick == 32);
	}
	TEST_CASE("failed computation or submission remains a failure at later owner barriers")
	{
		glob2test::HeadlessGlobals globals;
		enum class Failure { NextBoundaryCalculation, DeferredCalculation, Submission };
		for (auto failure : {Failure::NextBoundaryCalculation, Failure::DeferredCalculation, Failure::Submission})
		{
			CAPTURE(failure);
			const unsigned delay = failure == Failure::NextBoundaryCalculation ? 0 : 2;
			glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace = true});
			auto *unit = w.addUnit(WORKER, 10, 10);
			w.game.map.configureCompute(1);
			w.game.gameHeader.setTeamStatsDelay(delay);
			TeamStatsPipeline p(w.game.map.computeExecutor());
			const auto id = unit->typeNum;
			if (failure != Failure::Submission)
				unit->typeNum = 1000;
			else
			{
				// Submission failure must also remain terminal. With no workers,
				// these no-op jobs hold every executor slot until teardown.
				const ComputeExecutor::Group group{1, {[](void *, size_t) {}, nullptr}};
				for (size_t i = 0; i < ComputeExecutor::Slots; ++i)
					w.game.map.computeExecutor().submit(std::span(&group, 1), 1000);
			}
			const auto captured = input(w);
			if (failure == Failure::Submission)
				CHECK_THROWS(p.prepareBoundary(w.game, captured));
			else
				p.prepareBoundary(w.game, captured);
			unit->typeNum = id;
			w.game.stepCounter = std::max(1u,delay);
			CHECK_THROWS(p.publishDue(w.game));
			CHECK_THROWS(p.prepareBoundary(w.game, captured));
			CHECK_THROWS(p.drain());
			auto *memory = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream output(memory);
			CHECK_THROWS(p.save(&output));
		}
	}
	TEST_CASE("full game save preserves statistics across smoothing and history boundaries")
	{
		glob2test::HeadlessGlobals globals;
		GameHeader header;
		header.setNumberOfPlayers(1);
		header.setRandomSeed(123456);
		header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
		auto map = Engine::loadMapHeader("maps/balanced.map");
		for (unsigned checkpoint : {30u, 510u})
		{
			GameGUI source;
			REQUIRE(source.loadFromHeaders(map, header, true, true));
			source.game.map.configureCompute(1);
			for (unsigned tick = 0; tick < checkpoint; ++tick)
				source.game.step(0);
			auto *memory = new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream out(memory);
			source.save(&out, "statistics continuation");
			out.flush();
			GameGUI restored;
			GAGCore::BinaryInputStream in(
				new GAGCore::MemoryStreamBackend(memory->getBuffer(), memory->getPosition()));
			in.seekFromStart(0);
			REQUIRE(restored.load(&in));
			restored.game.map.configureCompute(2);
			CHECK(source.game.teamStatsChecksum() == restored.game.teamStatsChecksum());
			for (unsigned tick = 0; tick < 20; ++tick)
			{
				source.game.step(0);
				restored.game.step(0);
				CHECK(source.game.teamStatsChecksum() == restored.game.teamStatsChecksum());
				CHECK(source.game.teams[0]->stats.measurements() ==
					  restored.game.teams[0]->stats.measurements());
				CHECK(source.game.teams[0]->stats.measurementHistory ==
					  restored.game.teams[0]->stats.measurementHistory);
			}
		}
	}
	TEST_CASE("late workers are joined at the deadline without choosing publication time")
	{
		if (!GAGCore::ThreadSupport::available)
			return;
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w(glob2test::GameOptions{.loadDefaultRace = true});
		w.addUnit(WORKER, 10, 10);
		w.game.map.configureCompute(2);
		w.game.gameHeader.setTeamStatsDelay(2);
		struct Gate
		{
			std::atomic<bool> started = false, release = false, deadline = false;
		} gate;
		ComputeExecutor::Group blocker{1,
									   {[](void *context, size_t)
										{
											auto &g = *static_cast<Gate *>(context);
											g.started = true;
											g.started.notify_all();
											g.release.wait(false);
										},
										&gate},
									   ComputeExecutor::NoLane};
		auto ticket = w.game.map.computeExecutor().submit(std::span(&blocker, 1), 0);
		gate.started.wait(false);
		TeamStatsPipeline p(w.game.map.computeExecutor());
		p.prepareBoundary(w.game, input(w));
		w.game.stepCounter = 1;
		CHECK_FALSE(p.publishDue(w.game));
		CHECK(p.metrics.published == 0);
		std::jthread release(
			[&]
			{
				gate.deadline.wait(false);
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				gate.release = true;
				gate.release.notify_all();
			});
		w.game.stepCounter = 2;
		gate.deadline = true;
		gate.deadline.notify_all();
		CHECK(p.publishDue(w.game));
		CHECK(w.team->stats.sourceTick == 0);
		CHECK(w.team->stats.publishedTick == 2);
		w.game.map.computeExecutor().join(ticket);
	}
	TEST_CASE("pipeline versus synchronous snapshot boundary timing [benchmark]" *
			  doctest::skip(std::getenv("GLOB2_STATS_BENCHMARK") == nullptr))
	{
		glob2test::HeadlessGlobals globals;
		const unsigned ticks = std::getenv("GLOB2_STATS_BENCHMARK_TICKS")
			? std::strtoul(std::getenv("GLOB2_STATS_BENCHMARK_TICKS"), nullptr, 10) : 1024;
		REQUIRE(ticks > 0);
		REQUIRE(ticks <= 1000000);
		const bool checksumEnabled = std::getenv("GLOB2_STATS_BENCHMARK_CHECKSUM") != nullptr;
		for (int mode = 0; mode < 3; ++mode)
		{
			if (const char *selected = std::getenv("GLOB2_STATS_BENCHMARK_MODE");
				selected && mode != std::atoi(selected))
				continue;
			glob2test::HeadlessGame w(glob2test::GameOptions{
				.wDec = 8, .hDec = 7, .teams = 2, .loadDefaultRace = true, .seed = 1234567});
			for (int t = 0; t < 2; ++t)
			{
				for (int i = 0; i < 128; ++i)
					w.addUnit(i % 3, 16 + t * 128 + i % 64, 16 + 3 * (i / 64), t);
				w.addBuilding("inn", 4 + t * 128, 100, 0, t);
			}
			const unsigned delayedTicks=std::getenv("GLOB2_STATS_BENCHMARK_DELAY") ? std::strtoul(std::getenv("GLOB2_STATS_BENCHMARK_DELAY"),nullptr,10) : 8;
            REQUIRE(delayedTicks>=1); REQUIRE(delayedTicks<=8);
            w.game.gameHeader.setTeamStatsDelay(mode == 1 ? 0 : delayedTicks);
			w.game.map.configureCompute(mode == 2 ? 3 : 1);
			TeamStatsPipeline pipeline(w.game.map.computeExecutor());
			if (mode)
				for (int t = 0; t < 2; ++t)
					w.game.teams[t]->stats.enableCollection();
			const auto start = std::chrono::steady_clock::now();
			Uint64 checksumNs = 0;
			Uint32 checksum = 0;
			for (unsigned tick = 0; tick < ticks; ++tick)
			{
				w.game.stepCounter = tick;
				if (mode)
					pipeline.publishDue(w.game);
				const auto world = w.game.captureReadBoundary();
				if (mode)
				{
					pipeline.prepareBoundary(w.game, world);
					pipeline.publishDue(w.game);
				}
				else
					for (int t = 0; t < 2; ++t)
						w.game.teams[t]->stats.step(w.game.teams[t]);
				if (checksumEnabled)
				{
					const auto checksumStart = std::chrono::steady_clock::now();
					checksum ^= pipeline.checkSum();
					checksum ^= w.game.teamStatsChecksum();
					checksumNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - checksumStart).count();
				}
			}
			const auto ownerNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
									 std::chrono::steady_clock::now() - start)
									 .count();
			pipeline.drain();
			for (int t = 0; t < 2; ++t)
				CHECK(w.game.teams[t]->stats.getLatestStat()->totalUnit == 128);
			std::cout << "STATS_BENCH mode="
					  << (mode == 0   ? "synchronous"
						  : mode == 1 ? "snapshot_immediate"
									  : "snapshot_delayed")
					  << " delay=" << w.game.gameHeader.getTeamStatsDelay() << " seed=1234567 ticks=" << ticks << " units=256 owner_ns=" << ownerNs
					  << " checksum_ns=" << checksumNs << " checksum=" << checksum
					  << " peak_pending=" << pipeline.metrics.peakPending
					  << " peak_leased_bytes=" << w.game.snapshots().memoryMetrics().peakLeasedBytes
					  << '\n';
		}
	}

    TEST_CASE("advancing match statistics timing [benchmark]" * doctest::skip(std::getenv("GLOB2_STATS_BENCHMARK")==nullptr))
    {
        glob2test::HeadlessGlobals globals;
        const unsigned ticks=std::getenv("GLOB2_STATS_BENCHMARK_TICKS") ? std::strtoul(std::getenv("GLOB2_STATS_BENCHMARK_TICKS"),nullptr,10) : 1024;
        REQUIRE(ticks>0); REQUIRE(ticks<=1000000);
        const bool verifyTrace=std::getenv("GLOB2_STATS_BENCHMARK_CHECKSUM")!=nullptr;
        std::map<std::tuple<bool,int,int>,std::vector<Uint32>> expected;
        for(bool ai : {false,true}) for(int teams : {2,4}) for(int workers : {0,2}) for(int delay : {0,1,2,8}) {
            glob2test::HeadlessGame w(glob2test::GameOptions{.wDec=8,.hDec=8,.teams=teams,.loadDefaultRace=true,.header=true,.seed=1234567});
            for(int t=0;t<teams;++t) {
                const int x=16+(t%2)*128, y=16+(t/2)*128;
                auto* inn=w.addBuilding("inn",x,y,0,t);
                auto* swarm=w.addBuilding("swarm",x+16,y,0,t);
                for(auto* building : {inn,swarm}) { building->setMaterial(WHEAT, building->type->maxMaterial[WHEAT]); building->update(); }
                for(int row=0;row<8;++row) for(int col=0;col<24;++col) {
                    const int rx=x+col, ry=y+48+row;
                    w.game.map.setResourceByIndex(rx,ry,row<6 ? WHEAT : row==6 ? WOOD : STONE,0);
                    w.game.map.setResourceAmount(w.game.map.coordToIndex(rx,ry),65535);
                }
                if(ai) {
                    auto* team=w.game.teams[t]; team->startPosX=x; team->startPosY=y; team->startPosSet=Team::START_POS_FROM_UNIT;
                    w.game.players[t]->makeItAI(t%2 ? AI::CASTOR : AI::NUMBI);
                }
                for(int i=0;i<128;++i) w.addUnit(i%3,x+i%64,y+16+3*(i/64),t);
            }
            w.game.gameHeader.setTeamStatsDelay(delay);
            w.game.map.configureCompute(workers+1);
            std::vector<unsigned> actors; for(int t=0;t<teams;++t) actors.push_back(t);
            w.game.map.setMapDiscovered();
            Uint64 orders=0;
            std::vector<Uint32> trace;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned tick=0;tick<ticks;++tick) {
                if(ai) for(const auto& [player,candidate] : prepareAIOrders(w.game,actors,false)) {
                    auto order=w.game.validateAIOrder(candidate,player); REQUIRE(order); order->sender=player;
                    orders+=order->getOrderType()!=ORDER_NULL; w.game.executeOrder(order,0);
                }
                w.step();
                if(verifyTrace) trace.push_back(w.game.checkSum(nullptr,nullptr,nullptr,true));
            }
            const auto owner=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"STATS_ADVANCING teams="<<teams<<" workers="<<workers<<" delay="<<delay<<" ticks="<<ticks<<" seed=1234567 ai="<<(ai ? "numbi-castor" : "none")<<" orders="<<orders<<" owner_ns="<<owner;
            for(const auto& [name,value]:w.game.teamStatsMetrics()) std::cout<<' '<<name<<'='<<value;
            const auto memory=w.game.snapshots().memoryMetrics();
            std::cout<<" peak_leased_bytes="<<memory.peakLeasedBytes<<" checksum="<<w.checksum()<<'\n';
            CHECK(w.game.stepCounter==ticks);
            if(verifyTrace) {
                const auto key=std::tuple{ai,teams,delay};
                if(auto it=expected.find(key);it!=expected.end()) CHECK(trace==it->second);
                else expected.emplace(key,trace);
                for(unsigned tick=0;tick<trace.size();++tick)
                    std::cout<<"STATS_TRACE ai="<<ai<<" teams="<<teams<<" workers="<<workers<<" delay="<<delay<<" tick="<<tick<<" checksum="<<trace[tick]<<'\n';
            }
        }
    }
	TEST_CASE("periodic snapshot places match live coverage at wrapped footprint boundaries")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame w(
			glob2test::GameOptions{.wDec = 6, .hDec = 6, .teams = 2, .loadDefaultRace = true});
		w.game.stepCounter = 512;
		w.addBuilding("inn", 61, 61, 0, 0);
		w.addBuilding("inn", 30, 30, 0, 1);
		auto *unit = w.addUnit(WARRIOR, 10, 10, 0);
		w.addUnit(WARRIOR, 63, 63, 1);
		for (int t = 0; t < 2; ++t)
			w.game.teams[t]->stats.refreshMeasurements(w.game.teams[t]);
		TeamStatsPipeline pipeline(w.game.map.computeExecutor());
		for (int x : {0, 7, 15, 16, 17, 31, 48, 63})
			for (int y : {0, 15, 16, 17, 48, 63})
			{
				unit->posX = x;
				unit->posY = y;
				w.team->stats.sampleDefence(w.team);
				w.game.snapshots().invalidateBoundary();
				const auto world =
					w.game.captureReadBoundary();
				const auto sample = computeTeamStatsSample(world, 0, false);
				const auto &actual = sample.measurements;
				const auto &expected = w.team->stats.editMeasurements();
				CHECK(std::equal(std::begin(actual.warriors), std::end(actual.warriors),
								 std::begin(expected.warriors)));
				CHECK(std::equal(std::begin(actual.warriorLevels), std::end(actual.warriorLevels),
								 std::begin(expected.warriorLevels)));
				CHECK(actual.intruders == expected.intruders);
			}
	}
}
