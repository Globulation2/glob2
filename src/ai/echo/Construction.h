// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "echo/Gradients.h"
#include "echo/Position.h"
#include "Player.h"
#include "BuildingType.h"

#include <map>
#include <memory>
#include <tuple>
#include <vector>
#include <boost/logic/tribool.hpp>

namespace AIEcho
{
	class Echo;

	namespace Conditions
	{
		class Condition;
		class BuildingCondition;
		class NotUnderConstruction;
		class UnderConstruction;
		class BeingUpgraded;
		class BeingUpgradedTo;
		class SpecificBuildingType;
		class NotSpecificBuildingType;
		class BuildingLevel;
		class Upgradable;
		class EnemyBuildingDestroyed;
	}

	namespace Management
	{
		class ManagementOrder;
		class AssignWorkers;
		class ChangeSwarm;
		class DestroyBuilding;
		class RessourceTracker;
		class AddRessourceTracker;
		class PauseRessourceTracker;
		class UnPauseRessourceTracker;
		class ChangeFlagSize;
		class ChangeFlagMinimumLevel;
		class GlobalManagementOrder;
		class AddArea;
		class RemoveArea;
		class ChangeAlliances;
		class UpgradeRepair;
	}

	namespace SearchTools
	{
		class building_search_iterator;
		class BuildingSearch;
	}

	///This namespace stores all things related to the construction of new buildings.
	namespace Construction
	{
		class BuildingOrder;
		class FlagMap;
		class BuildingRegister;

		enum ConstraintType
		{
			CTMinimumDistance,
			CTMaximumDistance,
			CTMinimizedDistance,
			CTMaximizedDistance,
			CTCenterOfBuilding,
			CTSinglePosition,
		};

		///A generic constraint serves two purposes, one, to compute a score for a particular position, and two,
		///to verify that a particular position matches the requirements of the constraint. Most constraints
		///are passed a GradientInfo, as they use the distances on various gradients to do their work.
		///Keep in mind that the verifications that the position satisfies the constraint must be satisfied
		///for all points on a newly placed building, not just one (with the exception of points that aren't
		///touching the outside of the building)
		class Constraint
		{
		public:
			virtual ~Constraint(){}
		protected:
			friend class AIEcho::Construction::BuildingOrder;
			virtual int calculate_constraint(Echo& echo, int x, int y)=0;
			virtual bool passes_constraint(Echo& echo, int x, int y)=0;
			///This function is meant for the registering of GradientInfo, return NULL if the Constraint doesn't use a gradient
			virtual Gradients::GradientInfo* get_gradient_info()=0;
			virtual ConstraintType get_type()=0;
			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
			virtual void save(GAGCore::OutputStream *stream)=0;
			static Constraint* load_constraint(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_constraint(Constraint* constraint, GAGCore::OutputStream *stream);
		};

		///This constraint keeps buildings from being placed too close to a particular source
		class MinimumDistance : public Constraint
		{
		public:
			MinimumDistance(const Gradients::GradientInfo& gi, int distance);
		protected:
			MinimumDistance() :gradient_cache(NULL), distance(0) {}
			friend class Constraint;
			int calculate_constraint(Echo& echo, int x, int y);
			bool passes_constraint(Echo& echo, int x, int y);
			Gradients::GradientInfo* get_gradient_info() { return &gi; }
			ConstraintType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			Gradients::GradientInfo gi;
			Gradients::Gradient* gradient_cache;
			int distance;
		};

		///This constraint keeps buildings from being placed to far from a particular source
		class MaximumDistance: public Constraint
		{
		public:
			MaximumDistance(const Gradients::GradientInfo& gi, int distance);
		protected:
			MaximumDistance() :gradient_cache(NULL), distance(0) {}
			friend class Constraint;
			int calculate_constraint(Echo& echo, int x, int y);
			bool passes_constraint(Echo& echo, int x, int y);
			Gradients::GradientInfo* get_gradient_info() { return &gi; }
			ConstraintType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			Gradients::GradientInfo gi;
			Gradients::Gradient* gradient_cache;
			int distance;
		};

