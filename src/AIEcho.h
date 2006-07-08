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


	struct position
	{
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

	template <typename T> bool pointer_compare(T* t1, T* t2)
	{
		return (*t1)==(*t2);
	}


	namespace Gradients
	{
		namespace Entities
		{
			class Entity
			{
			public:
				virtual ~Entity(){}
				virtual bool is_entity(Map* map, int posx, int posy)=0;
				virtual bool operator==(const Entity& rhs)=0;
			};

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

			class AnyBuilding : public Entity
			{
			public:
				AnyBuilding(bool under_construction);
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			private:
				bool under_construction;
			};

			class Ressource : public Entity
			{
			public:
				Ressource(int ressource_type);
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			private:
				int ressource_type;
			};

			class AnyRessource : public Entity
			{
			public:
				AnyRessource();
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			};

			class Water : public Entity
			{
			public:
				Water();
				bool is_entity(Map* map, int posx, int posy);
				bool operator==(const Entity& rhs);
			};
		};

		class GradientInfo
		{
		public:
			GradientInfo();
			~GradientInfo();
			void add_source(Entities::Entity* source);
			void add_obstacle(Entities::Entity* obstacle);
			bool match_source(Map* map, int posx, int posy);
			bool match_obstacle(Map* map, int posx, int posy);
			bool operator==(const GradientInfo& rhs) const;
		private:
			std::vector<Entities::Entity*> sources;
			std::vector<Entities::Entity*> obstacles;
		};

		class Gradient
		{
		public:
			Gradient(const GradientInfo& gi);
			void recalculate(Map* map);
			int get_height(int posx, int posy) const;
			const GradientInfo& get_gradient_info() const { return gradient_info; }
		private:
			int width;
			int get_pos(int x, int y) const { return y*width + x; }
			GradientInfo gradient_info;
			std::vector<Sint16> gradient;
//			Sint16* gradient;
		};

		class GradientManager
		{
		public:
			GradientManager(Map* map);
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

	namespace Construction
	{
		class Constraint
		{
		public:
			virtual ~Constraint(){}
			virtual int calculate_constraint(Gradients::GradientManager& gm, int x, int y)=0;
			virtual bool passes_constraint(Gradients::GradientManager& gm, int x, int y)=0;
		};

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

		class BuildingOrder
		{
		public:
			BuildingOrder(Player* player, int building_type, int number_of_workers);
			void add_constraint(Constraint*  constraint);
			position find_location(Echo& echo, Map* map, Gradients::GradientManager& manager);
			int get_building_type() const { return building_type; }
			int get_number_of_workers() const { return number_of_workers; }
		private:
			int building_type;
			int number_of_workers;
			Player* player;
			std::vector<boost::shared_ptr<Constraint> > constraints;
		};

		class FlagMap
		{
		public:
			FlagMap(Echo& echo);
			int get_flag(int x, int y);
			void set_flag(int x, int y, int gid);
		private:
			std::vector<int> flagmap;
			int width;
			Echo& echo;
		};

		class BuildingRegister
		{
		public:
			BuildingRegister(Player* player, Echo& echo);
			void initiate();
			unsigned int register_building(int x, int y, int building_type);
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

	namespace Conditions
	{
		class Condition
		{
		public:
			virtual ~Condition() {}
			virtual bool passes(Echo& echo, int id)=0;
		};

		class NotUnderConstruction : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
		};

		class UnderConstruction : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
		};

		class BeingUpgraded : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
		};

		class BeingUpgradedTo : public Condition
		{
		public:
			BeingUpgradedTo(int level);
			bool passes(Echo& echo, int id);
		private:
			int level;
		};

		class SpecificBuildingType : public Condition
		{
		public:
			SpecificBuildingType(int building_type);
			bool passes(Echo& echo, int id);
		private:
			int building_type;
		};

		class NotSpecificBuildingType : public Condition
		{
		public:
			NotSpecificBuildingType(int building_type);
			bool passes(Echo& echo, int id);
		private:
			int building_type;
		};

		class BuildingLevel : public Condition
		{
		public:
			BuildingLevel(int building_level);
			bool passes(Echo& echo, int id);
		private:
			int building_level;
		};

		class Upgradable : public Condition
		{
		public:
			bool passes(Echo& echo, int id);
		};

		class TicksPassed : public Condition
		{
		public:
			TicksPassed(int num) : num(num) {}
			bool passes(Echo& echo, int id) { num--; if(num==0) return true; return false; }
		private:
			int num;
		};
	};


	namespace Management
	{
		class ManagementOrder
		{
		public:
			virtual ~ManagementOrder() {}
			void add_condition(Conditions::Condition* condition);
			bool passes_conditions(Echo& echo, unsigned int building_id);
			virtual void modify(Echo& echo, unsigned int building_id)=0;
		private:
			std::vector<boost::shared_ptr<Conditions::Condition> > conditions;
		};


		class AssignWorkers : public ManagementOrder
		{
		public:
			AssignWorkers(int number_of_workers);
			void modify(Echo& echo, unsigned int building_id);
		private:
			int number_of_workers;
		};


		class ChangeSwarm : public ManagementOrder
		{
		public:
			ChangeSwarm(int worker_ratio, int explorer_ratio, int warrior_ratio);
			void modify(Echo& echo, unsigned int building_id);
		private:
			int worker_ratio;
			int explorer_ratio;
			int warrior_ratio;
		};

		class DestroyBuilding : public ManagementOrder
		{
		public:
			void modify(Echo& echo, unsigned int building_id);
		};

		class RessourceTracker
		{
		public:
			RessourceTracker(Echo& echo, int building_id);
			void tick();
			int get_average_level();
			int get_age() { return timer; }
		private:
			std::vector<int> record;
			unsigned int position;
			int timer;
			Echo& echo;
			int building_id;
		};

		class AddRessourceTracker : public ManagementOrder
		{
		public:
			AddRessourceTracker();
			void modify(Echo& echo, unsigned int building_id);
		};

		class PauseRessourceTracker : public ManagementOrder
		{
		public:
			void modify(Echo& echo, unsigned int building_id);
		};

		class UnPauseRessourceTracker : public ManagementOrder
		{
		public:
			void modify(Echo& echo, unsigned int building_id);
		};

		class ChangeFlagSize : public ManagementOrder
		{
		public:
			ChangeFlagSize(int size);
			void modify(Echo& echo, unsigned int building_id);
		private:
			int size;
		};

	};

	namespace UpgradesRepairs
	{
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

	namespace SearchTools
	{
		class BuildingSearch;

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


		class BuildingSearch
		{
		public:
			BuildingSearch(Echo& echo);
			void add_condition(Conditions::Condition* condition);
			int count_buildings();
			building_search_iterator begin();
			building_search_iterator end();
			friend class building_search_iterator;
		protected:
			Echo& echo;
			bool passes_conditions(int b);
		private:
			std::vector<boost::shared_ptr<Conditions::Condition> > conditions;
		};


		int get_building_type(Echo& echo, unsigned int id);

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

		int is_flag(Echo& echo, int x, int y);
	};

	class Echo;

	class EchoAI
	{
	public:
		virtual ~EchoAI(){}
		virtual bool load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)=0;
		virtual void save(GAGCore::OutputStream *stream)=0;
		virtual void tick(Echo& echo)=0;
	};

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

	class Echo : public AIImplementation
	{
	public:
		Echo(EchoAI* echoai, Player* player);
		unsigned int add_building_order(Construction::BuildingOrder& bo);
		void add_management_order(Management::ManagementOrder* mo, unsigned int id);
		void update_management_orders();
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
	private:
		std::queue<Order*> orders;
		boost::shared_ptr<EchoAI> echoai;
		boost::shared_ptr<Gradients::GradientManager> gm;
		Construction::BuildingRegister br;
		Construction::FlagMap fm;
		std::vector<boost::tuple<boost::shared_ptr<Management::ManagementOrder>, int> > management_orders;
		std::map<int, boost::tuple<boost::shared_ptr<Management::RessourceTracker>, bool> > ressource_trackers;
		int timer;
		bool update_gm;
	};


	const unsigned int INVALID_BUILDING=65535;


};



#endif
