// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "echo/Position.h"
#include "Map.h"

#include <memory>
#include <queue>
#include <vector>
#include <boost/logic/tribool.hpp>

class GradientBFSTest;
class Player;

namespace AIEcho
{
	class Echo;

	namespace Construction
	{
		class MinimumDistance;
		class MaximumDistance;
		class MinimizedDistance;
		class MaximizedDistance;
	}

	///The gradients namespace stores anything related to Echo's gradient system.
	namespace Gradients
	{
		class GradientInfo;
		class Gradient;
		class GradientManager;

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
				virtual bool operator==(const Entity& rhs) const=0;

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
				bool operator==(const Entity& rhs) const;
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
				bool operator==(const Entity& rhs) const;
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
				bool operator==(const Entity& rhs) const;
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
				bool operator==(const Entity& rhs) const;
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
				bool operator==(const Entity& rhs) const;
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
				bool operator==(const Entity& rhs) const;
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
				bool operator==(const Entity& rhs) const;
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
				bool operator==(const Entity& rhs) const;
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
			std::vector<std::shared_ptr<Entities::Entity> > sources;
			std::vector<std::shared_ptr<Entities::Entity> > obstacles;
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
			///Returns true if the tile is reachable and within max_dist of the
			///nearest source. Excludes obstacle (-1) and BFS-unreached (-2) tiles,
			///which a naive `get_height < max_dist` would silently include.
			bool within_dist(int posx, int posy, int max_dist) const;
		private:
			friend class AIEcho::Gradients::GradientManager;
			friend class ::GradientBFSTest;

			///Causes the gradient to be updated
			void recalculate(Map* map);
			///Toroidal 8-connected BFS expansion from sources already seeded in `gradient`.
			///Push order is fixed for deterministic networking; do not change without
			///verifying lockstep behavior. Drains `positions`.
			void expand_bfs(std::queue<position>& positions);
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
			std::vector<std::shared_ptr<Gradient> > gradients;
			std::queue<int> queuedGradients;
			std::vector<int> ticks_since_update;
			Map* map;
			unsigned int cur_update;
			int timer;
		};
	};
}