		///This constraint tries to make buildings closer to a particular source. It can be given a weight,
		///changing the effect the constraint has on the final position of the building
		class MinimizedDistance : public Constraint
		{
		public:
			MinimizedDistance(const Gradients::GradientInfo& gi, int weight);
		protected:
			MinimizedDistance() :gradient_cache(NULL), weight(0) {}
			friend class Constraint;
			int calculate_constraint(Echo& echo, int x, int y);
			bool passes_constraint(Echo& echo, int x, int y);
			Gradients::GradientInfo* get_gradient_info() { return &gi; }
			ConstraintType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			Gradients::GradientInfo gi;
			Gradients::Gradient* gradient_cache;
			int weight;
		};

		///This constraint tries to make buildings farther from a particular source. It can be given a weight,
		///changing the effect the constraint has on the final position of the building
		class MaximizedDistance : public Constraint
		{
		public:
			MaximizedDistance(const Gradients::GradientInfo& gi, int weight);
		protected:
			MaximizedDistance() :gradient_cache(NULL), weight(0) {}
			friend class Constraint;
			int calculate_constraint(Echo& echo, int x, int y);
			bool passes_constraint(Echo& echo, int x, int y);
			Gradients::GradientInfo* get_gradient_info() { return &gi; }
			ConstraintType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			Gradients::GradientInfo gi;
			Gradients::Gradient* gradient_cache;
			int weight;
		};

		///This constraint doesn't use gradients, unlike the other ones. In particular, it only allows one
		///position to be allowed, the center of the building with the provided GBID. Notice this is not
		///like other building ID's, it can only be obtained with enemy_building_iterator or a similair
		///method.
		class CenterOfBuilding : public Constraint
		{
		public:
			explicit CenterOfBuilding(int gbid);
		protected:
			CenterOfBuilding() : gbid(0) {}
			friend class Constraint;
			int calculate_constraint(Echo& echo, int x, int y);
			bool passes_constraint(Echo& echo, int x, int y);
			Gradients::GradientInfo* get_gradient_info() { return NULL; }
			ConstraintType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int gbid;
		};


		///This constraint, againt unlike the others, does not use gradients. It only allows the given
		///position to be allowed. The resulting building will *not* be centered on it except if it is
		///a 1x1 building
		class SinglePosition : public Constraint
		{
		public:
			SinglePosition(int posx, int posy);
		protected:
			SinglePosition() : posx(0), posy(0) {}
			friend class Constraint;
			int calculate_constraint(Echo& echo, int x, int y);
			bool passes_constraint(Echo& echo, int x, int y);
			Gradients::GradientInfo* get_gradient_info() { return NULL; }
			ConstraintType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int posx;
			int posy;
		};


		///An order for new buildings to be constructed. It takes the type of building from IntBuildingType.h,
		///and the number of workers that should be used to construct it.
		class BuildingOrder
		{
		public:
			BuildingOrder(int building_type, int number_of_workers);
			///Adds a constraint to be used in finding a location of the building. This class takes ownership of the constraint.
			void add_constraint(Constraint*  constraint);
			///Adds a new condition to the building order. This assumes ownership of the condition.
			void add_condition(Conditions::Condition* condition);
		private:
			friend class AIEcho::Echo;
			BuildingOrder() {}
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			///An internal function used to find the location to place the building
			position find_location(Echo& echo, Map* map, Gradients::GradientManager& manager);
			boost::logic::tribool passes_conditions(Echo& echo);
			///An internal function that has all of the constraints register their respective Gradients with the GradientManager
			void queue_gradients(Gradients::GradientManager& manager);
			int get_building_type() const { return building_type; }
			int get_number_of_workers() const { return number_of_workers; }
			int building_type;
			int number_of_workers;
			int id;
			std::vector<std::shared_ptr<Constraint> > constraints;
			std::vector<std::shared_ptr<Conditions::Condition> > conditions;
		};

		///This class is used for quick lookup of flags, which aren't stored in Map like other buildings.
		class FlagMap
		{
		public:
			explicit FlagMap(Echo& echo);
			int get_flag(int x, int y);
		private:
			friend class AIEcho::Construction::BuildingRegister;
			friend class AIEcho::Echo;
			void set_flag(int x, int y, int gid);
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			std::vector<int> flagmap;
			int width;
			Echo& echo;
		};

