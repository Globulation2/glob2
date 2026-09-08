// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameGUITouch.h"
#include "GameGUI.h"
#include "GameGUIInternal.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "UnitDisplayNames.h"
#include <Toolkit.h>
#include <StringTable.h>
using namespace GAGCore;

Building* GameGUITouch::inspectedBuilding() const
{
    if (gui.selectionMode!=GameGUI::BUILDING_SELECTION || globalContainer->replaying) return nullptr;
    auto* b=gui.selectionBuilding();
    return b && b->owner->teamNumber==gui.localTeamNo ? b : nullptr;
}
std::vector<int> GameGUITouch::allocationTabs() const
{
    std::vector<int> result;
    if (auto* b=allocationBuilding()) {
        result={0,1};
        if (b->type->defaultUnitStayRange) result.push_back(2);
    }
    result.push_back(3);
    result.push_back(4);
    return result;
}
std::vector<GameGUITouch::BuildingAction> GameGUITouch::buildingActions() const
{
    std::vector<BuildingAction> result;
    auto* b=inspectedBuilding(); if (!b) return result;
    auto tr=[](const char* key) { return std::string(Toolkit::getStringTable()->getString(key)); };
    if (b->type->unitProductionTime) {
        const auto ratio=gui.displayedRatio(*b);
        for (int i=0;i<NB_UNIT_TYPE;++i) result.push_back({std::string(getUnitName(i))+": "+std::to_string(ratio[i]),0,i});
    }
    if (b->type->type=="clearingflag") {
        for (int i=0;i<BASIC_COUNT;++i) if (i!=STONE)
            result.push_back({getResourceName(i),1,i,gui.displayedClearingResource(*b,i)});
    }
    if (b->type->type=="warflag") {
        for (int i=0;i<NB_UNIT_LEVELS;++i)
            result.push_back({tr("[Min required level:]")+" "+std::to_string(i+1),2,i,gui.displayedMinLevelToFlag(*b)==i});
    }
    if (b->type->type=="explorationflag") {
        const char* names[]={"[any explorer]","[ground attack]"};
        for (int i=0;i<EXPLORATION_FLAG_OPTION_COUNT;++i)
            result.push_back({tr(names[i]),2,i,gui.displayedMinLevelToFlag(*b)==i});
    }
    if (b->constructionResultState==Building::REPAIR) result.push_back({tr("[cancel repair]"),3});
    else if (b->constructionResultState==Building::UPGRADE) result.push_back({tr("[cancel upgrade]"),3});
    else if (b->buildingState==Building::ALIVE && !b->type->isBuildingSite) {
        if (b->hp<b->type->hpMax && b->type->regenerationSpeed==0 &&
            b->isHardSpaceForBuildingSite(Building::REPAIR) && gui.localTeam->maxBuildLevel()>=b->type->level)
            result.push_back({tr("[repair]"),3});
        else if (b->hp==b->type->hpMax && b->type->nextLevel!=-1 &&
            b->isHardSpaceForBuildingSite(Building::UPGRADE) && gui.localTeam->maxBuildLevel()>b->type->level)
            result.push_back({tr("[upgrade]"),3});
    }
    if (b->buildingState==Building::WAITING_FOR_DESTRUCTION) result.push_back({tr("[cancel destroy]"),4});
    else if (b->buildingState==Building::ALIVE) {
        result.push_back({(confirmDestroy ? tr("[ok]")+": " : "")+tr("[destroy]"),4});
        if (confirmDestroy) result.push_back({tr("[Cancel]"),5});
    }
    return result;
}
void GameGUITouch::drawBuildingActions()
{
    auto* gfx=globalContainer->gfx; const auto content=panelContent();
    const double unit=gfx->logicalUnitsPerPoint();
    const auto rows=buildingActions();
    for (size_t i=0;i<rows.size();++i) {
        ViewRect rect{content.x,content.y+(i*56-actionScroll)*unit,content.w,48*unit};
        if (rect.y<content.y || rect.y+rect.h>content.y+content.h) continue;
        const auto& row=rows[i];
        gfx->drawFilledRect(int(rect.x),int(rect.y),int(rect.w),int(rect.h),row.selected ? Color(55,100,75) : Color(30,45,55));
        if (row.kind==0) {
            drawPointLabel({rect.x,rect.y,48*unit,rect.h},"−");
            drawPointLabel({rect.x+48*unit,rect.y,rect.w-96*unit,rect.h},row.label);
            drawPointLabel({rect.x+rect.w-48*unit,rect.y,48*unit,rect.h},"+");
        } else drawPointLabel(rect,(row.kind==1 || row.kind==2 ? (row.selected ? "[x] " : "[ ] ") : "")+row.label);
    }
    const double extent=rows.size()*56*unit;
    if (extent>content.h) gfx->drawFilledRect(int(content.x+content.w-3*unit),int(content.y+actionScroll*unit*content.h/extent),
        std::max(1,int(2*unit)),std::max(1,int(content.h*content.h/extent)),Color(170,185,190));
}
std::optional<GameGUITouch::BuildingAction> GameGUITouch::actionAt(ViewPoint point) const
{
    const auto content=panelContent(); const double unit=globalContainer->gfx->logicalUnitsPerPoint();
    const double y=(point.y-content.y)/unit+actionScroll;
    const auto rows=buildingActions(); const int i=int(y/56);
    if (!content.contains(point) || y<0 || i>=int(rows.size()) || y-i*56>=48) return std::nullopt;
    // Partially clipped rows are not actionable until scrolled fully into view.
    if (i*56-actionScroll<0 || (i*56-actionScroll+48)*unit>content.h) return std::nullopt;
    return rows[i];
}
void GameGUITouch::tapBuildingAction(ViewPoint point)
{
    auto* b=inspectedBuilding(); if (!b) return;
    const auto picked=actionAt(point);
    if (!picked || picked->kind!=heldActionKind || picked->value!=heldActionValue || heldActionConfirmation!=confirmDestroy) return;
    const auto row=*picked;
    const auto content=panelContent(); const double unit=globalContainer->gfx->logicalUnitsPerPoint();
    if (row.kind==0) {
        int delta=point.x<content.x+48*unit ? -1 : point.x>=content.x+content.w-48*unit ? 1 : 0;
        auto values=gui.displayedRatio(*b); const int next=std::clamp(values[row.value]+delta,0,int(MAX_RATIO_RANGE));
        if (next==values[row.value]) return;
        values[row.value]=next; gui.pendingFor(b->gid).pendingRatio=values;
        gui.orderQueue.push_back(std::make_shared<OrderModifySwarm>(b->gid,values.data()));
    } else if (row.kind==1) {
        std::array<bool,BASIC_COUNT> values; bool wire[BASIC_COUNT];
        for (int k=0;k<BASIC_COUNT;++k) values[k]=wire[k]=gui.displayedClearingResource(*b,k)^(k==row.value);
        gui.pendingFor(b->gid).pendingClearingResources=values;
        gui.orderQueue.push_back(std::make_shared<OrderModifyClearingFlag>(b->gid,wire));
    } else if (row.kind==2) {
        if (gui.displayedMinLevelToFlag(*b)==row.value) return;
        gui.pendingFor(b->gid).pendingMinLevelToFlag=row.value;
        gui.orderQueue.push_back(std::make_shared<OrderModifyMinLevelToFlag>(b->gid,row.value));
    } else if (row.kind==5) confirmDestroy=false;
    else {
        if (row.kind==4 && b->buildingState==Building::ALIVE && !confirmDestroy) { confirmDestroy=true; return; }
        gui.handleMenuClickBuildingSelection(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH+40,
            globalContainer->gfx->getH()-(row.kind==3 ? 40 : 16),SDL_BUTTON_LEFT);
        confirmDestroy=false;
    }
}
