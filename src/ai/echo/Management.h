// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "echo/Position.h"
#include "Map.h"
#include "Player.h"

#include <memory>
#include <string>
#include <vector>
#include <boost/logic/tribool.hpp>

namespace AIEcho
{
	class Echo;

	namespace Conditions
	{
		class Condition;
	}

	///This namespace stores anything related to managing you're buildings, flags and areas.
	namespace Management
	{
		enum ManagementOrderType
		{
			MAssignWorkers,
			MChangeSwarm,
			MDestroyBuilding,
			MAddRessourceTracker,
			MPauseRessourceTracker,
			MUnPauseRessourceTracker,
			MChangeFlagSize,
			MChangeFlagMinimumLevel,
			MAddArea,
			MRemoveArea,
			MChangeAlliances,
			MUpgradeRepair,
			MSendMessage,
			MChangeFlagPosition,
			MAdjustPriority,
		};


		///A generic management order can have conditions attached to it. This makes management orders
		///both convinient and usefull. They will wait for the conditions to be satisfied before
		///performing their change.
		class ManagementOrder
		{
		public:
			virtual ~ManagementOrder() {}
			///Adds a new condition to the management order. This assumes ownership of the condition.
			void add_condition(Conditions::Condition* condition);
		protected:
			virtual void modify(Echo& echo)=0;
			///This acts somewhat like a condition tester of its own. Like passes_conditions, this one
			///checks for the conditions for the management order to execute at all. indeterminate means
			///that its impossible to execute, false means wait some more and true means ready to execute
			///For example, the ChangeFlagSize order requires that the building be in existance, and
			///that its a flag.
			virtual boost::logic::tribool wait(Echo& echo)=0;

			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			virtual void save(GAGCore::OutputStream *stream);
			virtual ManagementOrderType get_type()=0;

			///Shared wait() implementation for orders that target a single building:
			///true once the building is constructed, false while it's pending,
			///indeterminate once it has gone away (so the order is dropped).
			static boost::logic::tribool wait_for_building(Echo& echo, int building_id);

		private:
			friend class AIEcho::Echo;
			boost::logic::tribool passes_conditions(Echo& echo);
			static ManagementOrder* load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_order(ManagementOrder* mo, GAGCore::OutputStream *stream);

			std::vector<std::shared_ptr<Conditions::Condition> > conditions;
		};

		///Assigns a particular number of workers to a building
		class AssignWorkers : public ManagementOrder
		{
		public:
			AssignWorkers() : number_of_workers(0), building_id(0) {}
			explicit AssignWorkers(int number_of_workers, int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int number_of_workers;
			int building_id;
		};

		///Changes the ratios on a swarm
		class ChangeSwarm : public ManagementOrder
		{
		public:
			ChangeSwarm() : worker_ratio(0), explorer_ratio(0), warrior_ratio(0), building_id(0) {}
			ChangeSwarm(int worker_ratio, int explorer_ratio, int warrior_ratio, int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int worker_ratio;
			int explorer_ratio;
			int warrior_ratio;
			int building_id;
		};

		///Orders the destruction of a building
		class DestroyBuilding : public ManagementOrder
		{
		public:
			DestroyBuilding() : building_id(0) {}
			DestroyBuilding(int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int building_id;
		};


		///A ressource tracker is generally used for management, like most other things. A ressource trackers job is to keep
		///track of the number of ressources in a particular building, and returning averages over a small period of time.
		///Its better to use a ressource tracker than getting the ressource amounts directly, because a ressource tracker
		///returns trends, and small anomalies like an Inn running out of food for only a second don't impact its result greatly.
		class RessourceTracker
		{
		public:
			RessourceTracker(Echo& echo, GAGCore::InputStream* stream, Player* player, Sint32 versionMinor) : echo(echo)
				{ load(stream, player, versionMinor);  }
			RessourceTracker(Echo& echo, int building_id, int length, int ressource);
			///Returns the total ressources the building possessed within the time frame
			int get_total_level();
			///Returns the number of ticks the ressource tracker has been tracking.
			int get_age();
		private:
			friend class AIEcho::Echo;
			void tick();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			std::vector<int> record;
			unsigned int position;
			int timer;
			int length;
			Echo& echo;
			int building_id;
			int ressource;
		};

		///This adds a ressource tracker to a building
		class AddRessourceTracker : public ManagementOrder
		{
		public:
			AddRessourceTracker(int length, int ressource, int building_id);
			AddRessourceTracker() : length(0), building_id(0), ressource(0) {}
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int length;
			int building_id;
			int ressource;
		};

		///This pauses a ressource tracker. This is mainly done when a building is about to be upgraded.
		class PauseRessourceTracker : public ManagementOrder
		{
		public:
			PauseRessourceTracker() : building_id(0) {}
			PauseRessourceTracker(int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int building_id;
		};

