import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys

root = Path('/home/bradley/glob2-gradient-unified-h128')
base = Path(__file__).resolve().parent
sys.path.insert(0, str(root))
from tools.adaptive_gradient_model import validate, predict
from tools.adaptive_gradient_codegen import render

model_path = root / 'tools/models/adaptive-gradient.json'
header_path = root / 'src/map/gradient/AdaptiveGradientModel.h'
model = json.loads(model_path.read_text())
validate(model)
assert model['trained'] is True
assert render(model) == header_path.read_text()
cpp = base / 'parity.cpp'
cpp.write_text('#include "' + str(header_path) + '"\n#include <iostream>\nint main(){AdaptiveGradientModel::BaseFeatures b;AdaptiveGradientModel::MovementCounts c;while(std::cin>>b[0]){for(unsigned i=1;i<b.size();++i)std::cin>>b[i];for(auto&v:c)std::cin>>v;std::cout<<AdaptiveGradientModel::predict(b,c)<<"\\n";}}\n')
argv = ['g++', '-std=c++20', '-O2', '-fsanitize=undefined', '-fno-sanitize-recover=undefined', str(cpp), '-o', str(base / 'parity')]
subprocess.run(argv, check=True, capture_output=True)
rng = random.Random(181101)
values = [0, 1, 8, 127, 128, 1000000, 1000001, (1 << 32) - 1]
queries = [0, 1, 2, 1000001, (1 << 62), (1 << 64) - 1]
vectors = []
for q in queries:
    for count in [0, q // 2, q]:
        vectors.append(([128, 17, 99, 3, 7, 123, 456], [count, q - count, q]))
for _ in range(512):
    q = rng.choice(queries)
    vectors.append(([rng.choice(values) for _ in range(7)], [rng.randrange(q + 1), rng.randrange(q + 1), q]))
raw = ''.join(' '.join(map(str, b + c)) + '\n' for b, c in vectors)
result = subprocess.run([str(base / 'parity')], input=raw, text=True, capture_output=True, check=True)
expected = [predict(model, b, c) for b, c in vectors]
actual = list(map(int, result.stdout.split()))
assert actual == expected, 'C++/Python candidate prediction mismatch'
receipt = dict(source_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
               model_sha256=hashlib.sha256(model_path.read_bytes()).hexdigest(),
               header_sha256=hashlib.sha256(header_path.read_bytes()).hexdigest(),
               generated_header_exact=True, integer_accumulator_bounds=True, ubsan=True,
               vectors=len(vectors), prediction_min=min(actual), prediction_max=max(actual),
               cpp_python_predictions_equal=True, compiler_argv=argv,
               vectors_sha256=hashlib.sha256(raw.encode()).hexdigest())
(base / 'PARITY.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
