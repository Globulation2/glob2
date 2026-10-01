// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include <functional>
#include <memory>
#include <stdexcept>
namespace Script
{
class HostFailure: public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct Host
{
 unsigned tick=0;
 unsigned width=0,height=0;
 int team=-1;
 std::function<Value(const std::string&,const std::vector<Value>&,const QueryBudget&)> query;
 std::function<unsigned()> random;
};
struct Result { Value state, effects; };
class Runtime
{
public:
 virtual ~Runtime()=default;
 virtual Result invoke(const std::string& source,const Value& state,bool initialize,Host& host)=0;
 virtual void validate(const std::string& source)=0;
};
std::unique_ptr<Runtime> makeRuntime();
}
