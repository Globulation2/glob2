// SPDX-License-Identifier: GPL-3.0-or-later
#include "CustomGameScreen.h"
#include "GlobalContainer.h"
#include "LobbyControls.h"
#include "LobbyMapPreview.h"
#include "AINames.h"
#include "GenerationService.h"
#include "gui/MobileSafeArea.h"
#include <InterfacePresentation.h>
#include <StringTable.h>

namespace {
std::string tr(const std::string& text) { return Toolkit::getStringTable()->getString("["+text+"]"); }
std::vector<std::string> translated(std::vector<std::string> labels) { for(auto& s:labels)s=tr(s);return labels; }
}

bool CustomGameScreen::usesResponsiveViewport() const { return GAGCore::phonePresentationRequested(); }
void CustomGameScreen::cancelExecutionInput() { controls->cancelTouch(); }

void CustomGameScreen::renderPhoneLobby()
{
    auto& ui=*controls;
    const auto safe=GAGCore::mobileDialogSafe(globalContainer->gfx);
    const int x=int(safe.x)+12, w=int(safe.w)-24, top=int(safe.y)+8;
    const int bottom=int(safe.y+safe.h)-8;
    ui.box({x-4,top-4,w+8,bottom-top+8},Color(232,237,218),8);
    const auto tabs=translated({"Map","Players","Rules"});
    for(int i=0;i<3;++i)ui.button("tab/"+std::to_string(i),{x+i*w/3,top,w/3-4,48},tabs[i],
        [this,i]{activateGroup(groups[i]);controls->resetFocus();},currentTab==groups[i],true,false,"standard");
    for(auto& entry:ui.regions)entry.second.box={0,0,0,0};
    const int y=top+56, h=std::max(1,bottom-y-92);
    if(currentTab==groups[2]) renderRules(x,y,w,h);
    else {
        const int region=currentTab==groups[1]?101:100;
        ui.beginRegion(region,{x,y,w,h});
        int row=y-ui.regions[region].offset;const int start=row;
        auto label=[&](const std::string& text){row+=ui.paragraph(x+4,row,w-20,text,"standard",false,false)+8;};
        auto button=[&](const std::string& id,const std::string& text,auto callback,bool selected=false){
            ui.button(id,{x,row,w-12,48},text,callback,selected);row+=56;
        };
        if(currentTab==groups[1]) {
            ui.segments("format",{x,row,w-12,48},translated({"FFA","2 vs 2","You vs all"}),
                setup.format=="FFA"?0:setup.format=="2 vs 2"?1:2,
                [this](int v){setup.presetTeams(v);},{true,setup.activeColonies()==4,bool(setup.humanColony()) && setup.activeColonies()>1});row+=64;
            for(int i=0;i<setup.capacity;++i) {
                auto& colony=setup.colonies[i];label(colonyLabel(i));
                std::vector<bool> enabled;
                for(int j=0;j<4;++j){auto draft=setup;enabled.push_back(draft.setController(i,(CustomGameSetup::Controller)j));}
                const auto id="colony/"+std::to_string(i);
                ui.dropdown(id+"/controller",{x,row,w-12,48},translated({"You","AI","You + AI","Closed"}),colony.controller,
                    [this,i](int v){setup.setController(i,(CustomGameSetup::Controller)v);},enabled);row+=56;
                if(colony.controller==CustomGameSetup::Computer || colony.controller==CustomGameSetup::Shared) {
                    std::vector<std::string> names;for(int ai:AINames::selectionOrder())names.push_back(AINames::getAISelectorText(ai));
                    ui.dropdown(id+"/ai",{x,row,w-12,48},names,AINames::selectionIndex(colony.ai),
                        [this,i](int v){setup.colonies[i].ai=(AI::ImplementationID)AINames::selectionOrder()[v];});row+=56;
                    button(id+"/info",tr("AI strategy"),[this,i]{showAIProfile(i);});
                }
                std::vector<std::string> teams;for(int j=0;j<setup.capacity;++j)teams.push_back(tr("Team")+" "+std::to_string(j+1));
                ui.dropdown(id+"/team",{x,row,w-12,48},teams,colony.alliance,[this,i](int v){setup.colonies[i].alliance=v;setup.format="Custom teams";});row+=72;
            }
        } else {
            ui.segments("map/mode",{x,row,w-12,48},translated({"Premade maps","Random map"}),setup.random,[this](int v){setMapMode(v);});row+=60;
            if(preview->isThumbnailLoaded()) {
                const int size=std::min(w-24,180);
                preview->setScreenPosition(x+(w-size)/2-(gfx->getW()-640)/2,row-(gfx->getH()-480)/2);
                preview->setDimensions(size,size);preview->paint();row+=size+12;
            }
            if(!setup.random) {
                if(separateMapLibraries) {
                    ui.segments("map/library",{x,row,w-12,48},translated({"Built-in maps","Your maps"}),userMaps,
                        [this](int v){userMaps=v;listMaps();});row+=56;
                }
                for(size_t i=0;i<mapPaths.size();++i)button("map/entry/"+std::to_string(i),mapNames[i],[this,i]{loadMap(mapPaths[i]);},librarySelection[userMaps]==mapPaths[i]);
            } else {
                button("landscape",tr(GenerationRequest::methodName(setup.generator.method)),[this]{chooseLandscape();});
                button("map/randomize",tr("Randomize"),[this]{invalidate();previewDue=SDL_GetTicks();});
                auto control=[&](const GenerationRequest::Control& c) {
                    label(tr(c.label));const auto id="generator/"+c.id;
                    auto apply=[this,c](int value){c.set(setup.generator,value);if(c.id=="teams")setup.setCapacity(setup.generator.nbTeams);++setup.mapRevision;invalidate();};
                    if(c.isToggle())ui.checkbox(id,{x,row,w-12,48},tr(c.label),c.get(setup.generator)!=0,[apply](bool v){apply(v?1:0);});
                    else if(c.powerOfTwo || !c.allowedValues.empty()) {
                        std::vector<std::string> values;for(int v:c.values())values.push_back(c.isChoice()?tr(c.valueLabel(v)):std::to_string(c.displayValue(v)));
                        ui.dropdown(id,{x,row,w-12,48},values,c.indexOf(c.get(setup.generator)),[c,apply](int v){apply(c.valueAt(v));});
                    } else ui.stepper(id,{x,row,w-12,48},c.get(setup.generator),c.minimum,c.maximum,apply,c.step);
                    row+=64;
                };
                for(const auto& c:GenerationRequest::sharedControls())if(c.id!="workers")control(c);
                for(const auto& c:GenerationRequest::controls(setup.generator.method))control(c);
            }
        }
        ui.endRegion(row-start);
    }
    gfx->setClipRect();
    std::string error=setup.validation();if(!setup.random && !validMap)error=tr("Select a valid map.");
    ui.text(x,bottom-80,error.empty()?tr(setup.ruleset):tr(error),"standard",w,true);
    ui.button("back",{x,bottom-52,88,48},tr("Back"),[this]{endExecute(CANCEL);});
    const auto caption=setup.humanColony()?tr(setup.random?(validMap?"Play this map":"Generate & play"):"Start game"):tr("Watch game");
    ui.button("start",{x+96,bottom-52,w-96,48},caption,[this]{onAction(nullptr,BUTTON_SHORTCUT,OK,0);},true,error.empty());
}

bool CustomGameChoiceScreen::usesResponsiveViewport() const {return GAGCore::phonePresentationRequested();}
void CustomGameChoiceScreen::cancelExecutionInput() {controls->cancelTouch();}
