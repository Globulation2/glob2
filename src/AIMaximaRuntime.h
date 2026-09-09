/*
  Maxima private runtime.

  This deliberately does not include or depend on the shared AI framework. It provides only
  the services used by Maxima and owns all of its state per AI instance.
 */

#ifndef AI_MAXIMA_RUNTIME_H
#define AI_MAXIMA_RUNTIME_H

#include "AIImplementation.h"
#include "Map.h"
#include "Order.h"
#include "Player.h"
#include "TeamStat.h"

#include <memory>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

class Building;
struct BuildingType;

namespace AIMaximaRuntime
{

struct position
{
	position() : x(0), y(0) {}
	position(int x, int y) : x(x), y(y) {}
	int x;
	int y;
};

struct PlacementResult
{
	PlacementResult() : found(false), value() {}
	explicit PlacementResult(const position& value) : found(true), value(value) {}
	bool found;
	position value;
};

class Context;

enum OptionalBool
{
	KeepValue=-1,
	ClearValue=0,
	SetValue=1
};

enum ConstructionFilter
{
	AnyConstruction=-1,
	CompletedBuildings=0,
	ConstructionSites=1
};

struct RuntimeEvent
{
	enum Type
	{
		BuildingResolved,
		UpdateClearing,
		UpdateSwarm,
		UpdateInn,
		BuildingUpdated,
		AttackFinished,
		GuardFlagDeleted,
		ExplorerAttackFlagDeleted,
		DigOutFinished,
		StarvationInnFinished,
		ReconFlagDeleted,
		// Version 89 placement-planner events.  Append only: saved Notify
		// management orders serialize these numeric values.
		DevelopmentParcelReserved,
		DevelopmentCreateIssued,
		DevelopmentSiteObserved,
		DevelopmentCompleted,
		DevelopmentInvalidatedBeforeIssue,
		DevelopmentCreateTimedOut,
		DevelopmentDestroyedDuringConstruction,
		DevelopmentUpgradeBlocked,
		DevelopmentRequiredSourceMissing,
		DevelopmentEngineRejected
	};

	RuntimeEvent(Type type, int first=-1, int second=-1)
		: type(type), first(first), second(second) {}
	Type type;
	int first;
	int second;
};

class RuntimeAI
{
public:
	virtual ~RuntimeAI() {}
	virtual void tick(Context& context)=0;
	virtual void handle_event(Context& context, const RuntimeEvent& event)=0;
};

namespace Gradients
{
enum DistanceState
{
	UnreachableCell=0,
	ObstacleCell=1,
	SourceCell=2
};
namespace Entities
{
	enum EntityType { EBuilding, EAnyTeamBuilding, EAnyResource, EResource,
		EWater, EPosition, ESand };

	class Entity
	{
	public:
		virtual ~Entity() {}
		virtual bool matches(Player* player, int x, int y) const=0;
		virtual bool equals(const Entity& other) const=0;
		virtual bool can_change() const=0;
		virtual EntityType type() const=0;
		virtual void save(GAGCore::OutputStream*) const=0;
		static Entity* load(GAGCore::InputStream*);
	};

	class Building : public Entity
	{
	public:
		Building(int buildingType, int team, bool includeConstruction);
		bool matches(Player*, int, int) const;
		bool equals(const Entity&) const;
		bool can_change() const { return true; }
		EntityType type() const { return EBuilding; }
		void save(GAGCore::OutputStream*) const;
	private:
		int buildingType;
		int team;
		bool includeConstruction;
	};

	class AnyTeamBuilding : public Entity
	{
	public:
		AnyTeamBuilding(int team, bool includeConstruction);
		bool matches(Player*, int, int) const;
		bool equals(const Entity&) const;
		bool can_change() const { return true; }
		EntityType type() const { return EAnyTeamBuilding; }
		void save(GAGCore::OutputStream*) const;
	private:
		int team;
		bool includeConstruction;
	};

