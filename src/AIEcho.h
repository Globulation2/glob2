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


#include <Map.h>
#include <AIImplementation.h>
#include <BuildingType.h>
#include <Player.h>

#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/logic/tribool.hpp>

#include <vector>
#include <queue>
#include <iterator>

#include "Order.h"

namespace AIEcho
{
	class Echo;
	namespace SearchTools
	{
		class building_search_iterator;
		class BuildingSearch;
	};
	namespace Management
	{
		class Tracker;
	};

	///A position on a map. Simple x and y cordinates, and a comparison operator for stoarge and maps and sets
	struct position
	{
		position() : x(0), y(0) {}
		position(int x, int y) : x(x), y(y) {}
		int x;
		int y;
		bool operator<(const position& rhs)
		{
			if(x!=rhs.x)
				return x<rhs.x;
			else
				return y<rhs.y;
		}
	};

	///Utility function
	template <typename T> bool pointer_compare(T* t1, T* t2)
	{
		return (*t1)==(*t2);
	}


	void signature_write(GAGCore::OutputStream *stream);
	void signature_check(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);

	///The gradients namespace stores anything related to Echo's gradient system.
	namespace Gradients
	{
		///Stores classes related to objects that determine the sources and obstacles on a gradient
		namespace Entities
		{
			///An entity is any observable object on the map. Its entirely generic, not specific to a certain team
			class Entity
			{
			public:
				virtual ~Entity(){}
				virtual bool is_entity(Map* map, int posx, int posy)=0;
				///The comparison operator is used to reference gradients by the entities and sources that was use to compute them
				virtual bool operator==(const Entity& rhs)=0;
			};

			///Matches any building of a particular type, team, and construction state
			class Building : public Entity
			{
			public:
				Building(int building_type, int team, bool under_construction);
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);

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
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			private:
				int team;
				bool under_construction;
			};

			///Matches any building from any team, as long as it matches the construction state
			class AnyBuilding : public Entity
			{
			public:
				AnyBuilding(bool under_construction);
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			private:
				bool under_construction;
			};

			///Matches a particular ressource type
			class Ressource : public Entity
			{
			public:
				Ressource(int ressource_type);
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			private:
				int ressource_type;
			};

			///Matches any ressource type
			class AnyRessource : public Entity
			{
			public:
				AnyRessource();
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			};

			///Matches water
			class Water : public Entity
			{
			public:
				Water();
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
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
			///Returns true if the provided position matches any of the sources that where added
			bool match_source(Map* map, int posx, int posy);
			///Returns true if the provided position matches any of the obstacles that where added
			bool match_obstacle(Map* map, int posx, int posy);
			bool operator==(const GradientInfo& rhs) const;
		private:
			std::vector<Entities::Entity*> sources;
			std::vector<Entities::Entity*> obstacles;
		};

		///A generic, all purpose gradient. The gradient is referenced by its GradientInfo, which it uses continually in its computation.
		///Echo gradients are probably the slowest gradients in the game. However, they have one key difference compared to other gradinents,
		///they can be shared, and they are generic, even more so than Nicowar gradients (which where decently generic, but not entirely).
		class Gradient
		{
		public:
			Gradient(const GradientInfo& gi);
			///Causes the gradient to be updated
			void recalculate(Map* map);
			///Gets the distance of the provided position from the nearest source
			int get_height(int posx, int posy) const;
			///Returns the gradient info for comparison
			const GradientInfo& get_gradient_info() const { return gradient_info; }
		private:
			int width;
			int get_pos(int x, int y) const { return y*width + x; }
			GradientInfo gradient_info;
			std::vector<Sint16> gradient;
//			Sint16* gradient;
		};

		///The gradient manager is a very important part of the system, just like the gradient itself is. The gradient manager takes upon the task
		///of managing and automatically updating various gradients in the game. It returns a matching gradient when provided a GradientInfo.
		///This object is shared among all Echo AI's, which means gradients that aren't specific to a particular team (such as most Ressource
		///gradients) don't have to be recalculated for every Echo AI seperately. This saves allot of cpu time when their are multiple Echo AI's.
		class GradientManager
		{
		public:
			GradientManager(Map* map);
			///A simple function, returns the Gradient that matches the GradientInfo. Its garunteed to be up to date within the last 150 ticks.
			///If a matching gradient isn't found, a new one is created. 150 ticks may sound like a large amount of leeway, however, most
			///gradients are updated sooner than that. As well, at normal game speed, 150 ticks is only 6 seconds, and you can count it yourself,
			///not much changes in the game in six seconds.
			Gradient& get_gradient(const GradientInfo& gi);
			void update();
		private:
			static int increment(const int x) { return x+1; }
			std::vector<boost::shared_ptr<Gradient> > gradients;
			std::vector<int> ticks_since_update;
			Map* map;
			unsigned int cur_update;
			int timer;
		};
	};

