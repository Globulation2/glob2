"""Explicit caller budgets for local experiment admission; no host discovery."""
from .resources import plan_allocations


def parse_cpu_ids(value):
    """Comma-separated logical CPU IDs; allocation belongs to the caller."""
    if not isinstance(value, str) or not value or any(not part.isascii() or not part.isdecimal() for part in value.split(',')):
        raise ValueError('--cpu-ids requires comma-separated nonnegative CPU IDs')
    ids = [int(part) for part in value.split(',')]
    if len(set(ids)) != len(ids):
        raise ValueError('duplicate CPU IDs')
    return ids


def positive_integer(value):
    if type(value) is int:
        number = value
    elif isinstance(value, str) and value.isascii() and value.isdecimal():
        number = int(value)
    else:
        raise ValueError('resource budget must be a positive integer')
    if not 1 <= number <= (1 << 63) - 1:
        raise ValueError('resource budget must be in 1..INT64MAX')
    return number


def add_budget_arguments(parser):
    parser.add_argument('--cpu-ids', help='caller-owned logical CPU IDs, comma-separated; no whole-host default')
    parser.add_argument('--jobs', type=int, help='explicit concurrent games within the supplied CPU budget')
    parser.add_argument('--compute-participants', type=positive_integer, help='participants per game including simulation owner; within each assigned CPU mask')
    parser.add_argument('--memory-planning-budget-bytes', help='shared soft planning reservation, not an RSS limit')


def configure_hosts(hosts, *, cpu_ids=None, slots=None, memory_planning_budget_bytes=None, participants_per_game=None,
                    resource_aware=False):
    """Validate caller budgets; CLI overrides target exactly one host.

    Explicit local transports in this host set run on the same machine, so their
    CPU allocations must be disjoint even with independent worker directories.
    SSH aliases do not prove distinct machine identities. This is not a global
    admission service: other host sets/packages need caller coordination.
    """
    if not isinstance(hosts, list) or not hosts or any(type(host) is not dict for host in hosts):
        raise ValueError('hosts must be a nonempty list of host configurations')
    if any(value is not None for value in (cpu_ids, slots, memory_planning_budget_bytes, participants_per_game)) and len(hosts) != 1:
        raise ValueError('CLI resource overrides require exactly one host; use per-host JSON budgets for multiple hosts')
    result = [dict(host) for host in hosts]
    for host in result:
        if cpu_ids is not None: host['cpu_ids'] = list(cpu_ids)
        if slots is not None: host['slots'] = positive_integer(slots)
        if participants_per_game is not None: host['participants_per_game'] = positive_integer(participants_per_game)
        if memory_planning_budget_bytes is not None:
            host['memory_planning_budget_bytes'] = positive_integer(memory_planning_budget_bytes)
        if host.get('cpu_ids') is not None:
            if 'slots' not in host:
                raise ValueError('explicit --jobs or host slots required with CPU IDs')
            plan_allocations(host['cpu_ids'], host['slots'],
                memory_planning_budget_bytes=host.get('memory_planning_budget_bytes'),
                participants_per_game=host.get('participants_per_game'))
            if host.get('memory_planning_budget_bytes') is not None:
                positive_integer(host['memory_planning_budget_bytes'])
        elif resource_aware or host.get('memory_planning_budget_bytes') is not None or host.get('participants_per_game') is not None:
            raise ValueError('resource-aware games require explicit --cpu-ids or host cpu_ids, plus concurrency')
    local = [host for host in result if host.get('transport', 'ssh') == 'local']
    if any(host.get('cpu_ids') is not None for host in local):
        if any(host.get('cpu_ids') is None for host in local):
            raise ValueError('known-local worker CPU claims require explicit budgets for every local host')
        used = set()
        for host in local:
            ids = set(host['cpu_ids'])
            if used.intersection(ids):
                raise ValueError('overlapping CPU budgets on the same known local machine')
            used.update(ids)
    for host in result:
        if host.get('cpu_ids') is not None:
            host['resource_identity_scope'] = ('known_local_machine_host_set_only'
                if host.get('transport', 'ssh') == 'local' else 'ssh_alias_machine_identity_unverified')
    return result


