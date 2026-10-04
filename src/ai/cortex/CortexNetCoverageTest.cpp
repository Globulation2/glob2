// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ai/cortex/CortexNet.h"
#include "ai/cortex/CortexConstants.h"
#include <array>
#include <vector>

namespace
{
void word(std::vector<Uint8>& blob, Sint32 value)
{
    const auto bits=static_cast<Uint32>(value);
    for (int shift : {0,8,16,24}) blob.push_back(static_cast<Uint8>(bits >> shift));
}
std::vector<Uint8> net(int inputs, int outputs)
{
    // Two hidden signals: relu(x0/2 - x1 + 1), relu(x1 - 2).
    // Logit k: (k+1)*hidden0 - hidden1 + k/4.
    std::vector<Uint8> blob;
    for (int value : {0x434e5831,1,16,2,3,inputs,2,outputs,inputs,2}) word(blob,value);
    for (int i=0; i<inputs; ++i) word(blob,i==0 ? 32768 : i==1 ? -65536 : 0);
    for (int i=0; i<inputs; ++i) word(blob,i==1 ? 65536 : 0);
    word(blob,65536); word(blob,-131072);
    word(blob,2); word(blob,outputs);
    for (int k=0; k<outputs; ++k) { word(blob,(k+1)*65536); word(blob,-65536); }
    for (int k=0; k<outputs; ++k) word(blob,k*16384);
    return blob;
}
void replaceWord(std::vector<Uint8>& blob,size_t index,Uint32 value)
{
    for (int i=0; i<4; ++i) blob.at(index*4+i)=static_cast<Uint8>(value>>(8*i));
}
}

TEST_SUITE("CortexNetCoverage")
{
    TEST_CASE("integer inference matches an independent two-signal oracle")
    {
        const auto blob=net(48,18);
        Cortex::CortexNet model;
        REQUIRE(model.loadFromMemory(blob.data(),blob.size(),48,18));
        for (int x : {-7,-1,0,1,3,20}) for (int y : {-3,0,2,7})
        {
            CAPTURE(x); CAPTURE(y);
            int features[48]{}; features[0]=x; features[1]=y;
            Sint32 logits[18]{};
            model.forwardDecide(features,logits);
            const int first=std::max(0,x*32768-y*65536+65536);
            const int second=std::max(0,y*65536-131072);
            for (int k=0; k<18; ++k) CHECK(logits[k] == (k+1)*first-second+k*16384);
            CHECK(model.scoreDecision(features,0) == -1);
            CHECK(model.scoreDecision(features,(1u<<18)-1) == 17);
            CHECK(model.scoreDecision(features,(1u<<2)|(1u<<7)) == 7);
            CHECK(model.scoreDecision(features,1u<<3) == 3);
        }
    }

    TEST_CASE("equal negative logits select the lowest eligible class")
    {
        auto blob=net(48,18);
        // Make output weights zero and all biases negative and equal.
        const size_t outputWeights=10+48*2+2+2;
        for (int k=0; k<36; ++k) replaceWord(blob,outputWeights+k,0);
        for (int k=0; k<18; ++k) replaceWord(blob,outputWeights+36+k,static_cast<Uint32>(-65536));
        Cortex::CortexNet model;
        REQUIRE(model.loadFromMemory(blob.data(),blob.size(),48,18));
        int features[48]{};
        CHECK(model.scoreDecision(features,(1u<<4)|(1u<<11)) == 4);
        CHECK(model.scoreDecision(features,1u<<31) == -1);
    }

    TEST_CASE("worker caps mask impossible outputs and clamp depleted wheat")
    {
        const auto blob=net(16,20);
        Cortex::CortexNet model;
        REQUIRE(model.loadFromMemory(blob.data(),blob.size(),16,20));
        int features[16]{};
        CHECK(model.chooseSwarmWorkers(features,0,1,-1) == Cortex::CORTEX_SWARM_WORKER_CAP);
        CHECK(model.chooseSwarmWorkers(features,Cortex::CORTEX_SWARM_CAP_LIFT_BUILDLEVEL,0,100) == Cortex::CORTEX_SWARM_WORKER_CAP);
        CHECK(model.chooseSwarmWorkers(features,Cortex::CORTEX_SWARM_CAP_LIFT_BUILDLEVEL,1,100) == Cortex::CORTEX_SWARM_WORKER_CAP_LATE);
        for (int wheat : {0,Cortex::CORTEX_SWARM_WHEAT_STARVED_TILES-1})
            CHECK(model.chooseSwarmWorkers(features,3,10,wheat) == Cortex::CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP);
        CHECK(model.chooseSwarmWorkers(features,0,1,Cortex::CORTEX_SWARM_WHEAT_STARVED_TILES) == Cortex::CORTEX_SWARM_WORKER_CAP);
    }

    TEST_CASE("truncated blobs and invalid header or layer contracts are rejected")
    {
        const auto good=net(48,18);
        glob2test::CapturedStderr diagnostics;
        Cortex::CortexNet model;
        // Every byte boundary crosses the header, architecture, weights and biases.
        for (size_t size=0; size<good.size(); ++size)
        {
            CAPTURE(size);
            CHECK_FALSE(model.loadFromMemory(good.data(),size,48,18));
        }
        for (const auto& change : std::array<std::pair<size_t,Uint32>,9>{{
            {0,0},{1,2},{2,8},{3,1},{4,1},{5,16},{7,20},{8,47},{9,3}}})
        {
            auto bad=good; replaceWord(bad,change.first,change.second);
            CAPTURE(change.first); CAPTURE(change.second);
            CHECK_FALSE(model.loadFromMemory(bad.data(),bad.size(),48,18));
        }
        REQUIRE(model.loadFromMemory(good.data(),good.size(),48,18));
        CHECK_FALSE(model.loadFromMemory(good.data(),good.size(),16,20));
        REQUIRE(model.loadFromMemory(good.data(),good.size(),48,18));
    }

    TEST_CASE("file loaders reject missing empty and wrong-net blobs")
    {
        glob2test::TempDir scratch;
        Cortex::CortexNet model;
        glob2test::CapturedStderr diagnostics;
        CHECK_FALSE(model.load((scratch.path/"missing").string()));
        glob2test::writeFile(scratch.path/"empty","");
        CHECK_FALSE(model.loadDecide((scratch.path/"empty").string()));
        const auto blob=net(48,18);
        glob2test::writeFile(scratch.path/"model",std::string(blob.begin(),blob.end()));
        CHECK_FALSE(model.load((scratch.path/"model").string()));
        CHECK(model.loadDecide((scratch.path/"model").string()));
    }
}