	///This namespace stores all things related to the construction of new buildings.
	namespace Construction
	{
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
			virtual int calculate_constraint(Gradients::GradientManager& gm, int x, int y)=0;
			virtual bool passes_constraint(Gradients::GradientManager& gm, int x, int y)=0;
		};

		///This constraint keeps buildings from being placed too close to a particular source
		class MinimumDistance : public Constraint
		{
		public:
			MinimumDistance(const Gradients::GradientInfo& gi, int distance);
			int calculate_constraint(Gradients::GradientManager& gm, int x, int y);
			bool passes_constraint(Gradients::GradientManager& gm, int x, int y);
		private:
			const Gradients::GradientInfo& gi;
			Gradients::Gradient* gradient_cache;
			int distance;
		};

		///This constraint keeps buildings from being placed to far from a particular source
		class MaximumDistance: public Constraint
		{
		public:
			MaximumDistance(const Gradients::GradientInfo& gi, int distance);
			int calculate_constraint(Gradients::GradientManager& gm, int x, int y);
			bool passes_constraint(Gradients::GradientManager& gm, int x, int y);
		private:
			const Gradients::GradientInfo& gi;
			Gradients::Gradient* gradient_cache;
			int distance;
		};

		///This constraint tries to make buildings closer to a particular source. It can be given a weight,
		///changing the effect the constraint has on the final position of the building
		class MinimizedDistance : public Constraint
		{
		public:
			MinimizedDistance(const Gradients::GradientInfo& gi, int weight);
			int calculate_constraint(Gradients::GradientManager& gm, int x, int y);
			bool passes_constraint(Gradients::GradientManager& gm, int x, int y);
		private:
			const Gradients::GradientInfo& gi;
			Gradients::Gradient* gradient_cache;
			int weight;
		};

		///This constraint tries to make buildings farther to a particular source. It can be given a weight,
		///changing the effect the constraint has on the final position of the building
		class MaximizedDistance : public Constraint
		{
		public:
			MaximizedDistance(const Gradients::GradientInfo& gi, int weight);
			int calculate_constraint(Gradients::GradientManager& gm, int x, int y);
			bool passes_constraint(Gradients::GradientManager& gm, int x, int y);
		private:
			const Gradients::GradientInfo& gi;
			Gradients::Gradient* gradient_cache;
			int weight;
		};

		///An order for a new building to be constructed. It takes the type of building from IntBuildingType.h,
		///and the number of workers that should be used to construct it.
		class BuildingOrder
		{
		public:
			BuildingOrder(Player* player, int building_type, int number_of_workers);
			///Addds a constraint to be used in finding a location of the building. This class takes ownership of the constraint.
			void add_constraint(Constraint*  constraint);
			///An internal function used to find the location to place the building
			position find_location(Echo& echo, Map* map, Gradients::GradientManager& manager);
			int get_building_type() const { return building_type; }
			int get_number_of_workers() const { return number_of_workers; }
		private:
			int building_type;
			int number_of_workers;
			Player* player;
			std::vector<boost::shared_ptr<Constraint> > constraints;
		};

		///This class is used for quick lookup of flags, which aren't stored in Map like other buildings.
		class FlagMap
		{
		public:
			FlagMap(Echo& echo);
			int get_flag(int x, int y);
			void set_flag(int x, int y, int gid);
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			std::vector<int> flagmap;
			int width;
			Echo& echo;
		};

		///The building register is a very important sub system of Echo. It keeps track of buildings.
		///A seemingly simple process, but very, very important. Buildings you construct are looked for,
		///found, recorded, etc. Allot of seemingly odd code is found here, meant to work arround some
		///of the difficulties of other parts of glob2, so that the AI programmer can have a seemless,
		///comfortable interface. Nothing here is directly important to an AI programmer.
		class BuildingRegister
		{
		public:
			BuildingRegister(Player* player, Echo& echo);
			///This function initiates the BuildingRegister with any buildings that already exist on the map.
			void initiate();
			///This function registers a new building, it is done automatically by a BuildingOrder
			unsigned int register_building(int x, int y, int building_type);

			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);