def native_execution_flags(description, profile, compatibility_mode, memory_bytes=None):
    """Build only advertised flags. Explicit older CLI2 retains original argv."""
    from .resources import PROFILES, classify_cli_capability
    if profile not in PROFILES: raise ValueError('invalid execution profile')
    support=classify_cli_capability(description)
    if compatibility_mode == 'legacy_argv':
        if support != 'legacy_cli2': raise ValueError('legacy_argv requires explicitly selected older CLI2')
        if memory_bytes is not None: raise ValueError('older CLI2 cannot report a planning memory budget')
        return []
    if compatibility_mode != 'require_profile' or support != 'execution_profiles_v1':
        raise ValueError('binary does not advertise execution profiles; select older compatibility explicitly')
    commands=[c for c in description['commands'] if isinstance(c,dict) and c.get('path')=='game run' and c.get('available') is True]
    if len(commands)!=1: raise ValueError('registered game run command required')
    names={o.get('name') for o in commands[0].get('options',[]) if isinstance(o,dict)}
    required={'--execution-profile','--compute-threads'}
    if memory_bytes is not None: required.add('--memory-budget-bytes'); positive_integer(memory_bytes)
    if not required.issubset(names): raise ValueError('binary does not advertise requested resource options')
    return ['--execution-profile',profile]+(['--memory-budget-bytes',str(memory_bytes)] if memory_bytes is not None else [])


def prepare_native_resources(binaries, cpu_ids, profile, compatibility_modes, memory_bytes=None):
    """Runtime caller admission/probe once, before benchmark timing; no auto CPU discovery."""
    import json, os, subprocess
    if cpu_ids is None:
        if any(mode!='legacy_argv' for mode in compatibility_modes.values()):
            raise ValueError('profile-aware measurements require explicit --cpu-ids')
    else:
        configure_hosts([{'slots':1}],cpu_ids=cpu_ids,memory_planning_budget_bytes=memory_bytes,resource_aware=True)
        if not hasattr(os,'sched_setaffinity'): raise ValueError('this platform cannot enforce requested CPU IDs')
        if not set(cpu_ids).issubset(os.sched_getaffinity(0)): raise ValueError('requested CPU IDs exceed caller affinity allocation')
        os.sched_setaffinity(0,cpu_ids)
        if set(os.sched_getaffinity(0))!=set(cpu_ids): raise ValueError('CPU affinity enforcement failed')
    descriptions={}; flags={}
    for role,binary in binaries.items():
        probe=subprocess.run([str(binary),'help','--format','json'],capture_output=True,text=True,check=True,timeout=30)
        descriptions[role]=json.loads(probe.stdout)
        flags[role]=native_execution_flags(descriptions[role],profile,compatibility_modes[role],memory_bytes)
    return {'cpu_ids':cpu_ids,'cpu_enforcement':'process_inherited_affinity' if cpu_ids is not None else 'explicit_legacy_not_enforced',
            'requested_execution_profile':profile,'memory_planning_bytes':memory_bytes,
            'memory_enforcement':'soft_planning_only','compatibility_modes':compatibility_modes,
            'cli_descriptions':descriptions,'flags':flags}


def validate_native_resource_result(result, receipt, role, participants):
    """Validate a measured direct run; a requested budget is not actual execution."""
    if receipt['compatibility_modes'][role]=='legacy_argv':
        return 'explicit_legacy_resolved_plan_unknown'
    from .resources import PROFILES
    if type(participants) is not int or participants < 1:
        raise ValueError('measured participants must be positive integer')
    plan=result.get('execution_resources')
    requested=receipt['requested_execution_profile']
    if type(requested) is not str or requested not in PROFILES:
        raise ValueError('invalid measured execution profile intent')
    # Validate the frozen CPU-ID/memory shape without inferring an optimal count.
    plan_allocations(receipt['cpu_ids'], 1, memory_planning_budget_bytes=receipt['memory_planning_bytes'])
    if (type(plan) is not dict or type(plan.get('schema_version')) is not int or plan['schema_version']!=1
            or type(plan.get('policy_version')) is not int or plan['policy_version']!=1
            or plan.get('requested_execution_profile')!=requested
            or plan.get('selected_execution_profile') not in PROFILES
            or (requested!='auto' and plan['selected_execution_profile']!=requested)
            or plan.get('profile_source')!='explicit_profile'
            or type(plan.get('profile_tuning_pending')) is not bool):
        raise ValueError('engine did not return the requested execution-resource profile/provenance')
    resources=plan.get('resolved_resources'); capabilities=plan.get('device_capabilities')
    if type(resources) is not dict or type(capabilities) is not dict:
        raise ValueError('missing resolved resources/device capabilities')
    if (type(resources.get('compute_participants')) is not int or resources['compute_participants']!=participants
            or type(capabilities.get('allocated_cpu_participants')) is not int
            or capabilities['allocated_cpu_participants']!=len(receipt['cpu_ids'])
            or type(result.get('compute_threads')) is not int or result['compute_threads']!=participants
            or type(result.get('compute_resolved_threads')) is not int or result['compute_resolved_threads']!=participants):
        raise ValueError('actual/frozen engine participants or allocation differ from measured caller budget')
    if receipt['memory_planning_bytes'] is not None and (type(resources.get('planning_memory_budget_bytes')) is not int
            or resources['planning_memory_budget_bytes']!=receipt['memory_planning_bytes']):
        raise ValueError('engine planning memory differs from requested budget')
    return 'validated_engine_resolved_plan'
