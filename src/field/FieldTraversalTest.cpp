// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "PowerOfTwo.h"
#include "map/generator/shared/Grid.h"
#include "field/UniformTraversal.h"
#include "field/Influence.h"
#include "field/GradientPropagation.h"
#include "field/PriorityTraversal.h"
#include <climits>
#include <cstdint>
#include <queue>
#include <random>
#include <vector>

namespace
{
template<class Stencil>
std::vector<int> reference(int w,int h,std::vector<int> values,
    const std::vector<int>& seeds,const Stencil& stencil)
{
    std::queue<int> queue;
    for(int seed:seeds)queue.push(seed);
    while(!queue.empty())
    {
        const int at=queue.front();queue.pop();
        for(auto d:stencil)
        {
            const int next=((at/w+d.y+h)%h)*w+(at%w+d.x+w)%w;
            if(values[next]==INT_MAX)
            { values[next]=values[at]+1;queue.push(next); }
        }
    }
    return values;
}
}
TEST_SUITE("FieldGradient")
{
TEST_CASE("Distance FIFO preserves order through wrap growth and scratch reuse")
{
    field::Frontier queue;
    std::queue<int> expected;
    std::mt19937 random(6921);
    for(int i=0;i<10000;++i)
    {
        if(expected.empty() || random()%3!=0)
        {queue.push_back(i);expected.push(i);}
        else
        {REQUIRE(queue.front()==expected.front());queue.pop_front();expected.pop();}
    }
    while(!expected.empty())
    {REQUIRE(queue.front()==expected.front());queue.pop_front();expected.pop();}
    CHECK(queue.empty());
    const auto retained=queue.capacity();
    queue.push_back(9);queue.clear();queue.push_back(42);
    CHECK(queue.front()==42);
    CHECK(queue.capacity()==retained);
    std::vector<int> values(16,INT_MAX);values[0]=0;
    queue.clear();queue.push_back(0);
    field::expandDistances(values,queue,{4,4},field::Cardinal,INT_MAX);
    CHECK(queue.empty());
    CHECK(values==reference(4,4,std::vector<int>{0,INT_MAX,INT_MAX,INT_MAX,
        INT_MAX,INT_MAX,INT_MAX,INT_MAX,INT_MAX,INT_MAX,INT_MAX,INT_MAX,
        INT_MAX,INT_MAX,INT_MAX,INT_MAX},{0},field::Cardinal));
}
TEST_CASE("Uniform fields preserve metrics obstacles and wrapping")
{
    std::mt19937 random(1943);
    for(int w:{1,2,5,16})for(int h:{1,3,8,17})for(int trial=0;trial<12;++trial)
    {
        std::vector<int> input(w*h,INT_MAX),seeds;
        for(int i=0;i<w*h;++i)
        {
            if(random()%5==0)input[i]=-1;
            else if(random()%7==0){input[i]=2;seeds.push_back(i);}
        }
        const auto check=[&](const auto& stencil) {
            const auto expected=reference(w,h,input,seeds,stencil);
            auto actual=input,queue=seeds;
            field::expandDistances(actual,queue,{w,h},stencil,INT_MAX);
            CHECK(actual==expected);
        };
        check(field::Cardinal);check(field::Surrounding);
    }
}
TEST_CASE("Ordered payload traversal stops on the threshold crossing cell")
{
    const field::Grid grid(5,3);
    std::vector<int> queue{0},depth(15,-1),visited;
    depth[0]=0;
    int sum=0;
    field::traverse(queue,grid,field::Cardinal,[&](int at) {
        visited.push_back(at);sum+=at+1;
        return sum>=13?field::Visit::Stop:field::Visit::Expand;
    },[&](int at,int x,int y) {
        const int next=grid.index(x,y);
        if(depth[next]<0){depth[next]=depth[at]+(next%2);queue.push_back(next);}
    });
    CHECK(visited==std::vector<int>{0,4,1,10});
    CHECK(sum==19);
    CHECK(depth[1]==1);
    CHECK(depth[4]==0);
}
TEST_CASE("Bounded traversal preserves unwrapped domain coordinates")
{
    std::vector<int> queue{0},seen(9,0);
    seen[0]=1;
    const field::Grid grid(3,3);
    field::traverse(queue,grid,field::Surrounding,[](int){return field::Visit::Expand;},
        [&](int,int x,int y) {
            if(x<0||y<0||x>=2||y>=2)return;
            const int next=grid.index(x,y);
            if(!seen[next]){seen[next]=1;queue.push_back(next);}
        });
    CHECK(queue==std::vector<int>{0,1,3,4});
}
TEST_CASE("Directional sweep digests preserve original byte arithmetic and scan order")
{
    // Captured from the original Castor implementation before extraction.
    const struct {int w,h,policy;std::uint64_t digest;} cases[]={
        {1,1,0,0x44bd9ed473cdbb02ULL},{1,1,1,0x44bd9ed473cdbb02ULL},
        {1,16,0,0xbd03644a2a5265d3ULL},{1,16,1,0xbd03644a2a5265d3ULL},
        {1,32,0,0xa8412f939121519eULL},{1,32,1,0xa8412f939121519eULL},
        {8,1,0,0xf4b2d64548794dd7ULL},{8,1,1,0xf4b2d64548794dd7ULL},
        {8,16,0,0x9b226b5c51b70977ULL},{8,16,1,0x8ba2cc0bf153e377ULL},
        {8,32,0,0xfb027b2811211c87ULL},{8,32,1,0xfa3e6959f03f6251ULL},
        {64,1,0,0xfaef731bb0ae9e73ULL},{64,1,1,0xfaef731bb0ae9e73ULL},
        {64,16,0,0x97fddb1d034fac82ULL},{64,16,1,0xb571cc70c828a8e5ULL},
        {64,32,0,0x9a7e39425c0e13aaULL},{64,32,1,0x284b3b7be4b9fe32ULL}};
    for(const auto& c:cases)
    {
        std::vector<std::uint8_t> values(c.w*c.h);
        std::uint32_t random=0x12345678;
        for(auto& v:values){random=1664525*random+1013904223;v=random>>24;}
        if(c.policy)field::directionalInfluence(values.data(),{c.w,c.h},field::BlockedUnitFloor<255>{});
        else field::directionalInfluence(values.data(),{c.w,c.h},field::ZeroFloorPinned<16>{});
        std::uint64_t digest=1469598103934665603ULL;
        for(auto v:values){digest^=v;digest*=1099511628211ULL;}
        CHECK(digest==c.digest);
    }
}
TEST_CASE("Convergent influence agrees with an independent strongest first frontier")
{
    std::mt19937 random(8731);
    for(int w:{1,5,16})for(int h:{1,7,32})for(int trial=0;trial<8;++trial)
    {
        std::vector<std::uint8_t> input(w*h,1);
        for(auto& v:input) { if(random()%5==0)v=0;else if(random()%7==0)v=2+random()%254; }
        auto expected=input,actual=input;
        std::priority_queue<std::pair<int,int>> queue;
        for(int i=0;i<w*h;++i)if(expected[i]>=3)queue.emplace(expected[i],i);
        while(!queue.empty())
        {
            const auto [value,at]=queue.top();queue.pop();
            if(value!=expected[at]||value<3)continue;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            {
                if(!dx&&!dy)continue;
                const int next=((at/w+dy+h)%h)*w+(at%w+dx+w)%w;
                if(expected[next]!=0&&expected[next]<value-1)
                {expected[next]=value-1;queue.emplace(value-1,next);}
            }
        }
        field::ConvergentInfluence solver(actual.data(),{w,h});
        int passes=0;
        if(solver.hasSources())do
        {
            solver.beginPass();
            for(int y=0;y<h;++y)solver.forwardRow(y);
            for(int y=h;y-->0;)solver.reverseRow(y);
            REQUIRE(++passes<=256);
        }while(solver.passChanged());
        CHECK(actual==expected);
    }
}
TEST_CASE("Map independent weighted kernel agrees with a heap oracle")
{
    std::mt19937 random(4328);
    GradientWorkspace workspace;
    for(int w:{1,3,8,32})for(int h:{1,5,16})for(int swim=0;swim<7;++swim)
        for(int limit:{0,5,42,300})
    {
        const int size=w*h;
        std::vector<std::uint8_t> water(size);
        std::vector<std::uint16_t> input(size,1);
        for(int i=0;i<size;++i)
        {
            water[i]=random()%2;
            if(random()%6==0 || (swim==0 && water[i]))input[i]=0;
            else if(random()%7==0)input[i]=65535-random()%100;
        }
        auto expected=input,actual=input;
        using Entry=std::pair<int,int>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
        for(int i=0;i<size;++i)if(expected[i]>1)queue.emplace(65535-expected[i],i);
        constexpr int waterCosts[]={0,5,7,10,13,20,30};
        while(!queue.empty())
        {
            const auto [cost,at]=queue.top();queue.pop();
            if(cost>limit || cost!=65535-expected[at])continue;
            const int step=swim && water[at]?waterCosts[swim]:10;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            {
                if(!dx&&!dy)continue;
                const int next=((at/w+dy+h)%h)*w+(at%w+dx+w)%w;
                const int nextCost=cost+((dx&&dy)?step*14/10:step);
                if(expected[next]!=0 && nextCost<=limit && 65535-nextCost>expected[next])
                {expected[next]=65535-nextCost;queue.emplace(nextCost,next);}
            }
        }
        gradient_kernel::propagateField(actual.data(),swim,limit,
            {w,h},workspace,[&](std::size_t i){return water[i]!=0;});
        CHECK(actual==expected);
    }
}
TEST_CASE("FIFO discovery can stop without admitting later neighbours")
{
    std::vector<int> queue{0},visited;
    field::breadthFirst(queue,[&](int at){visited.push_back(at);return field::Visit::Expand;},
        [&](int at) {
            for(int next=at+1;next<at+4;++next)
            {
                queue.push_back(next);
                if(queue.size()==3)return field::Visit::Stop;
            }
            return field::Visit::Expand;
        });
    CHECK(queue==std::vector<int>{0,1,2});
    CHECK(visited==std::vector<int>{0});
}
TEST_CASE("FIFO visitor stop retains vector history and ring pending entries")
{
    std::vector<int> history{0,1,2};
    field::Frontier pending;
    for(int at:history)pending.push_back(at);
    const auto stop=[](int at) { return at==1?field::Visit::Stop:field::Visit::Skip; };
    int expansions=0;
    const auto expand=[&](int) { ++expansions;return field::Visit::Expand; };
    field::breadthFirst(history,stop,expand);
    field::breadthFirst(pending,stop,expand);
    CHECK(expansions==0);
    CHECK(history==std::vector<int>{0,1,2});
    REQUIRE(!pending.empty());
    CHECK(pending.front()==2);
    pending.pop_front();
    CHECK(pending.empty());
}
TEST_CASE("Priority traversal preserves zero costs stale entries and heap tie order")
{
    using Entry=std::pair<int,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    std::vector<int> distance(4,INT_MAX),visited;
    const field::Grid grid(4,1);
    distance[3]=0;queue.emplace(0,3);queue.emplace(9,0);
    field::traversePriority(queue,grid,field::Cardinal,
        [](const Entry& entry){return entry.second;},
        [&](const Entry& entry) {
            if(entry.first!=distance[entry.second])return field::Visit::Skip;
            visited.push_back(entry.second);return field::Visit::Expand;
        },[&](const Entry& entry,int x,int y) {
            const int next=grid.index(x,y),cost=entry.first+(next==0?0:1);
            if(cost<distance[next]){distance[next]=cost;queue.emplace(cost,next);}
        });
    CHECK(distance==std::vector<int>{0,1,1,0});
    CHECK(visited==std::vector<int>{3,0,1,2});
    CHECK(queue.empty());
}
TEST_CASE("Depth first traversal preserves stack discovery order")
{
    std::vector<int> stack{0},visited;
    field::depthFirst(stack,[&](int at){visited.push_back(at);return field::Visit::Expand;},
        [&](int at) {
            if(at==0){stack.push_back(1);stack.push_back(2);}
            return field::Visit::Expand;
        });
    CHECK(visited==std::vector<int>{0,2,1});
    CHECK(stack.empty());
}
}

