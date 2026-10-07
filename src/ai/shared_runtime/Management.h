// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once
#include "Ressource.h"

#include "shared_runtime/Position.h"
#include "Map.h"
#include "Player.h"

#include <memory>
#include <string>
#include <vector>
#include "Tribool.h"

namespace AISharedRuntime
{
	class Runtime;

	namespace Conditions
	{
		class Condition;
	}

	///This namespace stores anything related to managing you're buildings, flags and areas.
	namespace Management
	{
 inline constexpr int RecurringInputStock=MaterialCount;

		enum ManagementOrderType
		{
			MAssignWorkers,
			MChangeSwarm,
			MDestroyBuilding,
			MAddMaterialTracker,
			MPauseMaterialTracker,
			MUnPauseMaterialTracker,
			MChangeFlagSize,
			MChangeFlagMinimumLevel,
			MAddArea,
			MRemoveArea,
			MChangeAlliances,
			MUpgradeRepair,
			MSendMessage,
			MChangeFlagPosition,
			MAdjustPriority,
            MRetireAttraction,
            MRetireFeeding,
		};


		///A generic management order can have conditions attached to it. This makes management orders
		///both convenient and useful. They will wait for the conditions to be satisfied before
		///performing their change.
		class ManagementOrder
		{
		public:
			virtual ~ManagementOrder() {}
			///Adds a new condition to the management order. This assumes ownership of the condition.
			void add_condition(Conditions::Condition* condition);
		protected:
			virtual void modify(Runtime& runtime)=0;
			///This acts somewhat like a condition tester of its own. Like passes_conditions, this one
			///checks for the conditions for the management order to execute at all. indeterminate means
			///that its impossible to execute, false means wait some more and true means ready to execute
			///For example, the ChangeFlagSize order requires that the building be in existence, and
			///that it's a flag.
			virtual tribool wait(Runtime& runtime)=0;

			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			virtual void save(GAGCore::OutputStream *stream);
			virtual ManagementOrderType get_type()=0;

			///Shared wait() implementation for orders that target a single building:
			///true once the building is constructed, false while it's pending,
			///indeterminate once it has gone away (so the order is dropped).
			static tribool wait_for_building(Runtime& runtime, int building_id);

		private:
			friend class AISharedRuntime::Runtime;
			tribool passes_conditions(Runtime& runtime);
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
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
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
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
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
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int building_id;
		};


        // Retire one strategic use without destroying an independent service
        // supplied by the same concrete building.
        class RetireAttraction : public DestroyBuilding
        {
        public:
            RetireAttraction() = default;
            RetireAttraction(int id,unsigned retiringUnitMask):DestroyBuilding(id),retiringUnitMask(retiringUnitMask) {}
        protected:
            void modify(Runtime& runtime) override;
            bool load(GAGCore::InputStream*,Player*,Sint32) override;
            void save(GAGCore::OutputStream*) override;
            ManagementOrderType get_type() override {return MRetireAttraction;}
        private:
            unsigned retiringUnitMask=0;
        };
        class RetireFeeding : public DestroyBuilding
        {
        public:
            using DestroyBuilding::DestroyBuilding;
        protected:
            void modify(Runtime& runtime) override;
            ManagementOrderType get_type() override {return MRetireFeeding;}
        };


		///A material tracker is generally used for management, like most other things. A material trackers job is to keep
		///track of the number of materials in a particular building, and returning averages over a small period of time.
		///Its better to use a material tracker than getting the material amounts directly, because a material tracker
		///returns trends, and small anomalies like an Inn running out of food for only a second don't impact its result greatly.
		class MaterialTracker
		{
		public:
			MaterialTracker(Runtime& runtime, GAGCore::InputStream* stream, Player* player, Sint32 versionMinor) : runtime(runtime)
				{ load(stream, player, versionMinor);  }
			MaterialTracker(Runtime& runtime, int building_id, int length, int material);
			///Returns the total materials the building possessed within the time frame
			int get_total_level();
			///Returns the number of ticks the material tracker has been tracking.
			int get_age();
		private:
			friend class AISharedRuntime::Runtime;
			void tick();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			std::vector<int> record;
			unsigned int position;
			int timer;
			int length;
			Runtime& runtime;
			int building_id;
			int material;
		};

