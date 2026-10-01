// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIImplementation.h"
#include "script/ScriptRuntime.h"
#include "script/ScriptObservations.h"
class AIJavaScript:public AIImplementation
{
 Player* player;
 std::string source;
 Script::Value state=Script::Value::object();
 bool initialized=false,disabled=false;
 std::string error;
 Script::Observations observations;
 std::unique_ptr<Script::Runtime> runtime;
public:
 explicit AIJavaScript(Player* player);
 bool load(GAGCore::InputStream*,Player*,Sint32) override;
 void save(GAGCore::OutputStream*) override;
 std::shared_ptr<Order> getOrder() override;
 void observe();
};
