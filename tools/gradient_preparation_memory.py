#!/usr/bin/env python3
"""Measure native snapshot record sizes and model bounded preparation footprints.

This is a conservative worksheet, not allocation telemetry or a GPU benchmark.
Every overlap version counts whole arrays. Shared table owners count once.
The output lists omitted variable catalog allocations so it cannot be used as
an admission proof; admission must charge the actual retained owners.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MIB = 1024 * 1024
PROBE = r'''
#include "sim/snapshot/WorldSnapshot.h"
#include <cstddef>
#include <iostream>
int main() {
using namespace SimulationSnapshot;
std::cout << "{\"resource\":" << sizeof(ResourceCell)
 << ",\"occupancy\":" << sizeof(OccupancyCell)
 << ",\"areas\":" << sizeof(AreaCell)
 << ",\"terrain_cell\":" << sizeof(Uint16)
 << ",\"terrain_vertex\":" << sizeof(TerrainType)
 << ",\"stock_index\":" << sizeof(Uint32)
 << ",\"stock\":" << sizeof(std::array<Uint16, MaterialCount>)
 << ",\"visibility\":" << 2 * sizeof(Uint32)
 << ",\"cell_rule\":" << sizeof(CellRule)
 << ",\"preparation_rule\":" << sizeof(GradientPreparationRule)
 << ",\"cpu_seed_columns\":" << CellRuleTable::OpenTerrainClasses.size() * sizeof(Uint16)
 << ",\"resource_properties\":" << sizeof(ResourceProperties)
 << ",\"resource_yields\":" << sizeof(ResourceYields)
 << ",\"chunk_stamp\":" << sizeof(Uint64)
 << ",\"resource_capacity\":" << ResourceRegistry::Capacity
 << ",\"rule_capacity\":" << CellRuleTable::Capacity << "}\n";
}
'''


def measure(compiler, includes):
    sys.path.insert(0, str(ROOT / 'scons'))
    from sources import INCLUDE_DIRECTORIES
    with tempfile.TemporaryDirectory(prefix='glob2-gradient-layout-') as directory:
        source = Path(directory) / 'layout.cpp'
        binary = Path(directory) / 'layout'
        source.write_text(PROBE)
        command = [compiler, '-std=c++20']
        command += ['-I' + str(ROOT / path) for path in INCLUDE_DIRECTORIES]
        command += ['-I' + path for path in includes]
        command += [str(source), '-o', str(binary)]
        subprocess.run(command, check=True)
        return json.loads(subprocess.check_output([str(binary)], text=True))


def worksheet(layout, jobs=8, table_rules=256, table_resources=64):
    records = sum(layout[key] for key in ('resource', 'occupancy', 'areas', 'terrain_cell',
                                         'terrain_vertex', 'stock_index', 'visibility'))
    # GPU does not need terrain vertices. It reads the cell rules compiled on CPU.
    gpu_records = records - layout['terrain_vertex']
    tables_host = table_rules * (layout['cell_rule'] + layout['preparation_rule'] + layout['cpu_seed_columns'])
    tables_host += table_resources * (layout['resource_properties'] + layout['resource_yields'] + 4)
    tables_device = table_rules * layout['preparation_rule'] + table_resources * 4
    rows = []
    for side in (128, 256, 512):
        cells, chunks = side * side, ((side + 15) // 16) ** 2
        for stocks in (False, True):
            for versions in (1, 2, 4):
                whole_host = cells * (records + (layout['stock'] if stocks else 0))
                whole_device = cells * (gpu_records + (layout['stock'] if stocks else 0))
                metadata = chunks * layout['chunk_stamp'] * 5
                # Each pending field retains a private 16-bit output. Readback is
                # 32-bit. At most one coordinator batch owns solve/storage/staging.
                outputs = jobs * cells * 2
                staging = whole_device + metadata
                batch_host = cells * 4 * min(jobs, 8)
                # a/b/cost/readback plus active/next tile masks and parameter blocks.
                batch_device = (cells * 16 + chunks * 8 + 256) * min(jobs, 8)
                host = versions * (whole_host + metadata) + tables_host + outputs + staging + batch_host
                device = versions * (whole_device + metadata) + tables_device + batch_device
                rows.append({'side': side, 'stock_sidecar_every_cell': stocks,
                             'snapshot_versions': versions, 'pending_fields': jobs,
                             'host_bytes': host, 'device_bytes': device,
                             'host_limit_fits_before_omissions': host <= 64 * MIB,
                             'device_limit_fits_before_omissions': device <= 128 * MIB})
    return {'record_sizes': layout, 'table_rules': table_rules, 'table_resources': table_resources,
            'assumptions': ['whole-version arrays have capacity equal to their logical length',
                            'whole-snapshot host staging remains live together',
                            'eight fields can retain separate 32-bit host readbacks',
                            'all versions share one table owner'],
            'budgets': {'host': 64 * MIB, 'device': 128 * MIB},
            'omissions': ['variable catalog strings, terrain whitelist/habitat allocations and indexes',
                          'allocator/control-block overhead and adapter allocation alignment',
                          'shared budgets already consumed by other gradient work'],
            'rows': rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--include', action='append', default=[])
    parser.add_argument('--pending-fields', type=int, default=8)
    parser.add_argument('--table-rules', type=int, default=256)
    parser.add_argument('--table-resources', type=int, default=64)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 1 <= args.pending_fields <= 64 or not 1 <= args.table_rules <= 65536 or not 1 <= args.table_resources <= 16384:
        parser.error('field/table counts outside supported capacity')
    prefix = os.environ.get('GLOB2_SDL3_PREFIX')
    includes = args.include + ([str(Path(prefix) / 'include')] if prefix else [])
    result = worksheet(measure(args.compiler, includes), args.pending_fields, args.table_rules, args.table_resources)
    content = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content)
    else:
        print(content, end='')