		///This adds a material tracker to a building
		class AddMaterialTracker : public ManagementOrder
		{
		public:
			AddMaterialTracker(int length, int material, int building_id);
			AddMaterialTracker() : length(0), building_id(0), material(0) {}
		protected:
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int length;
			int building_id;
			int material;
		};

		///This pauses a material tracker. This is mainly done when a building is about to be upgraded.
		class PauseMaterialTracker : public ManagementOrder
		{
		public:
			PauseMaterialTracker() : building_id(0) {}
			PauseMaterialTracker(int building_id);
		protected:
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int building_id;
		};

		///This unpauses a material tracker. This should be done when a building is done being upgraded.
		class UnPauseMaterialTracker : public ManagementOrder
		{
		public:
			UnPauseMaterialTracker() : building_id(0) {}
			UnPauseMaterialTracker(int building_id);
		protected:
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
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
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int size;
			int building_id;
		};

		///Ground attraction uses one-based minimum levels (targetRole 0).
		///Explorer attraction has an independent bombing requirement (targetRole 1, value 0/1).
		class ChangeFlagMinimumLevel : public ManagementOrder
		{
		public:
			ChangeFlagMinimumLevel() : minimum_level(0), building_id(0) {}
			explicit ChangeFlagMinimumLevel(int minimum_level, int building_id, int targetRole = 0);
		protected:
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int minimum_level;
			int building_id;
			int targetRole = 0; // -1 imports an old combined attraction control.
		};

		///This changes a flags position
		class ChangeFlagPosition : public ManagementOrder
		{
		public:
			ChangeFlagPosition() : x(0), y(0), building_id(0) {}
			explicit ChangeFlagPosition(int x, int y, int building_id);
		protected:
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
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
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_id;
			BuildingPriority priority;
		};

		///This management order adds a particular type of "area" to the ground.
		///The types of areas are in the AreaType enum (FarmArea only with the farm-areas experiment), and are passed to
		///the constructor. To have this change multiple areas, its nesseccary
		///to call the add_location function multiple times.
		class AddArea : public ManagementOrder
		{
		public:
			AddArea() {}
			explicit AddArea(AreaType areatype);
			void add_location(int x, int y);
		protected:
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
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
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
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
			///mode is unset. If you pass in indeterminate, that alliance mode is not changed, keeping whatever value it had before.
			ChangeAlliances(int team, tribool is_allied, tribool is_enemy, tribool view_market, tribool view_inn, tribool view_other);
		protected:
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int team;
			tribool is_allied;
			tribool is_enemy;
			tribool view_market;
			tribool view_inn;
			tribool view_other;
		};

		///This order calls for a particular building to be upgraded or repaired with the provided number of workers.
		class UpgradeRepair : public ManagementOrder
		{
		public:
			UpgradeRepair(int id);
		protected:
			friend class ManagementOrder;
			UpgradeRepair() {}
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
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
			void modify(Runtime& runtime);
			tribool wait(Runtime& runtime);
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			std::string message;
		};
	};
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::AssignWorkers::get_type()
{
	return MAssignWorkers;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::ChangeSwarm::get_type()
{
	return MChangeSwarm;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::DestroyBuilding::get_type()
{
	return MDestroyBuilding;
}


inline int AISharedRuntime::Management::MaterialTracker::get_age()
{
	return timer;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::AddMaterialTracker::get_type()
{
	return MAddMaterialTracker;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::PauseMaterialTracker::get_type()
{
	return MPauseMaterialTracker;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::UnPauseMaterialTracker::get_type()
{
	return MUnPauseMaterialTracker;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::ChangeFlagSize::get_type()
{
	return MChangeFlagSize;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::ChangeFlagMinimumLevel::get_type()
{
	return MChangeFlagMinimumLevel;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::AddArea::get_type()
{
	return MAddArea;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::RemoveArea::get_type()
{
	return MRemoveArea;
}



inline AISharedRuntime::Management::ManagementOrderType AISharedRuntime::Management::ChangeAlliances::get_type()
{
	return MChangeAlliances;
}
