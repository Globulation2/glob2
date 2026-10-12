"""Pure coordinator allocation contracts; no CPU discovery or engine launches.

CPU IDs must be supplied by the caller's machine-wide admission policy. Memory
amounts are soft planning reservations, not RSS limits or RLIMIT_AS. Allocation
records describe requested/enforceable budgets; workers must enforce CPU masks
and the engine must report its resolved plan before a migrated run is accepted.
"""
from dataclasses import dataclass
import hashlib
import json
import re

POLICY_VERSION = 1
SCHEMA_VERSION = 1
PROFILES = ('auto', 'eco', 'balanced', 'luxurious', 'fast-simulation',
            'efficient-throughput')


def _positive(value, name):
    if type(value) is not int or value < 1:
        raise ValueError(name + ' must be a positive integer')
    return value


def _cpus(values):
    if not isinstance(values, (tuple, list)) or not values:
        raise ValueError('explicit nonempty CPU-ID sequence required')
    if any(type(cpu) is not int or cpu < 0 for cpu in values):
        raise ValueError('CPU IDs must be nonnegative integers')
    if len(set(values)) != len(values):
        raise ValueError('duplicate CPU IDs')
    return tuple(values)


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode()).hexdigest()


@dataclass(frozen=True)
class Allocation:
    cpu_ids: tuple
    participants: int
    memory_planning_bytes: int | None = None

    def __post_init__(self):
        object.__setattr__(self, 'cpu_ids', _cpus(self.cpu_ids))
        _positive(self.participants, 'participants (including simulation owner)')
        if self.participants > len(self.cpu_ids):
            raise ValueError('participants exceed allocated CPU IDs')
        if self.memory_planning_bytes is not None:
            _positive(self.memory_planning_bytes, 'memory planning bytes')

    def record(self):
        return {'cpu_ids': list(self.cpu_ids), 'participants': self.participants,
                'participants_include_simulation_owner': True,
                'memory_planning_bytes': self.memory_planning_bytes,
                'memory_enforcement': 'soft_planning_only'}


@dataclass(frozen=True)
class AllocationPlan:
    cpu_ids: tuple
    allocations: tuple
    memory_planning_budget_bytes: int | None = None
    measured: bool = False

    def __post_init__(self):
        object.__setattr__(self, 'cpu_ids', tuple(sorted(_cpus(self.cpu_ids))))
        if not isinstance(self.allocations, (tuple, list)) or not self.allocations:
            raise ValueError('nonempty explicit allocations required')
        object.__setattr__(self, 'allocations', tuple(self.allocations))
        if type(self.measured) is not bool:
            raise ValueError('measured must be bool')
        if self.memory_planning_budget_bytes is not None:
            _positive(self.memory_planning_budget_bytes, 'memory planning budget')
        used = set()
        for allocation in self.allocations:
            if type(allocation) is not Allocation:
                raise ValueError('allocations must be Allocation records')
            ids = set(allocation.cpu_ids)
            if not ids.issubset(self.cpu_ids) or used.intersection(ids):
                raise ValueError('allocation is outside CPU budget or overlaps')
            used.update(ids)
        if self.memory_planning_budget_bytes is not None:
            amounts = [a.memory_planning_bytes for a in self.allocations]
            if any(n is None for n in amounts):
                raise ValueError('every allocation needs a memory planning reservation')
            if sum(amounts) > self.memory_planning_budget_bytes:
                raise ValueError('allocations exceed memory planning budget')

    def record(self):
        return {'schema_version': SCHEMA_VERSION, 'policy_version': POLICY_VERSION,
                'requested_execution_profile': 'efficient-throughput',
                'machine_budget': {'cpu_ids': list(self.cpu_ids),
                                   'memory_planning_bytes': self.memory_planning_budget_bytes},
                'allocations': [a.record() for a in self.allocations],
                'measured': self.measured, 'allocation_frozen': True,
                'scope': 'caller_supplied_budget_only'}

    def attempt_record(self, attempt_id, slot, *, requested_execution_profile='efficient-throughput'):
        if type(requested_execution_profile) is not str or requested_execution_profile not in PROFILES:
            raise ValueError('invalid requested execution profile')
        if not isinstance(attempt_id, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,127}', attempt_id):
            raise ValueError('invalid attempt ID')
        if type(slot) is not int or not 0 <= slot < len(self.allocations):
            raise ValueError('invalid allocation slot')
        return {'schema_version': SCHEMA_VERSION, 'policy_version': POLICY_VERSION,
                'requested_execution_profile': requested_execution_profile,
                'attempt_id': attempt_id, 'slot': slot,
                'allocation': self.allocations[slot].record(),
                'allocation_plan_sha256': _digest(self.record()),
                'measured': self.measured, 'allocation_frozen': True,
                'resolved_plan_status': 'pending_engine_result'}


