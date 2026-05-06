// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "echo/Construction.h"

#include <iterator>
#include <memory>
#include <vector>
#include <boost/logic/tribool.hpp>

namespace AIEcho
{
	class Echo;

	namespace Conditions
	{
		class BuildingCondition;
	}

	///This namespace stores anything related to search and iterating through buildings or teams that satisfy particular conditions.
	namespace SearchTools
	{
		class BuildingSearch;

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
			std::vector<std::shared_ptr<Conditions::BuildingCondition> > conditions;
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
			bool is_ressource(int x, int y);
			bool is_water(int x, int y);
			bool is_sand(int x, int y);
			bool is_grass(int x, int y);
			bool backs_onto_sand(int x, int y);
			int get_ammount_ressource(int x, int y);
		private:
			Echo& echo;
		};
	};
}
