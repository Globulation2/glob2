// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include <Toolkit.h>
#include <StringTable.h>

using namespace GAGCore;

void SettingsScreen::buildBuildings()
{
    auto& s=globalContainer->settings;
    info(tr("Set starting unit assignments by building type and level."));
    toggle("buildings.remember","Remember assignments between games",
        "Changes you make during play update these defaults for future games.",s.rememberUnit,[this](int v){globalContainer->settings.rememberUnit=v;commit();});
    const char* tabs[]={"Completed","Construction","Upgrades","Flags"};
    for(int i=0;i<4;++i){
        button("buildings.tab."+std::to_string(i),tr(tabs[i]),[this,i]{buildingTab=i;scroll[int(current)]=0;},buildingTab==i);
        form.back().columns=4;form.back().column=i;
    }
    if(buildingTab==3){
        info(tr("Starting unit counts and radius for newly placed flags."));
        const bool table=viewport.w>=620;
        if(table){int col=0;for(auto label:{"Flag","Units","Radius"}){auto& r=add("",Kind::Info,tr(label));r.columns=3;r.column=col++;}}
        for(int t=IntBuildingType::EXPLORATION_FLAG;t<=IntBuildingType::CLEARING_FLAG;++t){
            const int n=t-IntBuildingType::EXPLORATION_FLAG;
            auto name=Toolkit::getStringTable()->getString("["+IntBuildingType::typeFromShortNumber(t)+"]");
            auto& title=add("",table?Kind::Info:Kind::Section,name);
            if(table){title.columns=3;title.column=0;}
            number("units."+std::to_string(t)+".1",table?"":tr("Units"),s.defaultUnitsAssigned[t][1],1,20,[this,t](int v){
                globalContainer->settings.defaultUnitsAssigned[t][1]=v;commit();
            });
            if(table){form.back().columns=3;form.back().column=1;}
            number("radius."+std::to_string(n),table?"":tr("Radius"),s.defaultFlagRadius[n],0,20,[this,n](int v){
                globalContainer->settings.defaultFlagRadius[n]=v;commit();
            });
            if(s.defaultFlagRadius[n]==0)form.back().value=tr("Default");
            if(table){form.back().columns=3;form.back().column=2;}
        }
        return;
    }
    info(tr(buildingTab==0?"Unit counts for completed buildings.":buildingTab==1?
        "Unit counts assigned to new construction sites.":"Unit counts while a building is being upgraded."));
    // At wide sizes use a real comparison table. Narrow forms group levels by
    // building instead; both layouts are generated from the same slot mapping.
    const bool table=viewport.w>=620;
    const int columns=buildingTab==0?4:buildingTab==1?2:3;
    if(table){
        auto& h=add("",Kind::Info,tr("Building"));h.columns=columns;h.column=0;
        for(int col=1;col<columns;++col){auto& r=add("",Kind::Info,tr("Level")+" "+std::to_string(buildingTab==2?col+1:col));r.columns=columns;r.column=col;}
    }
    for(int t=0;t<IntBuildingType::NB_BUILDING;++t){
        if(t==IntBuildingType::EXPLORATION_FLAG || t==IntBuildingType::WAR_FLAG || t==IntBuildingType::CLEARING_FLAG)continue;
        const auto shortName=IntBuildingType::typeFromShortNumber(t);
        const auto name=Toolkit::getStringTable()->getString("["+shortName+"]");
        std::vector<int> slots;
        for(int l=(buildingTab==2?1:0);l<(buildingTab==1?1:3);++l){
            auto* type=globalContainer->buildingsTypes.getByType(shortName,l,buildingTab!=0);
            if(type && (buildingTab!=0 || type->foodable || type->fillable))slots.push_back(l*2+(buildingTab==0?1:0));
            else slots.push_back(-1);
        }
        bool any=false;for(int slot:slots)any|=slot>=0;if(!any)continue;
        auto& title=add("",table?Kind::Info:Kind::Section,name);
        if(table){title.columns=columns;title.column=0;}
        for(size_t i=0;i<slots.size();++i){
            int slot=slots[i];
            if(slot>=0){
                std::string label=table?"":tr("Level")+" "+std::to_string(slot/2+1);
                number("units."+std::to_string(t)+"."+std::to_string(slot),label,s.defaultUnitsAssigned[t][slot],1,20,
                    [this,t,slot](int v){globalContainer->settings.defaultUnitsAssigned[t][slot]=v;commit();});
            }else if(table)add("",Kind::Info,"—");else continue;
            if(table){form.back().columns=columns;form.back().column=int(i)+1;}
        }
    }
}
