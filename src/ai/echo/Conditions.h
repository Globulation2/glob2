// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "echo/Position.h"
#include "Player.h"

#include <boost/logic/tribool.hpp>

namespace AIEcho
{
	class Echo;

	namespace Construction
	{
		class BuildingOrder;
	}

	namespace Management
	{
		class ManagementOrder;
	}

	namespace SearchTools
	{
		class BuildingSearch;
	}

	///These are all conditions on a particular Building. They are used in several places, such as when counting numbers of buildings, or
	///for setting a condition on an order to change the number of units assigned, making them very usefull. Its important to note that
	///none of the conditions work on enemies buildings, they only work on buildings on you're own team.
	namespace Conditions
	{
		///This is used for loading and saving purposes only
		enum ConditionType
		{
			CParticularBuilding,
			CBuildingDestroyed,
			CEnemyBuildingDestroyed,
			CEitherCondition,
			CAllConditions,
			CPopulation,
		};

		class BuildingCondition;

		///This is a generic condition. It can be attached to many parts of the code
		class Condition
		{
		public:
			virtual ~Condition() {}
		protected:
			friend class Management::ManagementOrder;
			friend class Construction::BuildingOrder;
			friend class EitherCondition;
			friend class AllConditions;
			///This function checks if the condition passes. The third state, indeterminate, means that the condition
			///is impossible to fullfill. For example, a condition on a particular building could never pass if that
			///building is destroyed.
			virtual boost::logic::tribool passes(Echo& echo)=0;
			virtual ConditionType get_type()=0;
			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
			virtual void save(GAGCore::OutputStream *stream)=0;
			static Condition* load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_condition(Condition* condition, GAGCore::OutputStream *stream);
		};

		///This converts a BuildingCondition into a standard condition simply by supplying the id of the building
		///to be checked.
		class ParticularBuilding : public Condition
		{
		public:
			friend class Condition;
			ParticularBuilding(BuildingCondition* condition, int id);
			~ParticularBuilding();
			boost::logic::tribool passes(Echo& echo);
			ConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			ParticularBuilding();
			BuildingCondition* condition;
			int id;
		};

		///This condition matches when one of your own buildings are destroyed. It also matches when the building
		///is timed out and removed.
		class BuildingDestroyed : public Condition
		{
		public:
			BuildingDestroyed(int id);
		protected:
			friend class Condition;
			BuildingDestroyed() {}
			boost::logic::tribool passes(Echo& echo);
			ConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int id;
		};

		///This condition matches when the provided gid of the enemy building, obtained from an enemy_building_iterator,
		///is destroyed. It's meant for use with war flags or exploration flags.
		class EnemyBuildingDestroyed : public Condition
		{
		public:
			EnemyBuildingDestroyed(Echo& echo, int gbid);
		protected:
			friend class Condition;
			EnemyBuildingDestroyed() {}
			boost::logic::tribool passes(Echo& echo);
			ConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int gbid;
			int type;
			int level;
			position location;
		};

		///Matches if either condition is true, does not require both of them
		class EitherCondition : public Condition
		{
		public:
			EitherCondition(Condition* condition1, Condition* condition2);
		protected:
			friend class Condition;
			~EitherCondition();
			boost::logic::tribool passes(Echo& echo);
			ConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			EitherCondition();
			Condition* condition1;
			Condition* condition2;
		};

		///Matches if the given conditions are true. Anywhere between 1 and 4 conditions can be given.
		///This is made to be used in conjuction with EitherCondition
		class AllConditions : public Condition
		{
		public:
			AllConditions(Condition* a, Condition* b=NULL, Condition* c=NULL, Condition* d=NULL);
		protected:
			friend class Condition;
			~AllConditions();
			boost::logic::tribool passes(Echo& echo);
			ConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			AllConditions();
			Condition* a;
			Condition* b;
			Condition* c;
			Condition* d;
		};

		///Matches when the population of the specified group of units is reached in the given method
		class Population : public Condition
		{
		public:
			enum PopulationMethod
			{
				Greater,
				Lesser,
			};

			Population(bool workers, bool explorers, bool warriors, int num, PopulationMethod method);
		protected:
			friend class Condition;
			~Population();
			boost::logic::tribool passes(Echo& echo);
			ConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			Population();
			bool workers;
			bool explorers;
			bool warriors;
			int num;
			PopulationMethod method;
		};

		///This is used for loading and saving purposes only
		enum BuildingConditionType
		{
			CNotUnderConstruction,
			CUnderConstruction,
			CBeingUpgraded,
			CBeingUpgradedTo,
			CSpecificBuildingType,
			CNotSpecificBuildingType,
			CBuildingLevel,
			CUpgradable,
			CRessourceTrackerAmount,
			CRessourceTrackerAge,
			CTicksPassed
		};

