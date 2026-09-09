#include "../src/AIMaximaStaffing.h"

#include <cassert>
#include <iostream>
#include <map>
#include <vector>

using namespace AIMaxima::SwarmStaffing;

static std::map<int, int> byId(const std::vector<Assignment>& assignments)
{
	std::map<int, int> result;
	for(size_t i=0; i<assignments.size(); ++i)
		result[assignments[i].id]=assignments[i].workers;
	return result;
}

static int total(const std::vector<Assignment>& assignments)
{
	int result=0;
	for(size_t i=0; i<assignments.size(); ++i)
		result+=assignments[i].workers;
	return result;
}

static std::vector<Assignment> assign(std::vector<Candidate> candidates,int workers)
{
    return distribute(candidates,workers,20);
}

int main()
{
    // Actual supply, not rank: a colony with nine times the fertile corn
    // receives nine times the labor. An empty/no-supply swarm gets none.
    std::vector<Candidate> swarms={Candidate(20,900),Candidate(5,100),Candidate(10,0)};
    auto assigned=byId(assign(swarms,10));
    assert(assigned[20]==9 && assigned[5]==1 && assigned[10]==0);
    std::reverse(swarms.begin(),swarms.end());
    assert(byId(assign(swarms,10))==assigned);
    assert(total(assign(swarms,0))==0);
    assert(total(assign({Candidate(1,0),Candidate(2,0)},10))==0);
    assert(total(assign({},10))==0);
    assert(byId(assign({Candidate(7,0)},10))[7]==10);
    assert(total(assign({Candidate(7,0)},0))==0);
    assert(total(assign({Candidate(7,0)},-3))==0);
    auto overflow=byId(assign({Candidate(1,900),Candidate(2,100)},35));
    assert(overflow[1]==20 && overflow[2]==15);
    auto tied=byId(assign({Candidate(9,40),Candidate(3,40)},5));
    assert(tied[3]==3 && tied[9]==2);
    for(int count=1;count<=8;++count)
    {
        std::vector<Candidate> candidates;
        for(int i=0;i<count;++i) candidates.push_back(Candidate(100-i,(i+1)*7));
        std::map<int,int> previous;
        for(int workers=0;workers<=40;++workers)
        {
            auto allocations=assign(candidates,workers);
            assert(total(allocations)==std::min(workers,20*count));
            for(const auto& allocation:allocations) assert(allocation.workers<=20);
            for(const auto& allocation:allocations)
            {
                assert(allocation.workers>=previous[allocation.id]);
                previous[allocation.id]=allocation.workers;
            }
        }
    }
    std::cout<<"Maxima staffing standalone tests passed\n";
    return 0;
}
