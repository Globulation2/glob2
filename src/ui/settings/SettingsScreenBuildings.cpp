// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "SettingsScreen.h"
#include "GlobalContainer.h"
#include "BuildingType.h"
#include <Toolkit.h>
#include <StringTable.h>
#include <FormatableString.h>
#include <set>

using namespace GAGCore;
namespace {
std::string buildingName(const BuildingType& type)
{
    const auto& name=type.presentation.displayName.empty() ? type.key : type.presentation.displayName;
    const auto key="["+name+"]";
    auto* strings=Toolkit::getStringTable();
    return strings->doesStringExist(key) ? strings->getString(key) : name;
}
bool attracts(const BuildingType& type)
{
    return type.zonable[WORKER] || type.zonable[WARRIOR] || type.zonable[EXPLORER];
}
std::string variantName(const BuildingType& type)
{
    auto name=buildingName(type);
    if(type.presentation.showLevel) name+=" · "+std::to_string(type.level+1);
    return name;
}
}

void SettingsScreen::buildBuildings()
{
    auto& catalog=globalContainer->buildingsTypes;
    auto& settings=globalContainer->settings;
    if(phoneLayout && selectedBuilding>=0)
    {
        buildBuildingDetail(selectedBuilding);
        return;
    }
    const auto fingerprint=catalog.fingerprint();
    auto remember=[&] {
        toggle("buildings.remember","Remember assignments between games",
            "Changes you make during play update these defaults for future games.",settings.rememberUnit,
            [this](int value) {globalContainer->settings.rememberUnit=value;commit();});
    };
    if(phoneLayout)
    {
        for(std::size_t i=0;i<catalog.size();++i)
        {
            const auto& type=*catalog.get(i);
            if(!type.semantics.placeable) continue;
            const int completed=type.isBuildingSite ? type.nextLevel : int(i);
            const auto& shown=*catalog.get(completed);
            button("buildings.open."+type.key,
                FormattableString(tr("%0 · %1 Units")).arg(buildingName(shown)).arg(settings.buildingAssignment(fingerprint,shown)),
                [this,i] {selectedBuilding=int(i);resetScroll();});
            form.back().buildingIcon=completed;
        }
        remember();
        return;
    }
    info(tr("Set starting unit assignments by building type and level."));
    remember();
    const char* tabs[]={"Completed","Construction","Upgrades","Flags"};
    for(int i=0;i<4;++i)
    {
        button("buildings.tab."+std::to_string(i),tr(tabs[i]),[this,i] {buildingTab=i;resetScroll();},buildingTab==i);
        form.back().columns=4;form.back().column=i;
    }
    info(tr(buildingTab==0 ? "Unit counts for completed buildings." : buildingTab==1 ?
        "Unit counts assigned to new construction sites." : buildingTab==2 ?
        "Unit counts while a building is being upgraded." : "Starting unit counts and radius for newly placed flags."));
    const bool table=wideTable;
    const int columns=buildingTab==3 ? 3 : 2;
    if(table)
    {
        auto& title=add("",Kind::Info,tr("Building"));title.columns=columns;title.column=0;
        auto& count=add("",Kind::Info,tr("Units"));count.columns=columns;count.column=1;
        if(columns==3) {auto& radius=add("",Kind::Info,tr("Radius"));radius.columns=columns;radius.column=2;}
    }
    for(std::size_t i=0;i<catalog.size();++i)
    {
        const auto& type=*catalog.get(i);
        const bool flag=attracts(type) && type.semantics.placeable;
        const bool selected=buildingTab==3 ? flag : buildingTab==0 ? !type.isBuildingSite && !flag :
            type.isBuildingSite && (buildingTab==1 ? type.semantics.placeable : !type.semantics.placeable);
        if(!selected || (buildingTab!=3 && type.semantics.assignmentLimit==0)) continue;
        auto& title=add("",table?Kind::Info:Kind::Section,variantName(type));
        if(table) {title.columns=columns;title.column=0;}
        number("units."+type.key,table?"":tr("Units"),settings.buildingAssignment(fingerprint,type),0,type.semantics.assignmentLimit,
            [this,i,fingerprint](int value) {globalContainer->settings.setBuildingAssignment(fingerprint,*globalContainer->buildingsTypes.get(i),value);commit();});
        if(table) {form.back().columns=columns;form.back().column=1;}
        if(buildingTab==3)
        {
            number("radius."+type.key,table?"":tr("Radius"),settings.buildingRadius(fingerprint,type),0,type.maxUnitStayRange,
                [this,i,fingerprint](int value) {globalContainer->settings.setBuildingRadius(fingerprint,*globalContainer->buildingsTypes.get(i),value);commit();});
            if(table) {form.back().columns=columns;form.back().column=2;}
        }
    }
}

void SettingsScreen::buildBuildingDetail(int id)
{
    const auto& catalog=globalContainer->buildingsTypes;
    if(id<0 || std::size_t(id)>=catalog.size()) return;
    const auto fingerprint=catalog.fingerprint();
    auto& settings=globalContainer->settings;
    std::set<int> visited;
    while(id>=0 && visited.insert(id).second)
    {
        const auto& type=*catalog.get(id);
        if(type.semantics.assignmentLimit>0 || attracts(type))
        {
            add("",Kind::Section,variantName(type));
            if(type.isBuildingSite) info(tr(type.semantics.placeable ? "Construction" : "Upgrades"));
            else info(tr("Completed"));
            number("units."+type.key,tr("Units"),settings.buildingAssignment(fingerprint,type),0,type.semantics.assignmentLimit,
                [this,id,fingerprint](int value) {globalContainer->settings.setBuildingAssignment(fingerprint,*globalContainer->buildingsTypes.get(id),value);commit();});
            if(attracts(type) && type.semantics.placeable)
                number("radius."+type.key,tr("Radius"),settings.buildingRadius(fingerprint,type),0,type.maxUnitStayRange,
                    [this,id,fingerprint](int value) {globalContainer->settings.setBuildingRadius(fingerprint,*globalContainer->buildingsTypes.get(id),value);commit();});
        }
        id=type.nextLevel;
    }
}