		///A generic building condition has one important function, one that checks whether the condition is satisfied
		class BuildingCondition
		{
		public:
			virtual ~BuildingCondition() {}
			friend class AIEcho::Management::ManagementOrder;
			friend class AIEcho::Construction::BuildingOrder;
			friend class AIEcho::SearchTools::BuildingSearch;
			friend class ParticularBuilding;
		protected:
			virtual bool passes(Echo& echo, int id)=0;
			virtual BuildingConditionType get_type()=0;
			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
			virtual void save(GAGCore::OutputStream *stream)=0;
			static BuildingCondition* load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_condition(BuildingCondition* condition, GAGCore::OutputStream *stream);
		};

		///This condition waits for a building not to be under construction.
		class NotUnderConstruction : public BuildingCondition
		{
		public:
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This condition waits for a building to be under construction
		class UnderConstruction : public BuildingCondition
		{
		public:
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This condition tells whether a building is being upgraded
		class BeingUpgraded : public BuildingCondition
		{
		public:
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///Similair to BeingUpgraded, but this also takes a level, in which the building is being upgraded
		///to a particular level. When possible, use this instead od combining BeingUpgraded and BuildingLevel
		class BeingUpgradedTo : public BuildingCondition
		{
		public:
			BeingUpgradedTo() : level(0) {}
			explicit BeingUpgradedTo(int level);
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int level;
		};

		///This condition tells whether a building is a particular type, as defined in IntBuildingType.h
		class SpecificBuildingType : public BuildingCondition
		{
		public:
			SpecificBuildingType() : building_type(0) {}
			explicit SpecificBuildingType(int building_type);
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_type;
		};

		///This condition matches any building that isn't of a particular type
		class NotSpecificBuildingType : public BuildingCondition
		{
		public:
			NotSpecificBuildingType() : building_type(0) {}
			explicit NotSpecificBuildingType(int building_type);
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_type;
		};

		///This building matches buildings of a particular level
		class BuildingLevel : public BuildingCondition
		{
		public:
			BuildingLevel() : building_level(0) {}
			explicit BuildingLevel(int building_level);
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_level;
		};

		///This condition matches a building that can be upgraded
		class Upgradable : public BuildingCondition
		{
		public:
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This class compares the total amount of ressources recorded by a ressource tracker.
		class RessourceTrackerAmount : public BuildingCondition
		{
		public:
			enum TrackerMethod
			{
				Greater,
				Lesser,
			};

			explicit RessourceTrackerAmount(int amount, TrackerMethod tracker_method);
		private:
			friend class BuildingCondition;
			RessourceTrackerAmount();
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int amount;
			int tracker_method;
		};

		///This class compares the age provided by a ressource tracker
		class RessourceTrackerAge : public BuildingCondition
		{
		public:
			enum TrackerMethod
			{
				Greater,
				Lesser,
			};

			explicit RessourceTrackerAge(int age, TrackerMethod tracker_method);
		private:
			friend class BuildingCondition;
			RessourceTrackerAge();
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			int age;
			int tracker_method;
		};

		///This is a debug condition that waits for a certain number of tries before it passes.
		///Not to be used under normal circumstances. Only for debugging!
		class TicksPassed : public BuildingCondition
		{
		public:
			TicksPassed() : num(0) {}
			explicit TicksPassed(int num) : num(num) {}
		protected:
			bool passes(Echo& echo, int id);
			BuildingConditionType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int num;
		};
	};
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::Upgradable::get_type()
{
	return CUpgradable;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::NotUnderConstruction::get_type()
{
	return CNotUnderConstruction;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::UnderConstruction::get_type()
{
	return CUnderConstruction;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::BeingUpgraded::get_type()
{
	return CBeingUpgraded;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::BeingUpgradedTo::get_type()
{
	return CBeingUpgradedTo;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::SpecificBuildingType::get_type()
{
	return CSpecificBuildingType;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::NotSpecificBuildingType::get_type()
{
	return CNotSpecificBuildingType;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::BuildingLevel::get_type()
{
	return CBuildingLevel;
}



inline AIEcho::Conditions::ConditionType AIEcho::Conditions::EnemyBuildingDestroyed::get_type()
{
	return CEnemyBuildingDestroyed;
}



inline bool AIEcho::Conditions::TicksPassed::passes(AIEcho::Echo& echo, int id)
{
	num--; if(num==0) return true; return false;
}



inline AIEcho::Conditions::BuildingConditionType AIEcho::Conditions::TicksPassed::get_type()
{
	return CTicksPassed;
}



inline bool AIEcho::Conditions::TicksPassed::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	return false;
}



inline void AIEcho::Conditions::TicksPassed::save(GAGCore::OutputStream *stream)
{

}
