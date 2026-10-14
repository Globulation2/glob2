"""Explicit CPU allocation options shared by tournament entry points."""
from .resources import plan_allocations, positive_integer


def parse_cpu_ids(value):
    if not isinstance(value, str) or not value or any(
            not part.isascii() or not part.isdecimal() for part in value.split(',')):
        raise ValueError('--cpu-ids requires comma-separated nonnegative CPU IDs')
    ids = [int(part) for part in value.split(',')]
    if len(set(ids)) != len(ids):
        raise ValueError('duplicate CPU IDs')
    return ids


def add_budget_arguments(parser):
    parser.add_argument('--cpu-ids', help='caller-owned logical CPU IDs, comma-separated')
    parser.add_argument('--jobs', type=positive_integer, help='concurrent games within the supplied CPU budget')
    parser.add_argument('--compute-participants', type=positive_integer,
                        help='participants per game including the simulation owner')


def configure_hosts(hosts, *, cpu_ids=None, slots=None, participants_per_game=None):
    """Known local workers share a machine; SSH aliases cannot establish identity."""
    if not isinstance(hosts, list) or not hosts or any(type(host) is not dict for host in hosts):
        raise ValueError('hosts must be a nonempty list of configurations')
    if any(v is not None for v in (cpu_ids, slots, participants_per_game)) and len(hosts) != 1:
        raise ValueError('CLI CPU overrides require exactly one host; use per-host JSON for multiple hosts')
    result = [dict(host) for host in hosts]
    for host in result:
        if cpu_ids is not None: host['cpu_ids'] = list(cpu_ids)
        if slots is not None: host['slots'] = positive_integer(slots)
        if participants_per_game is not None: host['participants_per_game'] = positive_integer(participants_per_game)
        if host.get('cpu_ids') is not None:
            if 'slots' not in host:
                raise ValueError('explicit --jobs or host slots required with CPU IDs')
            plan_allocations(host['cpu_ids'], host['slots'], participants_per_game=host.get('participants_per_game'))
        elif host.get('participants_per_game') is not None:
            raise ValueError('participants per game requires an explicit CPU budget')
    local = [host for host in result if host.get('transport', 'ssh') == 'local']
    if any(host.get('cpu_ids') is not None for host in local):
        used = set()
        for host in local:
            if host.get('cpu_ids') is None:
                raise ValueError('explicit CPU budgets required for every local host')
            ids = set(host['cpu_ids'])
            if used.intersection(ids):
                raise ValueError('overlapping CPU budgets on the same known local machine')
            used.update(ids)
    return result
