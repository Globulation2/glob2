from pathlib import Path
S=Path(__import__('sys').argv[1]);f=S/'src/map/gradient/ResourceGradientProbe.cpp';s=f.read_text();s=s.replace('#include <stdexcept>','#include <stdexcept>\n#include <memory>')
marker='namespace {\n'
insert='''// BEGIN ISOLATED PAIRED-HORIZON ACCUMULATOR
struct PairedShortCounters {
    unsigned queries=0;
    std::int64_t deferred=0,refreshed=0;
    static constexpr bool eligible(unsigned age,unsigned delay) {return age>=delay && age<32;}
    static constexpr bool complete(unsigned age) {return age>=32;}
    void record(unsigned age,unsigned delay,std::int64_t a,std::int64_t b) {
        if(eligible(age,delay)){++queries;deferred+=a;refreshed+=b;}
    }
};
static_assert(!PairedShortCounters::eligible(7,8));
static_assert(PairedShortCounters::eligible(8,8));
static_assert(PairedShortCounters::eligible(31,8));
static_assert(!PairedShortCounters::eligible(32,8));
static_assert(!PairedShortCounters::complete(31) && PairedShortCounters::complete(32));
// END ISOLATED PAIRED-HORIZON ACCUMULATOR
struct PairedStudy {
    std::ofstream output;
    std::map<const ResourceGradientProbe::Episode*,PairedShortCounters> episodes;
    explicit PairedStudy(const std::filesystem::path& path):output(path) {
        if(!output)throw std::runtime_error("Cannot open paired horizon diagnostics");
        output<<"tick,family,team,movement,material,key,serving_capture_tick,publication_delay,observed_through_tick,short_complete,full_complete,short_queries,defer_extra_32,refresh_extra_32,full_queries,defer_extra_128,refresh_extra_128\\n";
    }
};
// No registry initialization, streams or allocations occur with the probe off.
// deque references survive append; entries are erased before each episode pop.
auto& pairedStudies() {
    static std::map<const ResourceGradientProbe*,std::unique_ptr<PairedStudy>> studies;
    return studies;
}
'''
assert s.count(marker)==1;s=s.replace(marker,marker+insert,1)
needle='    seedScratch.materials.disabled=true;'
s=s.replace(needle,'    pairedStudies().emplace(this,std::make_unique<PairedStudy>(std::filesystem::path(path).parent_path()/"paired-horizons.csv"));\n'+needle,1)
s=s.replace('finishStudy(lastTick);writeMovementTotals();}','finishStudy(lastTick);writeMovementTotals();pairedStudies().erase(this);}',1)
needle="<<o.openedQueries<<'\\n';\n}"
repl="""<<o.openedQueries<<'\\n';
    auto& paired=*pairedStudies().at(this);
    const auto& shortWindow=paired.episodes.at(&e);
    paired.output<<e.tick<<',';identity(paired.output,field);
    paired.output<<','<<e.servingCaptureTick<<','<<e.delay<<','<<lastTick<<','
        <<(complete||PairedShortCounters::complete(unsigned(lastTick-e.tick)))<<','<<complete<<','
        <<shortWindow.queries<<','<<shortWindow.deferred<<','<<shortWindow.refreshed<<','
        <<e.queries<<','<<e.deferExtra<<','<<e.refreshExtra<<'\\n';
    paired.episodes.erase(&e);
}"""
assert s.count(needle)==1;s=s.replace(needle,repl,1)
s=s.replace('f.episodes.clear();}study.flush();}','f.episodes.clear();}study.flush();pairedStudies().at(this)->output.flush();}',1)
needle='f.episodes.push_back(std::move(e));f.lastEpisode=lastTick;'
assert s.count(needle)==1;s=s.replace(needle,'f.episodes.push_back(std::move(e));pairedStudies().at(this)->episodes.emplace(&f.episodes.back(),PairedShortCounters{});f.lastEpisode=lastTick;',1)
needle='const auto a=quality(e.deferred.data()),b=quality(e.refreshed.data());++e.queries;'
assert s.count(needle)==1;s=s.replace(needle,'const auto a=quality(e.deferred.data()),b=quality(e.refreshed.data());pairedStudies().at(this)->episodes.at(&e).record(unsigned(tick-e.tick),e.delay,a[0],b[0]);++e.queries;',1)
f.write_text(s)
