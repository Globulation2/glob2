#!/usr/bin/env python3
"""Reject platform numeric dependencies in interpreter and pinned math objects."""
import argparse
import re
import subprocess

# Numeric functions in C/POSIX libm and the C conversion library. Namespaced
# glob2_math_* symbols are allowed. Classification/compiler intrinsics are not
# platform libm dependencies. No script-visible exceptions are permitted.
MATH = set('acos acosh asin asinh atan atan2 atanh cbrt ceil copysign cos cosh erf erfc '
           'exp exp2 expm1 fabs fdim floor fma fmax fmin fmod frexp hypot ilogb ldexp '
           'lgamma log log10 log1p log2 logb lrint llrint lround llround modf nan '
           'nearbyint nextafter nexttoward pow remainder remquo rint round scalbln '
           'scalbn sin sinh sqrt tan tanh tgamma trunc strtod strtof strtold atof'.split())
FORBIDDEN = MATH | {name + suffix for name in MATH for suffix in ('f', 'l')}


def check(objects, nm='nm'):
    for obj in objects:
        output = subprocess.check_output([nm, '-u', str(obj)], text=True)
        unexpected = []
        for line in output.splitlines():
            symbol = line.strip().split()[-1] if line.strip() else ''
            # Mach-O and 32-bit COFF prepend underscores. MinGW may use __imp_*.
            symbol = re.sub(r'^_*imp_', '', symbol).lstrip('_').split('@')[0]
            if symbol in FORBIDDEN:
                unexpected.append(symbol)
        if unexpected:
            raise RuntimeError(f'{obj}: platform numeric symbols: {", ".join(sorted(set(unexpected)))}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nm', default='nm')
    parser.add_argument('objects', nargs='+')
    args = parser.parse_args()
    check(args.objects, args.nm)
