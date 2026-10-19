// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

// Derived provider membership only. Epochs live in AdaptiveGradientState and
// survive loading; rebuilding this sparse footprint cache never advances them.
struct GradientMarketTargets
{
    struct References {
        std::uint32_t occupying=0,overlay=0;
        auto operator<=>(const References&) const = default;
    };
    using Footprints=std::map<std::size_t,References>;
    struct Provider {
        std::uint32_t identity=0;std::uint16_t gid=0;
        int x=0,y=0,width=0,height=0;bool occupying=false;
        auto operator<=>(const Provider&) const = default;
    };
    struct Ledger { bool initialized=false;Footprints cells;std::vector<Provider> providers,scratch; };
    struct Masks {std::uint32_t occupying=0,overlay=0;};
    std::map<std::uint32_t,Ledger> ledgers;
    std::map<std::size_t,std::map<unsigned,Masks>> inverse;
    void update(std::size_t index,unsigned team,unsigned material,References refs) {
        const auto bit=std::uint32_t(1)<<material;
        auto& masks=inverse[index][team];
        masks.occupying=(masks.occupying&~bit)|(refs.occupying?bit:0);
        masks.overlay=(masks.overlay&~bit)|(refs.overlay?bit:0);
        if(!masks.occupying&&!masks.overlay)inverse[index].erase(team);
        if(inverse[index].empty())inverse.erase(index);
    }
    static std::uint32_t key(unsigned team,unsigned material) {return (team<<16)|material;}
    static References at(const Footprints& cells,std::size_t index) {
        const auto found=cells.find(index);return found==cells.end()?References{}:found->second;
    }
    References at(unsigned team,unsigned material,std::size_t index) const {
        const auto found=ledgers.find(key(team,material));return found==ledgers.end()?References{}:at(found->second.cells,index);
    }
    bool initialized(unsigned team,unsigned material) const {
        const auto found=ledgers.find(key(team,material));return found!=ledgers.end()&&found->second.initialized;
    }
    template<class Changed> static void diff(const Footprints& before,const Footprints& after,Changed changed) {
        auto a=before.begin(),b=after.begin();
        while(a!=before.end() || b!=after.end()) {
            if(b==after.end() || (a!=before.end()&&a->first<b->first)) {changed(a->first,a->second,References{});++a;}
            else if(a==before.end() || b->first<a->first) {changed(b->first,References{},b->second);++b;}
            else {if(a->second!=b->second)changed(a->first,a->second,b->second);++a;++b;}
        }
    }
    void eraseTeam(unsigned team) {
        std::erase_if(ledgers,[&](const auto& item){return (item.first>>16)==team;});
        for(auto it=inverse.begin();it!=inverse.end();) {it->second.erase(team);if(it->second.empty())it=inverse.erase(it);else ++it;}
    }
};