TEST_SUITE("FieldGradient")
{
TEST_CASE("Power of two remainders preserve signed and mixed integer arithmetic")
{
    const auto check = [](auto value, auto divisor) {
        CHECK(powerOfTwoRemainder(value, divisor) == value % divisor);
        CHECK(dimensionRemainder(value, divisor) == value % divisor);
    };
    for (unsigned shift=0; shift<31; ++shift)
    {
        const int size=int(1u<<shift);
        for (const int value : {INT_MIN, INT_MIN+1, -65537, -129, -1, 0, 1, 129, 65537, INT_MAX})
        {
            check(value,size);
            check(value,unsigned(size));
            check(unsigned(value),size);
            check(std::int64_t(value),unsigned(size));
            check(std::uint64_t(unsigned(value)),size);
        }
    }
    for (unsigned shift=0; shift<64; ++shift)
    {
        const auto size=std::uint64_t{1}<<shift;
        check(INT64_MIN,size);check(INT64_MAX,size);check(UINT64_MAX,size);
    }
    for (const int size : {3,7,15,17,129})
        for (const int value : {INT_MIN,-65537,-1,0,1,65537,INT_MAX})
        {
            CHECK(dimensionRemainder(value,size) == value%size);
            CHECK(dimensionRemainder(value,unsigned(size)) == value%unsigned(size));
        }
    static_assert(powerOfTwoRemainder(-129,128)==-1);
    static_assert(powerOfTwoRemainder(-129,128u)==127u);
}
}

TEST_SUITE("FieldGradient")
{
TEST_CASE("Generator torus masks retain general and mutated grid wrapping")
{
    static_assert(sizeof(MapGeneration::Torus)==2*sizeof(int), "Script budget accounting must stay unchanged");
    for (const int w : {1,2,7,16,129}) for (const int h : {1,3,8,17})
    {
        MapGeneration::Torus torus(w,h);
        const auto check = [&] {
            for (const int v : {INT_MIN, -65537, -129, -1, 0, 1, 129, 65537, INT_MAX})
            {
                const auto wrap = [](int value,int size) {
                    const int r=value%size;return r<0?r+size:r;
                };
                CHECK(torus.x(v)==wrap(v,torus.w));
                CHECK(torus.y(v)==wrap(v,torus.h));
                CHECK(torus.remainderX(v)==v%torus.w);
                CHECK(torus.remainderY(unsigned(v))==unsigned(v)%torus.h);
            }
        };
        check();torus.w=13;torus.h=16;check();
    }
}
}