	class Resource : public Entity
	{
	public:
		explicit Resource(int resourceType);
		bool matches(Player*, int, int) const;
		bool equals(const Entity&) const;
		bool can_change() const;
		EntityType type() const { return EResource; }
		void save(GAGCore::OutputStream*) const;
	private:
		int resourceType;
	};

	class AnyResource : public Entity
	{
	public:
		bool matches(Player*, int, int) const;
		bool equals(const Entity&) const;
		bool can_change() const { return true; }
		EntityType type() const { return EAnyResource; }
		void save(GAGCore::OutputStream*) const;
	};

	class Water : public Entity
	{
	public:
		bool matches(Player*, int, int) const;
		bool equals(const Entity&) const;
		bool can_change() const { return false; }
		EntityType type() const { return EWater; }
		void save(GAGCore::OutputStream*) const;
	};

	class Position : public Entity
	{
	public:
		Position(int x, int y);
		bool matches(Player*, int, int) const;
		bool equals(const Entity&) const;
		bool can_change() const { return false; }
		EntityType type() const { return EPosition; }
		void save(GAGCore::OutputStream*) const;
	private:
		int x;
		int y;
	};

	class Sand : public Entity
	{
	public:
		bool matches(Player*, int, int) const;
		bool equals(const Entity&) const;
		bool can_change() const { return false; }
		EntityType type() const { return ESand; }
		void save(GAGCore::OutputStream*) const;
	};
}

class GradientInfo
{
public:
	void add_source(Entities::Entity* source);
	void add_obstacle(Entities::Entity* obstacle);
	bool matches_source(Player*, int, int) const;
	bool matches_obstacle(Player*, int, int) const;
	bool needs_updating() const;
	bool operator==(const GradientInfo&) const;
	void save(GAGCore::OutputStream*) const;
	bool load(GAGCore::InputStream*);
private:
	std::vector<std::shared_ptr<Entities::Entity> > sources;
	std::vector<std::shared_ptr<Entities::Entity> > obstacles;
};

class Gradient
{
public:
	explicit Gradient(const GradientInfo& info);
	int get_height(int x, int y) const;
	bool has_sources() const { return sourceCount!=0; }
private:
	friend class GradientManager;
	void recalculate(Player* player);
	GradientInfo info;
	int width;
	int sourceCount;
	std::vector<Sint16> values;
};

class GradientManager
{
public:
	explicit GradientManager(Player* player);
	Gradient& get_gradient(const GradientInfo& info);
	void queue_gradient(const GradientInfo& info);
	bool is_updated(const GradientInfo& info) const;
	void update(Uint32 worldStep);
	void invalidate();
	void saveExecutionState(GAGCore::OutputStream*) const;
	void loadExecutionState(GAGCore::InputStream*);
private:
	int find(const GradientInfo& info) const;
	Player* player;
	std::vector<std::shared_ptr<Gradient> > gradients;
	std::vector<int> ages;
	std::queue<int> queued;
	std::set<int> queuedIndexes;
	Uint32 lastWorldStep;
};
}

namespace Conditions
{
	enum Result { Impossible=-1, Waiting=0, Ready=1 };
	class Condition
	{
	public:
		virtual ~Condition() {}
		virtual Result passes(Context&) const=0;
		virtual int type() const=0;
		virtual void save(GAGCore::OutputStream*) const=0;
		static Condition* load(GAGCore::InputStream*);
	};
	class BuildingCondition
	{
	public:
		virtual ~BuildingCondition() {}
		virtual bool passes(Context&, int id) const=0;
		virtual int type() const=0;
		virtual void save(GAGCore::OutputStream*) const=0;
		static BuildingCondition* load(GAGCore::InputStream*);
	};
	class ParticularBuilding : public Condition
	{
	public:
		ParticularBuilding(BuildingCondition*, int id);
		Result passes(Context&) const;
		int type() const { return 0; }
		void save(GAGCore::OutputStream*) const;
	private:
		std::shared_ptr<BuildingCondition> condition;
		int id;
	};
	class BuildingDestroyed : public Condition
	{
	public:
		explicit BuildingDestroyed(int id) : id(id) {}
		Result passes(Context&) const;
		int type() const { return 1; }
		void save(GAGCore::OutputStream*) const;
	private: int id;
	};
	class EnemyBuildingDestroyed : public Condition
	{
	public:
		EnemyBuildingDestroyed(Context&, int gid) : gid(gid) {}
		explicit EnemyBuildingDestroyed(int gid) : gid(gid) {}
		Result passes(Context&) const;
		int type() const { return 2; }
		void save(GAGCore::OutputStream*) const;
	private: int gid;
	};
	class EitherCondition : public Condition
	{
	public:
		EitherCondition(Condition*, Condition*);
		Result passes(Context&) const;
		int type() const { return 3; }
		void save(GAGCore::OutputStream*) const;
	private:
		std::shared_ptr<Condition> first;
		std::shared_ptr<Condition> second;
	};
	class NotUnderConstruction : public BuildingCondition
	{ public: bool passes(Context&, int) const; int type() const{return 0;} void save(GAGCore::OutputStream*) const; };
	class UnderConstruction : public BuildingCondition
	{ public: bool passes(Context&, int) const; int type() const{return 1;} void save(GAGCore::OutputStream*) const; };
	class BeingUpgraded : public BuildingCondition
	{ public: bool passes(Context&, int) const; int type() const{return 2;} void save(GAGCore::OutputStream*) const; };
	class BeingUpgradedTo : public BuildingCondition
	{ public: explicit BeingUpgradedTo(int level) : level(level) {} bool passes(Context&, int) const; int type() const{return 3;} void save(GAGCore::OutputStream*) const; private: int level; };
	class SpecificBuildingType : public BuildingCondition
	{ public: explicit SpecificBuildingType(int type) : buildingType(type) {} bool passes(Context&, int) const; int type() const{return 4;} void save(GAGCore::OutputStream*) const; private: int buildingType; };
	class BuildingLevel : public BuildingCondition
	{ public: explicit BuildingLevel(int level) : level(level) {} bool passes(Context&, int) const; int type() const{return 5;} void save(GAGCore::OutputStream*) const; private: int level; };
	class Upgradable : public BuildingCondition
	{ public: bool passes(Context&, int) const; int type() const{return 6;} void save(GAGCore::OutputStream*) const; };
}

namespace Construction
{
	struct BuildingRecord
	{
		BuildingRecord();
		int x, y, type, gid, age;
		Uint64 runtimeIdentity; // Rebound on load; not part of the save format.
		bool issued, upgrading, upgradeSeen;
	};