		///The building register is a very important sub system of Echo. It keeps track of buildings.
		///A seemingly simple process, but very, very important. Buildings you construct are looked for,
		///found, recorded, etc. Allot of seemingly odd code is found here, meant to work arround some
		///of the difficulties of other parts of glob2, so that the AI programmer can have a seemless,
		///comfortable interface. Nothing here is directly important to an AI programmer.
		///The system puts buildings through three stages. The first is where the building order has been
		///issued by the ai, but it hasn't satisfied its conditions, and thus hasn't been sent to the glob2
		///engine. The second is where the building conditions are satisfied and the building order
		///has been sent, but the engine is awaiting the pertimiter of the building to be cleared before
		///it sets the building in place. The third stage is where the building has been set in place,
		///and was detected on the map. In this stage, an engine gid has been found and a pointer to
		///the building in memory secured. The fourth stage is where the building is being upgraded.
		///This is to solve a very minor bug where a building is destroyed, then a different one
		///rebuilt in the same spot fast enough that the building register couldn't detect the change.
		///If the register knows when a building is being upgraded, it knows when the building is
		///expected to change in size and to what size, and this bug is solved.
		///Another unmentioned part is that during the second stage, the building can be timed out if
		///it was unable to be set for various reasons (ressources grew into its area)
		class BuildingRegister
		{
		public:
			BuildingRegister(Player* player, Echo& echo);
			bool is_building_pending(unsigned int id);
			bool is_building_found(unsigned int id);
			bool is_building_upgrading(unsigned int id);
			int get_type(unsigned int id);
			int get_level(unsigned int id);
			int get_assigned(unsigned int id);
			Building* get_building(unsigned int id);
			BuildingType* get_building_type(unsigned int id);
		private:
			friend class AIEcho::SearchTools::building_search_iterator;
			friend class AIEcho::SearchTools::BuildingSearch;
			friend class AIEcho::Construction::BuildingOrder;
			friend class AIEcho::Echo;

			friend class AIEcho::Conditions::NotUnderConstruction;
			friend class AIEcho::Conditions::UnderConstruction;
			friend class AIEcho::Conditions::BeingUpgraded;
			friend class AIEcho::Conditions::BeingUpgradedTo;
			friend class AIEcho::Conditions::SpecificBuildingType;
			friend class AIEcho::Conditions::NotSpecificBuildingType;
			friend class AIEcho::Conditions::BuildingLevel;
			friend class AIEcho::Conditions::Upgradable;
			friend class AIEcho::Conditions::EnemyBuildingDestroyed;

			friend class AIEcho::Management::AssignWorkers;
			friend class AIEcho::Management::ChangeSwarm;
			friend class AIEcho::Management::DestroyBuilding;
			friend class AIEcho::Management::RessourceTracker;
			friend class AIEcho::Management::AddRessourceTracker;
			friend class AIEcho::Management::PauseRessourceTracker;
			friend class AIEcho::Management::UnPauseRessourceTracker;
			friend class AIEcho::Management::ChangeFlagSize;
			friend class AIEcho::Management::ChangeFlagMinimumLevel;
			friend class AIEcho::Management::GlobalManagementOrder;
			friend class AIEcho::Management::AddArea;
			friend class AIEcho::Management::RemoveArea;
			friend class AIEcho::Management::ChangeAlliances;
			friend class AIEcho::Management::UpgradeRepair;
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);

			///This function initiates the BuildingRegister with any buildings that already exist on the map.
			void initiate();
			///This function registers a new building. When the building orders conditions are satisfied and the order
			///for the construction is sent to the game engine, call issue_order.
			unsigned int register_building();
			///After registering a building, this tells the register that an order for the construction has commenced
			void issue_order(int id, int x, int y, int building_type);
			///Removes the building from the list of pending buildings. This may been to be done in the event that the
			///conditions for the buildings constructed can never be satisfied.
			void remove_building(int id);
			void set_upgrading(unsigned int id);
			void tick();

			typedef std::map<int, std::tuple<int, int, int, int> >::iterator pending_iterator;
			typedef std::map<int, std::tuple<int, int, int, int, boost::logic::tribool> >::iterator found_iterator;

			found_iterator begin() { return found_buildings.begin(); }
			found_iterator end() { return found_buildings.end(); }
			///The last variables in both of these is simply a "this exists" variable. Its used to combat the fact
			///that pending_buildings[id] may create a new object, and the system can't tell the difference between it and something
			///real. So bassically, the last variable is set to true when the object is supposed to be there, false is
			///the default value if its accidentilly created.
			std::map<int, std::tuple<int, int, int, int> > pending_buildings;
			std::map<int, std::tuple<int, int, int, int, boost::logic::tribool> > found_buildings;
			unsigned int building_id;
			Player* player;
			Echo& echo;
		};

	};
}
