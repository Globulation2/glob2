#!/usr/bin/env python3
"""Analyze retained GPU proof payloads using an independent heap oracle."""
import argparse
import hashlib
import heapq
import json
from pathlib import Path
import struct


def read_proof(path):
    payload = path.read_bytes()
    if len(payload) < 20:
        raise ValueError('truncated proof header')
    magic, version, width, height, cap = struct.unpack_from('<5I', payload)
    cells = width * height
    if magic != 0x47505246 or version != 1 or not cells:
        raise ValueError('invalid proof header')
    if cap > 65535 or len(payload) != 20 + cells * 10:
        raise ValueError('invalid cap or payload length')
    seeds = list(struct.unpack_from(f'<{cells}H', payload, 20))
    output = list(struct.unpack_from(f'<{cells}I', payload, 20 + cells * 2))
    packed = struct.unpack_from(f'<{cells}I', payload, 20 + cells * 6)
    costs = [(value & 65535, value >> 16) for value in packed]
    if any(not cardinal or not diagonal for cardinal, diagonal in costs):
        raise ValueError('invalid edge cost')
    return width, height, cap, seeds, output, costs


def oracle(width, height, cap, seeds, costs):
    values = seeds.copy()
    queue = [(65535 - value, cell) for cell, value in enumerate(seeds) if value > 1]
    heapq.heapify(queue)
    while queue:
        distance, cell = heapq.heappop(queue)
        if distance > cap or values[cell] != 65535 - distance:
            continue
        x, y = cell % width, cell // width
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if not (dx or dy):
                    continue
                neighbor = ((y + dy) % height) * width + (x + dx) % width
                following = distance + costs[cell][bool(dx and dy)]
                value = 65535 - following
                if seeds[neighbor] and following <= cap and value > max(1, values[neighbor]):
                    values[neighbor] = value
                    heapq.heappush(queue, (following, neighbor))
    return values


def analyze_transfer(path):
    payload = path.read_bytes()
    if len(payload) < 28:
        raise ValueError('truncated transfer header')
    magic, version, slot, width, height, cap, words = struct.unpack_from('<7I', payload)
    cells = width * height
    if (magic != 0x47545246 or version != 1 or not cells or not words or cap > 65535
            or len(payload) != 28 + cells * 2 + words * 8):
        raise ValueError('invalid transfer proof')
    expected = struct.unpack_from(f'<{words}I', payload, 28 + cells * 2)
    actual = struct.unpack_from(f'<{words}I', payload, 28 + cells * 2 + words * 4)
    wrong = [word for word in range(words) if actual[word] != expected[word]]
    return dict(fixture=str(path), sha256=hashlib.sha256(payload).hexdigest(), kind='transfer',
                slot=slot, width=width, height=height, cap=cap, words=words,
                wrong_words=len(wrong), examples=[dict(word=word, expected=expected[word],
                                                     actual=actual[word]) for word in wrong[:16]])


def analyze_control(path):
    payload = path.read_bytes()
    if len(payload) < 28:
        raise ValueError('truncated validation control header')
    magic, version, width, height, cap, first_invalid, marker = struct.unpack_from('<7I', payload)
    cells = width * height
    if (magic != 0x47504346 or version != 1 or not cells or cap > 65535
            or marker != 0x56505246 or len(payload) != 28 + cells * 6
            or (first_invalid != 0xffffffff and first_invalid >= cells)):
        raise ValueError('invalid validation control proof')
    seeds = list(struct.unpack_from(f'<{cells}H', payload, 28))
    packed = struct.unpack_from(f'<{cells}I', payload, 28 + cells * 2)
    costs = [(value & 65535, value >> 16) for value in packed]
    invalid = sum(not cardinal or not diagonal for cardinal, diagonal in costs)
    expected = None if invalid else oracle(width, height, cap, seeds, costs)
    return dict(fixture=str(path), sha256=hashlib.sha256(payload).hexdigest(),
                kind='validation_control', width=width, height=height, cap=cap,
                gpu_first_invalid=first_invalid, actual_field_available=False,
                invalid_input_costs=invalid, oracle_sha256=None if expected is None else
                hashlib.sha256(struct.pack(f'<{cells}H', *expected)).hexdigest())


def analyze(path):
    if path.read_bytes()[:4] == struct.pack('<I', 0x47504346):
        return analyze_control(path)
    if path.read_bytes()[:4] == struct.pack('<I', 0x47545246):
        return analyze_transfer(path)
    width, height, cap, seeds, output, costs = read_proof(path)
    expected = oracle(width, height, cap, seeds, costs)
    wrong = [cell for cell in range(len(seeds)) if output[cell] != expected[cell]]
    return dict(fixture=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                width=width, height=height, cap=cap, wrong_cells=len(wrong),
                above_oracle=sum(output[cell] > expected[cell] for cell in wrong),
                below_oracle=sum(output[cell] < expected[cell] for cell in wrong),
                out_of_range=sum(value > 65535 for value in output),
                examples=[dict(cell=cell, seed=seeds[cell], expected=expected[cell],
                               actual=output[cell], costs=costs[cell]) for cell in wrong[:16]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('proofs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps([analyze(path) for path in args.proofs], indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result)
    else:
        print(result, end='')


if __name__ == '__main__':
    main()
