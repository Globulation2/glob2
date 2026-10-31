// SPDX-License-Identifier: GPL-3.0-or-later
#include "TutorialModel.h"
#include "Introduction.h"
#include "scripting/javascript/ScriptValue.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace Tutorial
{
namespace
{
std::string text(const Script::Value &v,const char *key)
{
    auto result=v.string(key);
    if (result.size()>4096) throw std::runtime_error("Tutorial text exceeds 4096 bytes");
    return result;
}
std::string id(const Script::Value &v,const char *key)
{
    auto result=text(v,key);
    if (result.empty() || result.size()>80 || result.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
        throw std::runtime_error("Invalid tutorial identifier");
    return result;
}
}
Script::Value contentJson(const std::string &text)
{
    if(text.size()>Script::SourceLimit) throw std::runtime_error("Tutorial definition exceeds source limit");
    size_t nodes=0;
    const auto json=nlohmann::json::parse(text,[&](int depth,nlohmann::json::parse_event_t,nlohmann::json &) {
        if(depth>32 || ++nodes>10000) throw std::runtime_error("Tutorial definition is too complex");
        return true;
    });
    const auto convert=[](const auto &self,const nlohmann::json &j)->Script::Value {
        if(j.is_null()) return {};
        if(j.is_boolean()) return Script::Value(j.get<bool>());
        if(j.is_number()) return Script::Value(j.get<double>());
        if(j.is_string()) return Script::Value(j.get<std::string>());
        auto result=j.is_array() ? Script::Value::array() : Script::Value::object();
        if(j.is_array()) for(const auto &item:j) result.items.push_back(self(self,item));
        else for(auto it=j.begin();it!=j.end();++it) result.set(it.key(),self(self,it.value()));
        return result;
    };
    return convert(convert,json);
}
std::shared_ptr<const Model> parse(const Script::Value &v)
{
    if (v.kind==Script::Value::Null) return {};
    if (v.kind!=Script::Value::Object || v.integer("schema",1,1)!=1)
        throw std::runtime_error("Unsupported tutorial presentation");
    auto m=std::make_shared<Model>();
    m->lessonId=id(v,"lessonId");m->stepId=id(v,"stepId");
    m->version=v.integer("version",1,1000000);
    m->count=v.integer("count",1,64);m->index=v.integer("index",0,m->count);
    m->restartMap=text(v,"restartMap");
    if (!m->restartMap.starts_with("campaigns/") || !m->restartMap.ends_with(".map.gz") ||
        m->restartMap.find("..")!=std::string::npos || m->restartMap.find('\\')!=std::string::npos)
        throw std::runtime_error("Tutorial restart requires a packaged campaign map");
    m->title=text(v,"title");m->instruction=text(v,"instruction");m->touchInstruction=text(v,"touchInstruction");
    m->help=text(v,"help");m->touchHelp=text(v,"touchHelp");
    m->completionTitle=text(v,"completionTitle");m->completionBody=text(v,"completionBody");m->hint=text(v,"hint");
    const auto &highlights=v.get("highlights");
    if (highlights.kind!=Script::Value::Array || highlights.items.size()>8) throw std::runtime_error("Invalid tutorial highlights");
    const std::vector<std::string> controls={"palette","workers","production","upgrade","zone:guard","zone:forbidden","zone:clear","brush","build:swarm","build:inn","build:hospital","build:racetrack","build:swimmingpool","build:barracks","build:school","build:defencetower","build:stonewall","build:market","flag:exploration","flag:war","flag:clearing"};
    for (const auto &h:highlights.items) {
        if (h.kind!=Script::Value::String || std::find(controls.begin(),controls.end(),h.text)==controls.end())
            throw std::runtime_error("Unknown tutorial control anchor");
        m->highlights.push_back(h.text);
    }
    const auto &d=v.get("destination");
    if (d.kind!=Script::Value::Null) {
        const auto &x=d.get("x"),&y=d.get("y");
        if (d.kind!=Script::Value::Object || x.kind!=Script::Value::Number || y.kind!=Script::Value::Number ||
            !std::isfinite(x.number) || !std::isfinite(y.number) || x.number<0 || y.number<0 || x.number>65536 || y.number>65536)
            throw std::runtime_error("Invalid tutorial destination");
        m->destination=Destination{x.number,y.number,text(d,"label")};
    }
    return m;
}
std::shared_ptr<const Model> view(const SimulationSnapshot::Session &s)
{
    if (s.tutorial) return s.tutorial;
    // Old self-contained scripts keep running unchanged. Never reinterpret their
    // saved globals using a new lesson definition or runner.
    if (!Introduction::active(s)) return {};
    auto m=std::make_shared<Model>();
    m->lessonId="introduction-prototype";m->restartMap=Introduction::Map;
    m->index=Introduction::stage(s);m->count=5;m->stepId="prototype-"+std::to_string(m->index);
    m->completionTitle="[intro complete]";m->completionBody="[intro complete body]";
    if (!m->complete()) {
        const auto i=m->index;
        m->title=Introduction::Goals[i];m->instruction=Introduction::Instructions[i];
        m->touchInstruction=Introduction::TouchInstructions[i];m->help=Introduction::Help[i];m->touchHelp=Introduction::TouchHelp[i];
        const std::vector<std::vector<std::string>> anchors={{"palette","flag:exploration"},{"palette","build:inn"},{"workers"},{"workers","production"},{"flag:war"}};
        m->highlights=anchors[i];
        if (i==0) m->destination=Destination{34.5,32.5,"[intro scout destination]"};
        if (i==4) m->destination=Destination{49.5,33.5,"[intro attack destination]"};
    }
    if (s.hints) for (int i=0;i<s.hints->getNumberOfHints();++i)
        if (s.hints->isHintVisible(i)) {m->hint=s.hints->getGameHintText(i);break;}
    return m;
}
}