	class BuildingRegister
	{
	public:
		explicit BuildingRegister(Player* player);
		void initiate();
		unsigned register_building();
		void issue_order(int id, int x, int y, int type);
		void remove_building(int id);
		void set_upgrading(int id);
		void tick();
		bool is_building_pending(unsigned id) const;
		bool is_building_found(unsigned id) const;
		bool is_building_upgrading(unsigned id) const;
		int get_type(unsigned id) const;
		int get_level(unsigned id) const;
		int get_assigned(unsigned id) const;
		int get_enrolled(unsigned id) const;
		int get_on_site(unsigned id) const;
		::Building* get_building(unsigned id) const;
		::BuildingType* get_building_type(unsigned id) const;
		const std::map<int, BuildingRecord>& found() const { return foundBuildings; }
		const std::map<int, BuildingRecord>& pending() const { return pendingBuildings; }
		void save(GAGCore::OutputStream*) const;
		bool load(GAGCore::InputStream*);
	private:
        friend class ::AIMaximaRuntime::Context;
		Player* player;
		std::map<int, BuildingRecord> pendingBuildings;
		std::map<int, BuildingRecord> foundBuildings;
		unsigned nextId;
	};

	class Constraint
	{
	public:
		virtual ~Constraint() {}
		virtual int score(Context&, int, int)=0;
		virtual bool passes(Context&, int, int)=0;
		///Return the only possible anchor for constraints that describe an exact
		///point.  BuildingOrder uses this to avoid scanning the entire map for a
		///SinglePosition or CenterOfBuilding order; the normal passes/score path
		///still validates every constraint at that anchor.
		virtual bool exact_position(Context&, int&, int&) { return false; }
		virtual Gradients::GradientInfo* gradient_info() { return NULL; }
		virtual int type() const=0;
		virtual void save(GAGCore::OutputStream*) const=0;
		static Constraint* load(GAGCore::InputStream*);
	};
	class MinimumDistance : public Constraint
	{
	public: MinimumDistance(const Gradients::GradientInfo&, int); int score(Context&,int,int); bool passes(Context&,int,int); Gradients::GradientInfo* gradient_info() { return &info; } int type()const{return 0;} void save(GAGCore::OutputStream*)const;
	private: Gradients::GradientInfo info; int distance; Gradients::Gradient* cached;
	};
	class MaximumDistance : public Constraint
	{
	public: MaximumDistance(const Gradients::GradientInfo&, int); int score(Context&,int,int); bool passes(Context&,int,int); Gradients::GradientInfo* gradient_info() { return &info; } int type()const{return 1;} void save(GAGCore::OutputStream*)const;
	private: Gradients::GradientInfo info; int distance; Gradients::Gradient* cached;
	};
	class MinimizedDistance : public Constraint
	{
	public: MinimizedDistance(const Gradients::GradientInfo&, int); int score(Context&,int,int); bool passes(Context&,int,int); Gradients::GradientInfo* gradient_info() { return &info; } int type()const{return 2;} void save(GAGCore::OutputStream*)const;
	private: Gradients::GradientInfo info; int weight; Gradients::Gradient* cached;
	};
	class MaximizedDistance : public Constraint
	{
	public: MaximizedDistance(const Gradients::GradientInfo&, int); int score(Context&,int,int); bool passes(Context&,int,int); Gradients::GradientInfo* gradient_info() { return &info; } int type()const{return 3;} void save(GAGCore::OutputStream*)const;
	private: Gradients::GradientInfo info; int weight; Gradients::Gradient* cached;
	};
	class CenterOfBuilding : public Constraint
	{
	public: explicit CenterOfBuilding(int gid) : gid(gid) {} int score(Context&,int,int); bool passes(Context&,int,int); bool exact_position(Context&,int&,int&); int type()const{return 4;} void save(GAGCore::OutputStream*)const;
	private: int gid;
	};
	class SinglePosition : public Constraint
	{
	public: SinglePosition(int x, int y) : x(x), y(y) {} int score(Context&,int,int); bool passes(Context&,int,int); bool exact_position(Context&,int&,int&); int type()const{return 5;} void save(GAGCore::OutputStream*)const;
	private: int x,y;
	};

