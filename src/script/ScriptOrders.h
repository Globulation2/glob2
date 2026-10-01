// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ScriptValue.h"
#include <memory>
class Game;class Order;
namespace Script {std::shared_ptr<Order> order(Game& game,int team,const Value& descriptor);}
