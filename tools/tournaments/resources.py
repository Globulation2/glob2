"""Frozen tournament CPU shares, supplied by the caller rather than discovered."""
from dataclasses import dataclass
from .common import digest, identifier


def positive_integer(value):
    if type(value) is str and value.isascii() and value.isdecimal():
        value = int(value)
    if type(value) is not int or not 1 <= value <= (1 << 63) - 1:
        raise ValueError('resource count must be a positive integer in 1..INT64MAX')
    return value


def _cpus(values):
    if not isinstance(values, (list, tuple)) or not values:
        raise ValueError('explicit nonempty CPU-ID sequence required')
    if any(type(cpu) is not int or cpu < 0 for cpu in values):
        raise ValueError('CPU IDs must be nonnegative integers')
    if len(set(values)) != len(values):
        raise ValueError('duplicate CPU IDs')
    return tuple(sorted(values))


@dataclass(frozen=True)
class Allocation:
    cpu_ids: tuple
    participants: int

    def __post_init__(self):
        object.__setattr__(self, 'cpu_ids', _cpus(self.cpu_ids))
        if type(self.participants) is not int:
            raise ValueError('participants must be an integer including the simulation owner')
        positive_integer(self.participants)
        if self.participants > len(self.cpu_ids):
            raise ValueError('participants exceed allocated CPU IDs')

    def record(self):
        return {'cpu_ids': list(self.cpu_ids), 'participants': self.participants,
                'participants_include_simulation_owner': True}


@dataclass(frozen=True)
class AllocationPlan:
    cpu_ids: tuple
    allocations: tuple

    def __post_init__(self):
        object.__setattr__(self, 'cpu_ids', _cpus(self.cpu_ids))
        if not isinstance(self.allocations, (tuple, list)) or not self.allocations:
            raise ValueError('nonempty allocations required')
        object.__setattr__(self, 'allocations', tuple(self.allocations))
        used = set()
        for allocation in self.allocations:
            if type(allocation) is not Allocation:
                raise ValueError('allocations must be Allocation records')
            ids = set(allocation.cpu_ids)
            if not ids.issubset(self.cpu_ids) or used.intersection(ids):
                raise ValueError('allocation is outside CPU budget or overlaps')
            used.update(ids)

    def record(self):
        return {'schema_version': 1, 'cpu_ids': list(self.cpu_ids),
                'allocations': [a.record() for a in self.allocations],
                'allocation_frozen': True, 'scope': 'caller_supplied_budget_only'}

    def attempt_record(self, attempt_id, slot):
        identifier(attempt_id)
        if type(slot) is not int or not 0 <= slot < len(self.allocations):
            raise ValueError('invalid allocation slot')
        return {'schema_version': 1, 'attempt_id': attempt_id, 'slot': slot,
                'allocation': self.allocations[slot].record(),
                'allocation_plan_sha256': digest(self.record()), 'allocation_frozen': True}


def plan_allocations(cpu_ids, concurrent_games, *, participants_per_game=None):
    """Partition an explicit budget deterministically; do not infer host ownership."""
    cpus = _cpus(cpu_ids)
    if type(concurrent_games) is not int:
        raise ValueError('concurrent games must be an integer')
    slots = positive_integer(concurrent_games)
    if slots > len(cpus):
        raise ValueError('more games than allocated CPU IDs')
    if participants_per_game is not None:
        if type(participants_per_game) is not int:
            raise ValueError('participants per game must be an integer')
        positive_integer(participants_per_game)
    width, extra = divmod(len(cpus), slots)
    allocations, cursor = [], 0
    for slot in range(slots):
        count = width + (slot < extra)
        allocations.append(Allocation(cpus[cursor:cursor + count],
            count if participants_per_game is None else participants_per_game))
        cursor += count
    return AllocationPlan(cpus, tuple(allocations))


def plan_from_record(record):
    try:
        plan = AllocationPlan(record['cpu_ids'], tuple(
            Allocation(a['cpu_ids'], a['participants']) for a in record['allocations']))
        # Canonical JSON distinguishes booleans from integers and rejects extras.
        if digest(record) != digest(plan.record()):
            raise ValueError('noncanonical allocation plan')
        return plan
    except (KeyError, TypeError) as error:
        raise ValueError('invalid allocation plan record') from error


def validate_frozen_attempt(record, plan, attempt_id):
    try:
        if digest(record) == digest(plan.attempt_record(attempt_id, record['slot'])):
            return plan.allocations[record['slot']]
    except (KeyError, TypeError, ValueError):
        pass
    raise ValueError('frozen attempt allocation changed')


def validate_game_participants(result, allocation):
    """Reject silently reduced executor counts in an otherwise successful game."""
    for name in ('compute_threads', 'compute_resolved_threads'):
        if type(result.get(name)) is not int or result[name] != allocation.participants:
            raise ValueError('game result differs from assigned compute participants: ' + name)