		///This unpauses a ressource tracker. This should be done when a building is done being upgraded.
		class UnPauseRessourceTracker : public ManagementOrder
		{
		public:
			UnPauseRessourceTracker() : building_id(0) {}
			UnPauseRessourceTracker(int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int building_id;
		};

		///This changes the radius of a flag.
		class ChangeFlagSize : public ManagementOrder
		{
		public:
			ChangeFlagSize() : size(0), building_id(0) {}
			explicit ChangeFlagSize(int size, int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int size;
			int building_id;
		};

		///This changes the minimum_level required to attend a flag. Used mainly for War Flags, but this
		///can be used to control whether ground attack explorers come to a particular flag. To have only
		///ground attack explorers come, use level 4. Levels 2 and 3 can only be set by the map editor.
		class ChangeFlagMinimumLevel : public ManagementOrder
		{
		public:
			ChangeFlagMinimumLevel() : minimum_level(0), building_id(0) {}
			explicit ChangeFlagMinimumLevel(int minimum_level, int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int minimum_level;
			int building_id;
		};

		///This changes a flags position
		class ChangeFlagPosition : public ManagementOrder
		{
		public:
			ChangeFlagPosition() : x(0), y(0), building_id(0) {}
			explicit ChangeFlagPosition(int x, int y, int building_id);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int x;
			int y;
			int building_id;
		};

		///This order adjusts the priority on a building
		class AdjustPriority : public ManagementOrder
		{
		public:
			enum BuildingPriority
			{
				Low,
				Medium,
				High,
			};

			AdjustPriority() : building_id(0) {}
			AdjustPriority(int building_id, BuildingPriority priority);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_id;
			BuildingPriority priority;
		};

		///This management order adds a particular type of "area" to the ground.
		///The three types of areas are in the AreaType enum, and are passed to
		///the constructor. To have this change multiple areas, its nesseccary
		///to call the add_location function multiple times.
		class AddArea : public ManagementOrder
		{
		public:
			AddArea() {}
			explicit AddArea(AreaType areatype);
			void add_location(int x, int y);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			AreaType areatype;
			std::vector<position> locations;
		};

		///This management order removes an area from the ground. Its exactly
		///the same as AddArea, with the exception that it removes areas,
		///instead of adding them.
		class RemoveArea : public ManagementOrder
		{
		public:
			RemoveArea() {}
			explicit RemoveArea(AreaType areatype);
			void add_location(int x, int y);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			AreaType areatype;
			std::vector<position> locations;
		};

		///This class allows you to adjust alliances with other teams.
		class ChangeAlliances : public ManagementOrder
		{
		public:
			ChangeAlliances() {}
			///You pass in a team number, that can be retrieved from enemy_team_iterator or a similar method. Then you pass in modifiers
			///on each of the possible alliances. If you pass in true, that alliance mode is set. If you pass in false, that alliance
			///mode is unset. If you pass in undeterminate, that alliance mode is not changed, keeping whatever value it had before.
			ChangeAlliances(int team, boost::logic::tribool is_allied, boost::logic::tribool is_enemy, boost::logic::tribool view_market, boost::logic::tribool view_inn, boost::logic::tribool view_other);
		protected:
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int team;
			boost::logic::tribool is_allied;
			boost::logic::tribool is_enemy;
			boost::logic::tribool view_market;
			boost::logic::tribool view_inn;
			boost::logic::tribool view_other;
		};

		///This order calls for a particular building to be upgraded or repaired with the provided number of workers.
		class UpgradeRepair : public ManagementOrder
		{
		public:
			UpgradeRepair(int id);
		protected:
			friend class ManagementOrder;
			UpgradeRepair() {}
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int id;
		};

		#ifdef SendMessage
		#undef SendMessage
		#endif

		///This sends a message to the AI's handle_message function.
		class SendMessage : public ManagementOrder
		{
		public:
			SendMessage(const std::string& message);
		protected:
			friend class ManagementOrder;
			SendMessage() {}
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			std::string message;
		};
	};
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::AssignWorkers::get_type()
{
	return MAssignWorkers;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::ChangeSwarm::get_type()
{
	return MChangeSwarm;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::DestroyBuilding::get_type()
{
	return MDestroyBuilding;
}


inline int AIEcho::Management::RessourceTracker::get_age()
{
	return timer;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::AddRessourceTracker::get_type()
{
	return MAddRessourceTracker;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::PauseRessourceTracker::get_type()
{
	return MPauseRessourceTracker;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::UnPauseRessourceTracker::get_type()
{
	return MUnPauseRessourceTracker;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::ChangeFlagSize::get_type()
{
	return MChangeFlagSize;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::ChangeFlagMinimumLevel::get_type()
{
	return MChangeFlagMinimumLevel;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::AddArea::get_type()
{
	return MAddArea;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::RemoveArea::get_type()
{
	return MRemoveArea;
}



inline AIEcho::Management::ManagementOrderType AIEcho::Management::ChangeAlliances::get_type()
{
	return MChangeAlliances;
}