	class BuildingOrder
	{
	public:
		BuildingOrder(int type, int workers);
		void add_constraint(Constraint*);
		void add_condition(Conditions::Condition*);
		void save(GAGCore::OutputStream*) const;
		static BuildingOrder* load(GAGCore::InputStream*);
	private:
		friend class ::AIMaximaRuntime::Context;
		PlacementResult find_location(Context&, int cellBudget, bool& complete);
		bool score_location(Context&, ::BuildingType*, bool flag, int x, int y,
			int& score);
		void reset_search();
		Conditions::Result conditions_pass(Context&) const;
		void queue_gradients(Gradients::GradientManager&);
		int type, workers, id;
		// Full-map searches are resumed in the original x-major order.  These
		// fields resume through the saved execution section; loaded games
		// restart their pending search.
		int searchCursor, searchWidth, searchHeight, searchBestScore;
		position searchBest;
		bool searchActive;
		std::vector<std::shared_ptr<Constraint> > constraints;
		std::vector<std::shared_ptr<Conditions::Condition> > conditions;
	};
}

namespace Management
{
	class ResourceTracker
	{
	public:
		ResourceTracker(Context&, int id, int length, int resource);
		int get_total_level() const;
		int get_age() const { return timer; }
		void tick();
		void save(GAGCore::OutputStream*) const;
		static ResourceTracker* load(Context&, GAGCore::InputStream*);
	private:
		Context& context;
		std::vector<int> record;
		unsigned position;
		int timer, buildingId, resource;
	};