			void set_upgrading(unsigned int id);
			void tick();
			bool is_building_pending(unsigned int id);
			bool is_building_found(unsigned int id);
			bool is_building_upgrading(unsigned int id);
			Building* get_building(unsigned int id);
			int get_type(unsigned int id);


			friend class SearchTools::building_search_iterator;
			friend class SearchTools::BuildingSearch;
		protected:

			typedef std::map<int, boost::tuple<int, int, int, int> >::iterator pending_iterator;
			typedef std::map<int, boost::tuple<int, int, int, int, boost::logic::tribool> >::iterator found_iterator;

			found_iterator begin() { return found_buildings.begin(); }
			found_iterator end() { return found_buildings.end(); }
		private:
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
			CNotUnderConstruction,
			CUnderConstruction,
			CBeingUpgraded,
			CBeingUpgradedTo,
			CSpecificBuildingType,
			CNotSpecificBuildingType,
			CBuildingLevel,
			CUpgradable,
			CTicksPassed
		};

		///A generic condition has one important function, one that checks whether the condition is satisfied
		class Condition
		{
		public:
			virtual ~Condition() {}
			virtual bool passes(Echo& echo, int id)=0;
			virtual ConditionType get_type()=0;
			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
			virtual void save(GAGCore::OutputStream *stream)=0;
			static Condition* load_condition(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_condition(Condition* condition, GAGCore::OutputStream *stream);
		};

		///This condition waits for a building not to be under construction. 
		class NotUnderConstruction : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CNotUnderConstruction; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This condition waits for a building to be under construction
		class UnderConstruction : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CUnderConstruction; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This condition tells whether a building is being upgraded
		class BeingUpgraded : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CBeingUpgraded; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///Similair to BeingUpgraded, but this also takes a level, in which the building is being upgraded
		///to a particular level. When possible, use this instead od combining BeingUpgraded and BuildingLevel
		class BeingUpgradedTo : public Condition
		{
		public:
			BeingUpgradedTo() : level(0) {}
			BeingUpgradedTo(int level);
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CBeingUpgradedTo; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int level;
		};

		///This condition tells whether a building is a particular type, as defined in IntBuildingType.h
		class SpecificBuildingType : public Condition
		{
		public:
			SpecificBuildingType() : building_type(0) {}
			SpecificBuildingType(int building_type);
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CSpecificBuildingType; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_type;
		};

		///This condition matches any building that isn't of a particular type
		class NotSpecificBuildingType : public Condition
		{
		public:
			NotSpecificBuildingType() : building_type(0) {}
			NotSpecificBuildingType(int building_type);
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CNotSpecificBuildingType; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_type;
		};

		///This building matches buildings of a particular level
		class BuildingLevel : public Condition
		{
		public:
			BuildingLevel() : building_level(0) {}
			BuildingLevel(int building_level);
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CBuildingLevel; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int building_level;
		};

		///This condition matches a building that can be upgraded
		class Upgradable : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
			ConditionType get_type() { return CUpgradable; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This is a debug condition that waits for a certain number of tries before it passes.
		///Not to be used under normal circumstances.
		class TicksPassed : public Condition
		{
		public:
			TicksPassed() : num(0) {}
			TicksPassed(int num) : num(num) {}
			bool passes(Echo& echo, int id) { num--; if(num==0) return true; return false; }
			ConditionType get_type() { return CTicksPassed; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor) {return false;}
			void save(GAGCore::OutputStream *stream) {}
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
			MChangeFlagMinimumLevel
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
			bool passes_conditions(Echo& echo, int building_id);
			virtual void modify(Echo& echo, int building_id)=0;

			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
			virtual void save(GAGCore::OutputStream *stream)=0;
			virtual ManagementOrderType get_type()=0;

			static ManagementOrder* load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_order(ManagementOrder* mo, GAGCore::OutputStream *stream);

		private:
			std::vector<boost::shared_ptr<Conditions::Condition> > conditions;
		};

		///Assigns a particular number of workers to a building
		class AssignWorkers : public ManagementOrder
		{
		public:
			AssignWorkers() : number_of_workers(0) {}
			AssignWorkers(int number_of_workers);
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MAssignWorkers; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int number_of_workers;
		};

		///Changes the ratios on a swarm
		class ChangeSwarm : public ManagementOrder
		{
		public:
			ChangeSwarm() : worker_ratio(0), explorer_ratio(0), warrior_ratio(0) {}
			ChangeSwarm(int worker_ratio, int explorer_ratio, int warrior_ratio);
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MChangeSwarm; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int worker_ratio;
			int explorer_ratio;
			int warrior_ratio;
		};

		///Orders the destruction of a building
		class DestroyBuilding : public ManagementOrder
		{
		public:
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MDestroyBuilding; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
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
			RessourceTracker(Echo& echo, int building_id);
			void tick();
			///Returns the total ressources the building possessed within the time frame
			int get_average_level();
			///Returns the number of ticks the ressource tracker has been tracking.
			int get_age() { return timer; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			std::vector<int> record;
			unsigned int position;
			int timer;
			Echo& echo;
			int building_id;
		};

		///This adds a ressource tracker to a building
		class AddRessourceTracker : public ManagementOrder
		{
		public:
			AddRessourceTracker();
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MAddRessourceTracker; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This pauses a ressource tracker. This is mainly done when a building is about to be upgraded.
		class PauseRessourceTracker : public ManagementOrder
		{
		public:
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MPauseRessourceTracker; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This unpauses a ressource tracker. This should be done when a building is done being upgraded.
		class UnPauseRessourceTracker : public ManagementOrder
		{
		public:
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MUnPauseRessourceTracker; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		};

		///This changes the radius of a flag.
		class ChangeFlagSize : public ManagementOrder
		{
		public:
			ChangeFlagSize() : size(0) {}
			ChangeFlagSize(int size);
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MChangeFlagSize; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int size;
		};

		///This changes the minimum_level required to attend a flag. Used mainly for War Flags, but this
		///can be used to control whether ground attack explorers come to a particular flag
		class ChangeFlagMinimumLevel : public ManagementOrder
		{
		public:
			ChangeFlagMinimumLevel() : minimum_level(0) {}
			ChangeFlagMinimumLevel(int minimum_level); 
			void modify(Echo& echo, int building_id);
			ManagementOrderType get_type() { return MChangeFlagMinimumLevel; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			int minimum_level;
		};

		enum GlobalManagementOrderType
		{
			MAddArea,
			MRemoveArea
		};


		///These management orders are of a different kind. They differ because they don't modify one particular
		///building or flag.
		class GlobalManagementOrder
		{
		public:
			virtual ~GlobalManagementOrder() {}
			virtual void modify(Echo& echo)=0;

			virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
			virtual void save(GAGCore::OutputStream *stream)=0;
			virtual GlobalManagementOrderType get_type()=0;
			static GlobalManagementOrder* load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			static void save_order(GlobalManagementOrder* mo, GAGCore::OutputStream *stream);
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
		class AddArea : public GlobalManagementOrder
		{
		public:
			AddArea() {}
			AddArea(AreaType areatype);
			void add_location(int x, int y);
			void modify(Echo& echo);
			GlobalManagementOrderType get_type() { return MAddArea; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			AreaType areatype;
			std::vector<position> locations;
		};

		///This management order removes an area from the ground. Its exactly
		///the same as AddArea, with the exception that it removes areas,
		///instead of adding them.
		class RemoveArea : public GlobalManagementOrder
		{
		public:
			RemoveArea() {}
			RemoveArea(AreaType areatype);
			void add_location(int x, int y);
			void modify(Echo& echo);
			GlobalManagementOrderType get_type() { return MRemoveArea; }
			bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
			void save(GAGCore::OutputStream *stream);
		private:
			AreaType areatype;
			std::vector<position> locations;
		};
	};

	///Stores anything related to upgrades or repairs
	namespace UpgradesRepairs
	{
		///This order calls for a particular building to be upgraded or repaired with the provided number of workers.
		class UpgradeRepairOrder
		{
		public:
			UpgradeRepairOrder(Echo& echo, int id, int number_of_workers);
			int get_id() const { return id; }
			int get_number_of_workers() const { return number_of_workers; }
		private:
			Echo& echo;
			int id;
			int number_of_workers;
		};
	};

	///This namespace stores anything related to search and iterating through buildings or teams that satisfy particular conditions.
	namespace SearchTools
	{
		class BuildingSearch;

		///This is a standards complying iterator that iterates over buildings that satisfy conditions. Can only be
		///obtained from a BuildingSearch object
		class building_search_iterator
		{
		public:
			const unsigned int operator*();
			building_search_iterator& operator++();
			building_search_iterator operator++(int);
			bool operator!=(const building_search_iterator& rhs) const;
			friend class BuildingSearch;
			typedef std::forward_iterator_tag iterator_category;
			typedef unsigned int        value_type;
			typedef size_t   difference_type;
			typedef unsigned int*           pointer;
			typedef unsigned int&         reference;
		protected:
			building_search_iterator();
			building_search_iterator(BuildingSearch& search);
		private:
			void set_to_next();
			int found_id;
			Construction::BuildingRegister::found_iterator position;
			bool is_end;
			BuildingSearch* search;
		};

		///This class holds all of the conditions for a search of buildings. Its much preferred to use this building search system
		///than to manually go over the buildings yourself, or record building ID's in your AI for future use. It has a begin() and
		///end() function like standard containers.
		class BuildingSearch
		{
		public:
			BuildingSearch(Echo& echo);
			///This adds a condition that the building has to pass in order to be examined.
			void add_condition(Conditions::Condition* condition);
			///This counts up all the buildings that satisfy the conditions
			int count_buildings();
			///Returns the begininng iterator
			building_search_iterator begin();
			///Returns the one-past-the-end iterator
			building_search_iterator end();
			friend class building_search_iterator;
		protected:
			Echo& echo;
			bool passes_conditions(int b);
		private:
			std::vector<boost::shared_ptr<Conditions::Condition> > conditions;
		};

		///When provided with a building ID, this function will return its building type in IntBuildingTypes.h
		int get_building_type(Echo& echo, unsigned int id);

		///This class is a standard iterator that is used to iterate over teams that qualify as "enemies".
		///It returns an integer corrosponding to the teams id. 
		class enemy_team_iterator
		{
		public:
			enemy_team_iterator(Echo& echo);
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
	};

	class Echo;

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
	};

	///Reach to infinity is a simple economic test AI for Echo.
	class ReachToInfinity : public EchoAI
	{
	public:
		ReachToInfinity();
		bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		void save(GAGCore::OutputStream *stream);
		void tick(Echo& echo);
	private:
		int timer;
	};

	///This is the part that ties everything together. This bridges the interface between the game and the AI system.
	///This is where you send all of you're orders.
	class Echo : public AIImplementation
	{
	public:
		Echo(EchoAI* echoai, Player* player);
		unsigned int add_building_order(Construction::BuildingOrder& bo);
		void add_management_order(Management::ManagementOrder* mo, unsigned int id);		
		void add_global_management_order(Management::GlobalManagementOrder* gmo);
		void update_management_orders();
		void update_global_management_orders();
		void add_upgrade_repair_order(UpgradesRepairs::UpgradeRepairOrder* uro);
		void add_ressource_tracker(Management::RessourceTracker* rt, int building_id);
		void pause_ressource_tracker(int building_id);
		void unpause_ressource_tracker(int building_id);
		boost::shared_ptr<Management::RessourceTracker> get_ressource_tracker(int building_id);
		void update_ressource_trackers();

		bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
		void save(GAGCore::OutputStream *stream);

		Order *getOrder(void);
		Player* player;
		void flare(int x, int y)
			{ orders.push(new MapMarkOrder(player->team->teamNumber, x, y)); }

		Construction::BuildingRegister& get_building_register()
			{ return br; }

		Construction::FlagMap& get_flag_map()
			{ return fm; }

		void push_order(Order* order)
			{ orders.push(order); }

		Gradients::GradientManager& get_gradient_manager() { return *gm; }
	private:
		std::queue<Order*> orders;
		boost::shared_ptr<EchoAI> echoai;
		boost::shared_ptr<Gradients::GradientManager> gm;
		Construction::BuildingRegister br;
		Construction::FlagMap fm;
		std::vector<boost::tuple<boost::shared_ptr<Management::ManagementOrder>, int> > management_orders;
		std::vector<boost::shared_ptr<Management::GlobalManagementOrder> > global_management_orders;
		std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> > ressource_trackers;
		typedef std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> >::iterator tracker_iterator;
		int timer;
		bool update_gm;
	};


	const unsigned int INVALID_BUILDING=65535;


};



#endif
