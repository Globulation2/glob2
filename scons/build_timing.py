"""Opt-in per-action intervals for local build benchmarks (no signature inputs)."""
import json
import os
import threading
import time

_LOCK = threading.Lock()
_STARTED = {}
_REGISTERED = set()
_PHASES = {}


def begin(target, source, env):
    with _LOCK:
        _STARTED[tuple(node.abspath for node in target)] = time.monotonic()
    return 0


def end(target, source, env):
    stopped = time.monotonic()
    key = tuple(node.abspath for node in target)
    with _LOCK:
        started = _STARTED.pop(key)
        with open(os.environ['GLOB2_BUILD_TIMING_LOG'], 'a') as output:
            output.write(json.dumps({'phase': _PHASES[key], 'targets': key, 'start': started, 'end': stopped}) + '\n')
    return 0


def record(env, nodes, phase):
    if not os.environ.get('GLOB2_BUILD_TIMING_LOG'):
        return nodes
    from SCons.Script import Action, Flatten
    local = env
    for node in Flatten(nodes):
        key = node.abspath
        if key in _REGISTERED:
            continue
        _REGISTERED.add(key)
        _PHASES[tuple(item.abspath for item in node.get_executor().get_all_targets())] = phase
        local.AddPreAction(node, Action(begin, cmdstr=None))
        local.AddPostAction(node, Action(end, cmdstr=None))
    return nodes