def plan_allocations(cpu_ids, concurrent_games, *, memory_planning_budget_bytes=None,
                     explicit_allocations=None, participants_per_game=None, measured=False):
    """Partition an explicit shared budget or validate preserved allocations.

    No cores are implicitly reserved and no SMT/model/count heuristics are used.
    The caller chooses concurrency; this helper does not claim the whole host.
    A returned plan is immutable even for unmeasured runs. Adaptive admission
    needs a new plan/attempt identity, never mutation of a measured allocation.
    """
    cpus = tuple(sorted(_cpus(cpu_ids)))
    slots = _positive(concurrent_games, 'concurrent games')
    if slots > len(cpus):
        raise ValueError('more games than allocated CPU IDs')
    if memory_planning_budget_bytes is not None:
        _positive(memory_planning_budget_bytes, 'memory planning budget')
        if memory_planning_budget_bytes < slots:
            raise ValueError('memory planning budget cannot reserve every game')
    if participants_per_game is not None:
        _positive(participants_per_game, 'participants per game (including simulation owner)')
    if explicit_allocations is not None:
        if participants_per_game is not None:
            raise ValueError('preserved explicit allocations cannot be combined with participant override')
        if not isinstance(explicit_allocations, (tuple, list)) or len(explicit_allocations) != slots:
            raise ValueError('one explicit allocation required per game')
        allocations = tuple(explicit_allocations)
    else:
        width, extra = divmod(len(cpus), slots)
        memory, memory_extra = (divmod(memory_planning_budget_bytes, slots)
                                if memory_planning_budget_bytes is not None else (None, 0))
        allocations, cursor = [], 0
        for slot in range(slots):
            count = width + (slot < extra)
            ids = cpus[cursor:cursor + count]
            allocations.append(Allocation(ids, count if participants_per_game is None else participants_per_game,
                               memory + (slot < memory_extra) if memory is not None else None))
            cursor += count
        allocations = tuple(allocations)
    return AllocationPlan(cpus, allocations, memory_planning_budget_bytes, measured)


def validate_frozen_attempt(record, plan, attempt_id, slot):
    """Require the exact requested allocation; never silently re-plan a retry."""
    if type(plan) is not AllocationPlan or type(record) is not dict:
        raise ValueError('frozen attempt allocation changed')
    # Canonical JSON preserves bool/int distinctions that dict equality loses.
    try:
        same = _digest(record) == _digest(plan.attempt_record(attempt_id, slot,
            requested_execution_profile=record.get('requested_execution_profile')))
    except (TypeError, ValueError):
        same = False
    if not same:
        raise ValueError('frozen attempt allocation changed')


def classify_cli_capability(description):
    """Classify already-parsed help JSON, without probing or guessing flags.

    Older CLI2 may still support all game features but lacks this contract. An
    explicit profile must be rejected there or use a separately selected legacy
    compatibility policy; this function never approximates/drop-flags fallback.
    """
    if not isinstance(description, dict):
        raise ValueError('CLI description must be an object')
    if (type(description.get('cli_version')) is not int or description['cli_version'] != 2
            or type(description.get('schema_version')) is not int or description['schema_version'] != 1
            or not isinstance(description.get('commands'), list)):
        return 'unsupported_cli'
    contract = description.get('execution_profiles')
    if contract is None:
        return 'legacy_cli2'
    if not isinstance(contract, dict):
        raise ValueError('malformed execution-profile capability')
    if (type(contract.get('policy_version')) is not int or contract['policy_version'] != POLICY_VERSION
            or type(contract.get('resource_plan_schema_version')) is not int
            or contract['resource_plan_schema_version'] != SCHEMA_VERSION):
        return 'unsupported_execution_contract'
    profiles = contract.get('profiles')
    if not isinstance(profiles, list) or any(type(p) is not str for p in profiles) or len(profiles) != len(set(profiles)):
        raise ValueError('malformed execution-profile names')
    if set(profiles) != set(PROFILES):
        return 'unsupported_execution_contract'
    return 'execution_profiles_v1'


def plan_from_record(record):
    """Decode and validate a frozen JSON plan without accepting extra fields."""
    if type(record) is not dict or type(record.get('machine_budget')) is not dict:
        raise ValueError('invalid allocation plan record')
    budget = record['machine_budget']
    try:
        allocations = tuple(Allocation(a['cpu_ids'], a['participants'], a['memory_planning_bytes'])
                            for a in record['allocations'])
        plan = AllocationPlan(budget['cpu_ids'], allocations, budget['memory_planning_bytes'], record['measured'])
    except (KeyError, TypeError) as error:
        raise ValueError('invalid allocation plan record') from error
    if _digest(record) != _digest(plan.record()):
        raise ValueError('noncanonical or changed allocation plan record')
    return plan


def default_execution_request(*, compatibility_mode='require_profile',
                              requested_execution_profile='efficient-throughput'):
    if compatibility_mode not in ('require_profile', 'legacy_argv'):
        raise ValueError('invalid execution compatibility mode')
    if type(requested_execution_profile) is not str or requested_execution_profile not in PROFILES:
        raise ValueError('invalid requested execution profile')
    return {'schema_version': SCHEMA_VERSION, 'policy_version': POLICY_VERSION,
            'requested_execution_profile': requested_execution_profile,
            'compatibility_mode': compatibility_mode}
