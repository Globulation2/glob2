// SPDX-License-Identifier: GPL-3.0-or-later
#include "JavaScriptMap.h"
#include "ScriptObservations.h"
#include "GameGUI.h"
#include "Game.h"
#include "Stream.h"
#include "GlobalContainer.h"
#include <locale>
#include <sstream>
#include <stdexcept>
namespace Script
{
namespace
{
bool boolean(const Value& v,const char* key){const auto& b=v.get(key);if(b.kind!=Value::Boolean)throw std::runtime_error("Expected effect boolean");return b.number!=0;}
const char* buildingNames[]={"swarm","inn","hospital","racetrack","swimmingpool","barracks","school","defencetower","stonewall","market"};
const char* flagNames[]={"explorationflag","warflag","clearingflag"};
bool choice(const std::string& n,bool flag){if(flag){for(auto x:flagNames)if(n==x)return true;}else{for(auto x:buildingNames)if(n==x)return true;}return false;}
std::string rng(const MersenneTwister& engine){std::ostringstream s;s.imbue(std::locale::classic());s<<engine;return s.str();}
unsigned mix(unsigned h,const std::string& s){for(unsigned char c:s)h=(h^c)*16777619u;return h;}
}
void JavaScriptMap::reset(){state=Value::object();presentation=Value::object();initialized=false;seeded=false;random.seed();}
bool JavaScriptMap::buildingAllowed(const std::string& name,bool flag) const
{
 const auto& value=presentation.get(flag?"flags":"buildings").get(name);return value.kind!=Value::Boolean || value.number!=0;
}
void JavaScriptMap::present(GameGUI& gui) const
{
 for(auto name:buildingNames){if(buildingAllowed(name,false))gui.enableBuildingsChoice(name);else gui.disableBuildingsChoice(name);}
 for(auto name:flagNames){if(buildingAllowed(name,true))gui.enableFlagsChoice(name);else gui.disableFlagsChoice(name);}
 const auto& elements=presentation.get("elements");
 for(int i=0;i<5;++i){const auto& v=elements.get(std::to_string(i));if(v.kind==Value::Boolean && !v.number)gui.disableGUIElement(i);else gui.enableGUIElement(i);}
 gui.hideScriptText();const auto& message=presentation.get("message");if(message.kind==Value::String)gui.showScriptText(message.text);
 for(const auto& [lang,text]:presentation.get("translations").fields)if(text.kind==Value::String)gui.showScriptTextTr(text.text,lang);
}
void JavaScriptMap::step(const std::string& source,GameGUI& gui)
{
 auto checkpoint=random;const bool wasSeeded=seeded;
 if(!seeded){random.seed(gui.game.gameHeader.getRandomSeed()^0x4a534d50u);seeded=true;}
 try
 {
  Observations observations(gui.game,-1);Host host;host.tick=gui.game.stepCounter;host.team=-1;host.width=gui.game.map.getW();host.height=gui.game.map.getH();host.random=[this]{return random();};host.query=[&](const auto& name,const auto& args,const Script::QueryBudget& budget){if(name=="interface"){budget(1,presentation.encode().size()*NativeValueCost);return presentation;}return observations.query(name,args,budget);};
  auto result=runtime->invoke(source,state,!initialized,host);
  if(result.effects.kind==Value::Null)result.effects=Value::array();
  if(result.effects.kind!=Value::Array || result.effects.items.size()>256)throw std::runtime_error("Map callback requires at most 256 effects");
  Value nextPresentation=presentation;
  for(const auto& effect:result.effects.items)
  {
   if(effect.kind!=Value::Object)throw std::runtime_error("Effect requires a record");
   auto type=effect.string("type");
   if(type=="message") {nextPresentation.set("message",effect.string("text"));nextPresentation.set("translations",Value::object());}
   else if(type=="messageTranslated") {auto translations=nextPresentation.get("translations");if(translations.kind==Value::Null)translations=Value::object();translations.set(effect.string("language"),effect.string("text"));nextPresentation.set("translations",translations);}
   else if(type=="hideMessage"){nextPresentation.set("message",Value());nextPresentation.set("translations",Value::object());}
   else if(type=="buildingChoice" || type=="flagChoice")
   {
    bool flag=type=="flagChoice";auto name=effect.string("name");if(!choice(name,flag))throw std::runtime_error("Unknown scenario choice");auto rules=nextPresentation.get(flag?"flags":"buildings");if(rules.kind==Value::Null)rules=Value::object();rules.set(name,boolean(effect,"enabled"));nextPresentation.set(flag?"flags":"buildings",rules);
   }
   else if(type=="guiElement") {int id=effect.integer("id",0,4);auto elements=nextPresentation.get("elements");if(elements.kind==Value::Null)elements=Value::object();elements.set(std::to_string(id),boolean(effect,"enabled"));nextPresentation.set("elements",elements);}
   else if(type=="hint") {effect.integer("id",0,gui.game.gameHints.getNumberOfHints()-1);boolean(effect,"visible");}
   else if(type=="objective")
   {
    effect.integer("id",0,gui.game.objectives.getNumberOfObjectives()-1);auto action=effect.string("action");if(action!="complete" && action!="incomplete" && action!="failed" && action!="hidden" && action!="visible")throw std::runtime_error("Unknown objective action");
   }
   else throw std::runtime_error("Unknown scenario effect");
  }
  nextPresentation.encode();
  for(const auto& effect:result.effects.items)
  {
   auto type=effect.string("type");
   if(type=="hint"){int id=effect.integer("id",0,gui.game.gameHints.getNumberOfHints()-1);if(boolean(effect,"visible"))gui.game.gameHints.setHintVisible(id);else gui.game.gameHints.setHintHidden(id);}
   if(type=="objective")
   {
    int id=effect.integer("id",0,gui.game.objectives.getNumberOfObjectives()-1);auto action=effect.string("action");auto& o=gui.game.objectives;
    if(action=="complete")o.setObjectiveComplete(id);else if(action=="incomplete")o.setObjectiveIncomplete(id);else if(action=="failed")o.setObjectiveFailed(id);else if(action=="hidden")o.setObjectiveHidden(id);else o.setObjectiveVisible(id);
   }
  }
  state=std::move(result.state);presentation=std::move(nextPresentation);initialized=true;present(gui);
 }
 catch(...){random=checkpoint;seeded=wasSeeded;throw;}
}
void JavaScriptMap::save(GAGCore::OutputStream* s) const
{
 s->writeEnterSection("JavaScriptMap");s->writeUint32(ProfileVersion,"profile");s->writeText(state.encode(),"state");s->writeText(presentation.encode(),"presentation");s->writeUint8(initialized,"initialized");s->writeUint8(seeded,"seeded");s->writeText(rng(random),"random");s->writeLeaveSection();
}
void JavaScriptMap::load(GAGCore::InputStream* s)
{
 s->readEnterSection("JavaScriptMap");if(s->readUint32("profile")!=ProfileVersion)throw std::runtime_error("Unsupported map JavaScript profile");state=Value::decode(s->readText("state"));presentation=Value::decode(s->readText("presentation"));initialized=s->readUint8("initialized");seeded=s->readUint8("seeded");std::istringstream input(s->readText("random")+" ");input.imbue(std::locale::classic());if(!(input>>random))throw std::runtime_error("Invalid map script RNG");s->readLeaveSection();
}
unsigned JavaScriptMap::checksum(GameGUI* gui) const
{
 unsigned h=mix(2166136261u,state.encode());h=mix(h,presentation.encode());h=mix(h,rng(random));h=(h^initialized)*16777619u;h=(h^seeded)*16777619u;
 if(gui){auto& o=gui->game.objectives;for(int i=0;i<o.getNumberOfObjectives();++i){h=(h^o.isObjectiveVisible(i))*16777619u;h=(h^o.isObjectiveComplete(i))*16777619u;h=(h^o.isObjectiveFailed(i))*16777619u;}auto& hints=gui->game.gameHints;for(int i=0;i<hints.getNumberOfHints();++i)h=(h^hints.isHintVisible(i))*16777619u;}return h;
}
}
