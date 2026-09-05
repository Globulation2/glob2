// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "echo/Position.h"
#include "echo/Gradients.h"
#include "echo/Construction.h"
#include "echo/Conditions.h"
#include "echo/Management.h"
#include "echo/SearchTools.h"

#include "AIEchoTuning.h"
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
		// Tick helpers — each guards on its own timer condition and is invoked
		// unconditionally from tick(). Implementations are split across
		// ReachToInfinity.cpp, ReachToInfinityBuilding.cpp, and
		// ReachToInfinityFlags.cpp; the call order in tick() matches the
		// original sequence of if-blocks.
		void tick_initial_setup(Echo& echo);
		void tick_explorer_flags_fruit(Echo& echo);
		void tick_explorer_flags_enemies(Echo& echo);
		void tick_inns_near_wheat(Echo& echo);
		void tick_swarms_near_wheat(Echo& echo);
		void tick_racetrack_near_stone_wood(Echo& echo);
		void tick_swimmingpool_near_wheat_wood(Echo& echo);
		void tick_school_inland(Echo& echo);
		void tick_upgrade_l1_to_l2(Echo& echo);
		void tick_upgrade_l2_to_l3(Echo& echo);
		void tick_delete_old_inns_swarms(Echo& echo);
		void tick_farming_areas(Echo& echo);

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

	// --- AI Echo per-slice magic-number renames (Phase 3b) ---
	// Sentinels — distinct meanings; do not collapse into a single constant.

	/// BuildingRegister: pending-building tuple's "ticks since registered" slot
	/// is set to this value to signal "engine order not yet sent — still waiting
	/// on conditions" (Construction.cpp:667, 798).
	static constexpr int AI_ECHO_PENDING_NOT_ISSUED = -1;

	/// SearchTools iterator initial state — "iteration has not yet started; the
	/// first set_to_next() call will seed the cursor". Distinct from the
	/// wildcard sentinels below (SearchTools.cpp building_search_iterator,
	/// enemy_team_iterator, enemy_building_iterator).
	static constexpr int AI_ECHO_ITER_NOT_STARTED = -1;

	/// enemy_building_iterator: building_type == this means "match any building
	/// type" (SearchTools.h:113-116, SearchTools.cpp:313).
	static constexpr int AI_ECHO_WILDCARD_TYPE = -1;

	/// enemy_building_iterator: level == this means "match any level"
	/// (SearchTools.h:113-116, SearchTools.cpp:314).
	static constexpr int AI_ECHO_WILDCARD_LEVEL = -1;

	// On-disk encoding of boost::logic::tribool inside AI Echo save streams.
	// Used by BuildingRegister and ChangeAlliances. NOT a wire-format enum —
	// these bytes only appear in saved-game/AI snapshots.
	static constexpr int AI_ECHO_TRIBOOL_FALSE = 0;
	static constexpr int AI_ECHO_TRIBOOL_TRUE = 1;
	static constexpr int AI_ECHO_TRIBOOL_INDETERMINATE = 2;

	/// Convert the AI Echo "user-facing" 1-based building level (the level
	/// number a script writer types) to the engine's 0-based BuildingType::level
	/// (Conditions.cpp:481, 524, Management.cpp:624, SearchTools.cpp:314).
	static constexpr int AI_ECHO_LEVEL_OFFSET_USER_TO_ENGINE = 1;

	/// In BeingUpgradedTo::passes, a finished (non-site) building's current
	/// engine level is target-2: the user level is 1-based AND the building
	/// hasn't yet stepped up. Distinct from the offset above (Conditions.cpp:486).
	static constexpr int AI_ECHO_LEVEL_OFFSET_FINISHED_TO_TARGET = 2;

	// AI Echo's internal Gradient encoding (in echo/Gradient.cpp). This is
	// SEPARATE from the engine's Map gradient sentinels (GRADIENT_FORBIDDEN
	// etc. in MapInternal.h) — Echo BFS uses a tiny 3-value encoding offset by
	// 2 so that Gradient::get_height() returns 0 at sources, -1 on obstacles,
	// and -2 on cells the BFS never reached.

	/// Returned by Gradient::get_height() for tiles that BFS never reached
	/// (Construction.cpp:96, 149, 203, 253; corresponds to internal value 0).
	static constexpr int AI_ECHO_GRADIENT_HEIGHT_UNREACHED = -2;

	/// Internal seed value written for source tiles before BFS expansion; the
	/// +2 offset is reversed by Gradient::get_height() (Gradient.cpp:227, 240).
	static constexpr int AI_ECHO_GRADIENT_SOURCE_SEED = 2;

	/// Internal value written for obstacle tiles; never expanded by BFS (which
	/// only fills cells == 0). get_height() returns -1 on these (Gradient.cpp:231).
	static constexpr int AI_ECHO_GRADIENT_OBSTACLE_MARKER = 1;

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
