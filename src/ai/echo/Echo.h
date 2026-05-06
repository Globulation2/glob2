// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "echo/Position.h"
#include "echo/Gradients.h"
#include "echo/Construction.h"
#include "echo/Conditions.h"
#include "echo/Management.h"
#include "echo/SearchTools.h"

#include "AIImplementation.h"
#include "Order.h"
#include "Player.h"
#include "TeamStat.h"

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace AIEcho
{
	///This is a base class for all EchoAI's
	class EchoAI
	{
	public:
		virtual ~EchoAI(){}
		///Your AI must implement the load function that loads all of its data from a stream
		virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
		///Your AI must implement a save function that saves all of its data to a stream
		virtual void save(GAGCore::OutputStream *stream)=0;
		///This function is called every tick, about 25 times per second. This is where you put
		///all of you AI's logic
		virtual void tick(Echo& echo)=0;
		///Handles a message sent from the AI to itself if certain conditions are satisfied.
		virtual void handle_message(Echo& echo, const std::string& message)=0;
	};

	///Reach To Infinity is a simple economic test AI for Echo.
	class ReachToInfinity : public EchoAI
	{
	public:
		ReachToInfinity();
		bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		void save(GAGCore::OutputStream *stream);
		void tick(Echo& echo);
		void handle_message(Echo& echo, const std::string& message);
	private:
		int timer;
		bool flag_on_cherry;
		bool flag_on_orange;
		bool flag_on_prune;
		std::set<int> flags_on_enemy;
	};

	///This is the part that ties everything together. This bridges the interface between the game and the AI system.
	///This is where you send all of you're orders.
	class Echo : public AIImplementation
	{
	public:
		Echo(EchoAI* echoai, Player* player);
		bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		void save(GAGCore::OutputStream *stream);

		std::shared_ptr<Order> getOrder(void);

		unsigned int add_building_order(Construction::BuildingOrder* bo);
		void add_management_order(Management::ManagementOrder* mo);
		void add_ressource_tracker(Management::RessourceTracker* rt, int building_id);
		std::shared_ptr<Management::RessourceTracker> get_ressource_tracker(int building_id);

		TeamStat& get_team_stats();
		void flare(int x, int y);
		Construction::BuildingRegister& get_building_register();
		Construction::FlagMap& get_flag_map();
		void push_order(std::shared_ptr<Order> order);
		Gradients::GradientManager& get_gradient_manager();
		std::set<int>& get_starting_buildings();

		bool is_fruit_on_map() { return is_fruit; }

		Player* player;
	private:

		friend class AIEcho::Management::AddRessourceTracker;
		friend class AIEcho::Management::PauseRessourceTracker;
		friend class AIEcho::Management::UnPauseRessourceTracker;
		friend class AIEcho::Management::ChangeAlliances;
		friend class AIEcho::Management::SendMessage;


		Uint32 allies;
		Uint32 enemies;
		Uint32 inn_view;
		Uint32 market_view;
		Uint32 other_view;

		void update_management_orders();
		void pause_ressource_tracker(int building_id);
		void unpause_ressource_tracker(int building_id);
		void init_starting_buildings();
		void update_ressource_trackers();
		void update_building_orders();
		void check_fruit();

		std::list<std::shared_ptr<Order> > orders;
		std::shared_ptr<EchoAI> echoai;
		std::shared_ptr<Gradients::GradientManager> gm;
		Construction::BuildingRegister br;
		Construction::FlagMap fm;
		std::vector<std::shared_ptr<Construction::BuildingOrder> > building_orders;
		std::vector<std::shared_ptr<Management::ManagementOrder> > management_orders;
		std::map<int, std::tuple<std::shared_ptr<Management::RessourceTracker>, bool> > ressource_trackers;
		typedef std::map<int, std::tuple<std::shared_ptr<Management::RessourceTracker>, bool> >::iterator tracker_iterator;
		std::set<int> starting_buildings;
		int timer;
		///This to keep multiuple buildings from being constructed on the same tick.
		///Before the next building is constructed, the previous building must be
		///found on the BuildingRegister
		int previous_building_id;
		bool update_gm;
		bool is_fruit;

		int from_load_timer;
	};

	const unsigned int INVALID_BUILDING=65535;

	void signature_write(GAGCore::OutputStream *stream);
	void signature_check(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
};



inline TeamStat& AIEcho::Echo::get_team_stats()
{
	return *player->team->stats.getLatestStat();
}



inline void AIEcho::Echo::flare(int x, int y)
{
	orders.push_back(std::shared_ptr<Order>(new MapMarkOrder(player->team->teamNumber, x, y)));
}



inline AIEcho::Construction::BuildingRegister& AIEcho::Echo::get_building_register()
{
	return br;
}



inline AIEcho::Construction::FlagMap& AIEcho::Echo::get_flag_map()
{
	return fm;
}



inline void AIEcho::Echo::push_order(std::shared_ptr<Order> order)
{
	orders.push_back(order);
}



inline AIEcho::Gradients::GradientManager& AIEcho::Echo::get_gradient_manager()
{
	return *gm;
}



inline std::set<int>& AIEcho::Echo::get_starting_buildings()
{
	return starting_buildings;
}