	class ManagementOrder
	{
	public:
		virtual ~ManagementOrder() {}
		void add_condition(Conditions::Condition*);
		Conditions::Result ready(Context&) const;
		virtual Conditions::Result wait(Context&) const=0;
		virtual void modify(Context&)=0;
		virtual int type() const=0;
		virtual void save_payload(GAGCore::OutputStream*) const=0;
		void save(GAGCore::OutputStream*) const;
		static ManagementOrder* load(GAGCore::InputStream*);
	private:
		std::vector<std::shared_ptr<Conditions::Condition> > conditions;
	};
	class AssignWorkers : public ManagementOrder
	{ public: AssignWorkers(int workers,int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 0;} void save_payload(GAGCore::OutputStream*)const; private:int workers,id; };
	class ChangeSwarm : public ManagementOrder
	{ public: ChangeSwarm(int worker,int explorer,int warrior,int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 1;} void save_payload(GAGCore::OutputStream*)const; private:int worker,explorer,warrior,id; };
	class DestroyBuilding : public ManagementOrder
	{ public: explicit DestroyBuilding(int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 2;} void save_payload(GAGCore::OutputStream*)const; private:int id; };
	class AddResourceTracker : public ManagementOrder
	{ public: AddResourceTracker(int length,int resource,int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 3;} void save_payload(GAGCore::OutputStream*)const; private:int length,resource,id; };
	class ChangeFlagSize : public ManagementOrder
	{ public: ChangeFlagSize(int size,int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 4;} void save_payload(GAGCore::OutputStream*)const; private:int size,id; };
	class ChangeFlagMinimumLevel : public ManagementOrder
	{ public: ChangeFlagMinimumLevel(int level,int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 5;} void save_payload(GAGCore::OutputStream*)const; private:int level,id; };
	class ChangeFlagPosition : public ManagementOrder
	{ public: ChangeFlagPosition(int x,int y,int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 6;} void save_payload(GAGCore::OutputStream*)const; private:int x,y,id; };
	class AddArea : public ManagementOrder
	{ public: explicit AddArea(AreaType); void add_location(int,int); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 7;} void save_payload(GAGCore::OutputStream*)const; private:AreaType areaType; std::vector<position> locations; };
	class RemoveArea : public ManagementOrder
	{ public: explicit RemoveArea(AreaType); void add_location(int,int); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 8;} void save_payload(GAGCore::OutputStream*)const; private:AreaType areaType; std::vector<position> locations; };
	class ChangeAlliances : public ManagementOrder
	{ public: ChangeAlliances(int team,OptionalBool,OptionalBool,OptionalBool,OptionalBool,OptionalBool); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 9;} void save_payload(GAGCore::OutputStream*)const; private:int team; OptionalBool allied,enemy,market,inn,other; };
	class UpgradeRepair : public ManagementOrder
	{ public: explicit UpgradeRepair(int id); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 10;} void save_payload(GAGCore::OutputStream*)const; private:int id; };
	class Notify : public ManagementOrder
	{ public: explicit Notify(const RuntimeEvent& event); Conditions::Result wait(Context&) const; void modify(Context&); int type()const{return 11;} void save_payload(GAGCore::OutputStream*)const; private:RuntimeEvent event; };
}

