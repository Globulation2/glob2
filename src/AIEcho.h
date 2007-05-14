/*
  Copyright (C) 2006 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef AIEcho_h
#define AIEcho_h


#include "Map.h"
#include "AIImplementation.h"
#include "BuildingType.h"
#include "Player.h"
#include "TeamStat.h"
#include "Order.h"

#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/logic/tribool.hpp>

#include <vector>
#include <queue>
#include <iterator>
#include <set>


namespace AIEcho
{
	class position;

	namespace Gradients
	{
		namespace Entities
		{
			class Entity;
			class Building;
			class AnyTeamBuilding;
			class AnyBuilding;
			class Ressource;
			class AnyRessource;
			class Water;
		};
		class GradientInfo;
		class Gradient;
		class GradientManager;

	};

	namespace Construction
	{
		class Constraint;
		class MinimumDistance;
		class MaximumDistance;
		class MinimizedDistance;
		class MaximizedDistance;
		class CenterOfBuilding;
		class BuildingOrder;
		class FlagMap;
		class BuildingRegister;
	};

	namespace Conditions
	{
		class Condition;
		class ParticularBuilding;
		class BuildingDestroyed;
		class EnemyBuildingDestroyed;
		class EitherCondition;
		class AllConditions;
		class BuildingCondition;
		class NotUnderConstruction;
		class UnderConstruction;
		class BeingUpgraded;
		class BeingUpgradedTo;
		class SpecificBuildingType;
		class NotSpecificBuildingType;
		class BuildingLevel;
		class Upgradable;
		class TicksPassed;
	};

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
	};

	namespace SearchTools
	{
		class building_search_iterator;
		class BuildingSearch;
		class enemy_team_iterator;
		class enemy_building_iterator;
	};

	class Echo;
	class EchoAI;
	class ReachToInfinity;
};


namespace AIEcho
{
	///A position on a map. Simple x and y cordinates, and a comparison operator for stoarge and maps and sets
	class position
	{
	public:
		position() : x(0), y(0) {}
		position(int x, int y) : x(x), y(y) {}
		int x;
		int y;
		bool operator<(const position& rhs) const
		{
			if(x!=rhs.x)
				return x<rhs.x;
			else
				return y<rhs.y;
		}
	};

	///The gradients namespace stores anything related to Echo's gradient system.
	namespace Gradients
	{
		///Stores classes related to objects that determine the sources and obstacles on a gradient
		namespace Entities
		{
			///This is an enum of the types of entities, used for saving and loading
			enum EntityType
			{
				EBuilding,
				EAnyTeamBuilding,
				EAnyBuilding,
				ERessource,
				EAnyRessource,
				EWater,
				EPosition,
				ESand,
			};

			///An entity is any observable object on the map. Its entirely generic, not specific to a certain team
			class Entity
			{
			public:
				virtual ~Entity(){}
				friend class AIEcho::Gradients::GradientInfo;
			protected:
				virtual bool is_entity(Map* map, int posx, int posy)=0;
				///The comparison operator is used to reference gradients by the entities and sources that was use to compute them
				virtual bool operator==(const Entity& rhs)=0;

				///This function says whether the entity can change during runtime. For example, water never changes during
				///the coarse of the game, however the layout of buildings can.
				virtual bool can_change()=0;

				virtual EntityType get_type()=0;
				virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
				virtual void save(GAGCore::OutputStream *stream)=0;
				static Entity* load_entity(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				static void save_entity(Entity* entity, GAGCore::OutputStream *stream);
			};

			///Matches any building of a particular type, team, and construction state
			class Building : public Entity
			{
			public:
				Building(int building_type, int team, bool under_construction);
			protected:
				Building() : building_type(-1), team(-1), under_construction(false) {}
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
			private:
				int building_type;
				int team;
				bool under_construction;
			};

			///Matches any building of a particular team and consruction state
			class AnyTeamBuilding : public Entity
			{
			public:
				AnyTeamBuilding(int team, bool under_construction);
			protected:
				AnyTeamBuilding() : team(-1), under_construction(false) {}
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
			private:
				int team;
				bool under_construction;
			};

			///Matches any building from any team, as long as it matches the construction state
			class AnyBuilding : public Entity
			{
			public:
				explicit AnyBuilding(bool under_construction);
			protected:
				AnyBuilding() : under_construction(false) {}
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
			private:
				bool under_construction;
			};

			///Matches a particular ressource type
			class Ressource : public Entity
			{
			public:
				explicit Ressource(int ressource_type);
			protected:
				Ressource() : ressource_type(-1) {}
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
			private:
				int ressource_type;
			};

			///Matches any ressource type
			class AnyRessource : public Entity
			{
			public:
				AnyRessource();
			protected:
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
			};

			///Matches water
			class Water : public Entity
			{
			public:
				Water();
			protected:
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
			};

			///Matches the provided position
			class Position : public Entity
			{
			public:
				Position(int x, int y);
			protected:
				Position() : x(-1), y(-1) {}
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
				int x;
				int y;
			};

			///Matches sand
			class Sand : public Entity
			{
			public:
				Sand();
			protected:
				friend class Entity;
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
				bool can_change();
				EntityType get_type();
				bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
				void save(GAGCore::OutputStream *stream);
			};
		};

		///The gradient info class is used to hold the information about sources and obstacles taht are used to compute a gradient
		class GradientInfo
		{
		public:
			GradientInfo();
			~GradientInfo();
			///Adds a provided source to the gradient. Ownership for the source is taken.
			void add_source(Entities::Entity* source);
			///Adds a provided obstacle to the gradient. Ownership for the obstacle is taken.
			void add_obstacle(Entities::Entity* obstacle);
		private:
			friend class AIEcho::Gradients::Gradient;
			friend class AIEcho::Gradients::GradientManager;
			friend class AIEcho::Construction::MinimumDistance;
			friend class AIEcho::Construction::MaximumDistance;
			friend class AIEcho::Construction::MinimizedDistance;
			friend class AIEcho::Construction::MaximizedDistance;


			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);

			///Returns true if the provided position matches any of the sources that where added
			bool match_source(Map* map, int posx, int posy);
			///Returns true if the provided position matches any of the obstacles that where added
			bool match_obstacle(Map* map, int posx, int posy);
			///Returns true if this GradientInfo has any entities that can change, causing it to need to be updated.
			///This is an optmization, as many gradients don't need to be update
			bool needs_updating() const;

			bool operator==(const GradientInfo& rhs) const;
			std::vector<boost::shared_ptr<Entities::Entity> > sources;
			std::vector<boost::shared_ptr<Entities::Entity> > obstacles;
			mutable boost::logic::tribool needs_updated;
		};

		///Heres a few convience functions for creating a Gradient Info
		///@{
		GradientInfo make_gradient_info(Entities::Entity* source);
		GradientInfo make_gradient_info_obstacle(Entities::Entity* source, Entities::Entity* obstacle);
		GradientInfo make_gradient_info(Entities::Entity* source1, Entities::Entity* source2);
		GradientInfo make_gradient_info_obstacle(Entities::Entity* source1, Entities::Entity* source2, Entities::Entity* obstacle);
		///@}



		///A generic, all purpose gradient. The gradient is referenced by its GradientInfo, which it uses continually in its computation.
		///Echo gradients are probably the slowest gradients in the game. However, they have one key difference compared to other gradinents,
		///they can be shared, and they are generic, even more so than Nicowar gradients (which where decently generic, but not entirely).
		class Gradient
		{
		public:
			explicit Gradient(const GradientInfo& gi);
			///Gets the distance of the provided position from the nearest source
			int get_height(int posx, int posy) const;
		private:
			friend class AIEcho::Gradients::GradientManager;

			///Causes the gradient to be updated
			void recalculate(Map* map);
			///Returns the gradient info for comparison
			const GradientInfo& get_gradient_info() const { return gradient_info; }
			int width;
			int get_pos(int x, int y) const { return y*width + x; }
			GradientInfo gradient_info;
			std::vector<Sint16> gradient;
//			Sint16* gradient;
		};

		///The gradient manager is a very important part of the system, just like the gradient itself is. The gradient manager takes upon the task
		///of managing and updating various gradients in the game. It returns a matching gradient when provided a GradientInfo.
		///This object is shared among all Echo AI's, which means gradients that aren't specific to a particular team (such as most Ressource
		///gradients) don't have to be recalculated for every Echo AI seperately. This saves allot of cpu time when their are multiple Echo AI's.
		class GradientManager
		{
		public:
			explicit GradientManager(Map* map);
			///A simple function, returns the Gradient that matches the GradientInfo. Its garunteed to be up to date within the last 150 ticks.
			///If a matching gradient isn't found, a new one is created. 150 ticks may sound like a large amount of leeway, however, most
			///gradients are updated sooner than that. As well, at normal game speed, 150 ticks is only 6 seconds, and you can count it yourself,
			///not much changes in the game in six seconds.
			Gradient& get_gradient(const GradientInfo& gi);
			///Queues up a gradient with GradientInfo to be updated. This gradient will be updated once and then never again.
			void queue_gradient(const GradientInfo& gi);
			///Returns true if the gradient GradientInfo has been updated recently.
			bool is_updated(const GradientInfo& gi);
		private:
			friend class AIEcho::Echo;
			void update();
			static int increment(const int x) { return x+1; }
			std::vector<boost::shared_ptr<Gradient> > gradients;
			std::queue<int> queuedGradients;
			std::vector<int> ticks_since_update;
			Map* map;
			unsigned int cur_update;
			int timer;
		};
	};

	///This namespace stores all things related to the construction of new buildings.
	namespace Construction
	{
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
			std::vector<boost::shared_ptr<Constraint> > constraints;
			std::vector<boost::shared_ptr<Conditions::Condition> > conditions;
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
		///has been sent, but the engine is awaiting the pertimiter of the building to be clearing before
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
			friend class AIEcho::Conditions::TicksPassed;

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

			typedef std::map<int, boost::tuple<int, int, int, int> >::iterator pending_iterator;
			typedef std::map<int, boost::tuple<int, int, int, int, boost::logic::tribool> >::iterator found_iterator;

			found_iterator begin() { return found_buildings.begin(); }
			found_iterator end() { return found_buildings.end(); }
			std::map<int, boost::tuple<int, int, int, int> > pending_buildings;
			std::map<int, boost::tuple<int, int, int, int, boost::logic::tribool> > found_buildings;
			unsigned int building_id;
			Player* player;
			Echo& echo;
		};

	};

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

		private:
			friend class AIEcho::Echo;
			boost::logic::tribool passes_conditions(Echo& echo);
			static ManagementOrder* load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_order(ManagementOrder* mo, GAGCore::OutputStream *stream);

			std::vector<boost::shared_ptr<Conditions::Condition> > conditions;
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

		enum AreaType
		{
			ClearingArea,
			ForbiddenArea,
			GuardArea
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
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
		protected:
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
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
		protected:
			friend class ManagementOrder;
			UpgradeRepair() {}
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int id;
		};

		///This sends a message to the AI's handle_message function.
		#ifdef SendMessage
		#undef SendMessage
 		#endif
		class SendMessage : public ManagementOrder
		{
		public:
			SendMessage(const std::string& message);
			void modify(Echo& echo);
			boost::logic::tribool wait(Echo& echo);
		protected:
			friend class ManagementOrder;
			SendMessage() {}
			ManagementOrderType get_type();
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
			std::string message;
		};
	};

	///This namespace stores anything related to search and iterating through buildings or teams that satisfy particular conditions.
	namespace SearchTools
	{
		///This is a standards complying iterator that iterates over buildings that satisfy conditions. Can only be
		///obtained from a BuildingSearch object.
		class building_search_iterator
		{
		public:
			const unsigned int operator*();
			building_search_iterator& operator++();
			building_search_iterator operator++(int);
			bool operator!=(const building_search_iterator& rhs) const;

			typedef std::forward_iterator_tag iterator_category;
			typedef unsigned int        value_type;
			typedef size_t   difference_type;
			typedef unsigned int*           pointer;
			typedef unsigned int&         reference;
		private:
			friend class AIEcho::SearchTools::BuildingSearch;
			building_search_iterator();
			explicit building_search_iterator(BuildingSearch& search);
			void set_to_next();
			int found_id;
			Construction::BuildingRegister::found_iterator position;
			bool is_end;
			BuildingSearch* search;
		};

		///This class holds all of the conditions for a search of buildings. Its much preferred to use this building search system
		///than to manually go over the buildings yourself, or record building ID's in your AI for future use. It has a begin() and
		///end() function like standard containers
		class BuildingSearch
		{
		public:
			explicit BuildingSearch(Echo& echo);
			///This adds a condition that the building has to pass in order to be examined.
			void add_condition(Conditions::BuildingCondition* condition);
			///This counts up all the buildings that satisfy the conditions
			int count_buildings();
			///Returns the begininng iterator
			building_search_iterator begin();
			///Returns the one-past-the-end iterator
			building_search_iterator end();
		private:
			friend class AIEcho::SearchTools::building_search_iterator;
			Echo& echo;
			bool passes_conditions(int b);
			std::vector<boost::shared_ptr<Conditions::BuildingCondition> > conditions;
		};

		///This class is a standard iterator that is used to iterate over teams that qualify as "enemies".
		///It returns an integer corrosponding to the teams id. 
		class enemy_team_iterator
		{
		public:
			explicit enemy_team_iterator(Echo& echo);
			enemy_team_iterator();
			const unsigned int operator*();
			enemy_team_iterator& operator++();
			enemy_team_iterator operator++(int);
			bool operator!=(const enemy_team_iterator& rhs) const;

			typedef std::forward_iterator_tag iterator_category;
			typedef unsigned int        value_type;
			typedef size_t   difference_type;
			typedef unsigned int*           pointer;
			typedef unsigned int&         reference;
		private:
			void set_to_next();
			int team_number;
			bool is_end;
			Echo* echo;
		};

		///This function returns whether there is a flag at the given position, and if so, its GID, if not, NOGBID
		int is_flag(Echo& echo, int x, int y);

		///This is an iterator that is used to iterate over enemy buildings. You only get so much information
		///about enemy buildings, which is why you can't use the standard Conditions. It returns standard GBIDs,
		///which are different from the building ID's you get in other portions of the system. If a class or function
		///requires a GBID instead of a standard building id, it has to come from here. You also don't get information
		///on buildings you can't see, with one exception, you can get information about buildings you don't see,
		///as long as those buildings existed when the game started (this simulates a human looking at the map before
		///a game)
		class enemy_building_iterator
		{
		public:
			enemy_building_iterator();
			///These are the three pieces of information you are provided with. If building_type or level are -1,
			///they are considered a wildcard, any building will match. If construction_site is indeterminate,
			///the same thing applies, its a wildcard, any building will match.
			enemy_building_iterator(Echo& echo, int team, int building_type, int level, boost::logic::tribool construction_site);

			const unsigned int operator*();
			enemy_building_iterator& operator++();
			enemy_building_iterator operator++(int);
			bool operator!=(const enemy_building_iterator& rhs) const;

			typedef std::forward_iterator_tag iterator_category;
			typedef unsigned int        value_type;
			typedef size_t   difference_type;
			typedef unsigned int*           pointer;
			typedef unsigned int&         reference;

		private:
			void set_to_next();
			int current_gid;
			int current_index;
			int team;
			int building_type;
			int level;
			boost::logic::tribool construction_site;
			bool is_end;
			Echo* echo;
		};


		///This class is used to get information about the map.
		class MapInfo
		{
		public:
			MapInfo(Echo& echo);
			int get_width();
			int get_height();
			bool is_forbidden_area(int x, int y);
			bool is_guard_area(int x, int y);
			bool is_clearing_area(int x, int y);
			bool is_discovered(int x, int y);
			bool is_ressource(int x, int y, int type);
			bool is_water(int x, int y);
		private:
			Echo& echo;
		};
	};

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

		Order *getOrder(void);

		unsigned int add_building_order(Construction::BuildingOrder* bo);
		void add_management_order(Management::ManagementOrder* mo);		
		void add_ressource_tracker(Management::RessourceTracker* rt, int building_id);
		boost::shared_ptr<Management::RessourceTracker> get_ressource_tracker(int building_id);

		TeamStat& get_team_stats();
		void flare(int x, int y);
		Construction::BuildingRegister& get_building_register();
		Construction::FlagMap& get_flag_map();
		void push_order(Order* order);
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

		std::queue<Order*> orders;
		boost::shared_ptr<EchoAI> echoai;
		boost::shared_ptr<Gradients::GradientManager> gm;
		Construction::BuildingRegister br;
		Construction::FlagMap fm;
		std::vector<boost::shared_ptr<Construction::BuildingOrder> > building_orders;
		std::vector<boost::shared_ptr<Management::ManagementOrder> > management_orders;
		std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> > ressource_trackers;
		typedef std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> >::iterator tracker_iterator;
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



inline bool AIEcho::Conditions::TicksPassed::passes(Echo& echo, int id)
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



inline TeamStat& AIEcho::Echo::get_team_stats()
{
	return *player->team->stats.getLatestStat();
}



inline void AIEcho::Echo::flare(int x, int y)
{
	orders.push(new MapMarkOrder(player->team->teamNumber, x, y));
}



inline AIEcho::Construction::BuildingRegister& AIEcho::Echo::get_building_register()
{
	return br;
}



inline AIEcho::Construction::FlagMap& AIEcho::Echo::get_flag_map()
{
	return fm;
}



inline void AIEcho::Echo::push_order(Order* order)
{
	orders.push(order);
}



inline AIEcho::Gradients::GradientManager& AIEcho::Echo::get_gradient_manager()
{
	return *gm;
}



inline std::set<int>& AIEcho::Echo::get_starting_buildings()
{
	return starting_buildings;
}




#endif
