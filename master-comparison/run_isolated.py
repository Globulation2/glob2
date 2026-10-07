import pathlib
import run_benchmark as b
b.ROOT=b.ROOT/'isolated';b.ROOT.mkdir(exist_ok=True)
for repeat in range(6):
 for case in (list(b.FIXTURES) if repeat%2==0 else list(reversed(b.FIXTURES))):
  b.pair(case,4,repeat,2000,500)
for repeat in range(4):
 for case in b.FIXTURES:b.pair(case,4,repeat,2000,500,draw=0,phase='control')