namespace SearchTools
{
	class BuildingSearch;
	class building_search_iterator
	{
	public:
		building_search_iterator();
		unsigned operator*() const;
		building_search_iterator& operator++();
		bool operator!=(const building_search_iterator&) const;
	private:
		friend class BuildingSearch;
		building_search_iterator(BuildingSearch*, bool end);
		void advance();
		BuildingSearch* search;
		std::map<int, Construction::BuildingRecord>::const_iterator iterator;
		bool ended;
	};
	class BuildingSearch
	{
	public:
		explicit BuildingSearch(Context& context);
		void add_condition(Conditions::BuildingCondition*);
		int count_buildings();
		building_search_iterator begin();
		building_search_iterator end();
	private:
		friend class building_search_iterator;
		bool matches(int id) const;
		Context& context;
		std::vector<std::shared_ptr<Conditions::BuildingCondition> > conditions;
	};
	class enemy_team_iterator
	{
	public:
		explicit enemy_team_iterator(Context&);
		enemy_team_iterator();
		unsigned operator*() const;
		enemy_team_iterator& operator++();
		bool operator!=(const enemy_team_iterator&) const;
	private:
		void advance(); Context* context; int team; bool ended;
	};
	class enemy_building_iterator
	{
	public:
		enemy_building_iterator();
		enemy_building_iterator(Context&,int team,int type,int level,ConstructionFilter construction);
		unsigned operator*() const;
		enemy_building_iterator& operator++();
		bool operator!=(const enemy_building_iterator&) const;
	private:
		void advance(); Context* context; int team,type,level,index,gid; ConstructionFilter construction; bool ended;
	};
	class MapInfo
	{
	public:
		explicit MapInfo(Context&);
		int get_width() const; int get_height() const;
		bool is_forbidden_area(int,int) const; bool is_guard_area(int,int) const;
		bool is_clearing_area(int,int) const; bool is_discovered(int,int) const;
		bool is_resource(int,int,int) const; bool is_resource(int,int) const;
		bool is_water(int,int) const; bool is_sand(int,int) const; bool is_grass(int,int) const;
		bool backs_onto_sand(int,int) const; int get_ammount_resource(int,int) const;
	private: Context& context;
	};
}

class Context
{
public:
	explicit Context(Player* player);
	std::shared_ptr<Order> getOrder(RuntimeAI& ai);
	unsigned add_building_order(Construction::BuildingOrder*);
	///Cancel an unissued request, or delete the building once its issued order resolves.
	void cancel_or_destroy_building(int id);
	///Find queued, issued, and observed exploration flags anchored on a resource.
	std::vector<int> resource_flags(int resource) const;
	///Issue an exact planner-selected construction after a final engine-space
	///check. Returns the Maxima building id, or -1 without queuing an order.
	int issue_building_at(int shortType, int workers, int x, int y);
	///Resolve a live building or an exact position reserved by a queued order.
	bool get_building_position(int id, int& x, int& y);
	///Issue an explicitly classified upgrade or repair after revalidation.
	bool issue_upgrade_repair(int id, bool repair);
	void add_management_order(Management::ManagementOrder*);
	void add_resource_tracker(Management::ResourceTracker*, int id);
	std::shared_ptr<Management::ResourceTracker> get_resource_tracker(int id);
	Construction::BuildingRegister& get_building_register() { return buildings; }
	Gradients::GradientManager& get_gradient_manager() { return gradients; }
	TeamStat& get_team_stats();
	void push_order(std::shared_ptr<Order> order) { orders.push_back(order); }
	bool is_fruit_on_map() const { return fruitOnMap; }
	void dispatch_event(const RuntimeEvent& event);
	void save(GAGCore::OutputStream*) const;
	bool load(GAGCore::InputStream*, Sint32 versionMinor);
	void saveExecutionState(GAGCore::OutputStream*) const;
	void loadExecutionState(GAGCore::InputStream*, Sint32 versionMinor);

	Player* player;
	Uint32 allies, enemies, inn_view, market_view, other_view;
private:
	friend class Management::ResourceTracker;
	void initialize();
	void update_management_orders();
	void update_building_orders();
	void update_trackers();
	void detect_fruit();
	RuntimeAI* activeAI;
	Construction::BuildingRegister buildings;
	Gradients::GradientManager gradients;
	std::list<std::shared_ptr<Order> > orders;
	std::shared_ptr<Order> nullOrder;
	std::vector<std::shared_ptr<Construction::BuildingOrder> > buildingOrders;
	std::vector<std::shared_ptr<Management::ManagementOrder> > managementOrders;
	std::map<int, std::shared_ptr<Management::ResourceTracker> > trackers;
	int timer;
	int previousBuildingId;
	bool initialized;
	bool fruitOnMap;
};

}

#endif
