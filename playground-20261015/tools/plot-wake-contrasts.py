"""Plot already audited paired contrasts; no experiments or canonical report edits."""
from pathlib import Path
import hashlib, json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

O = Path(__file__).resolve().parent
audit = O.parents[2] / 'fresh-audit-causal/playground448-final/review.json'
data = json.loads(audit.read_text())
assert data['state'] == 'independently_verified_448_playground_only'
assert data['runs'] == data['all_winners'] == 448 and data['caps'] == 0
rows = [r for r in data['contrasts'] if r['reference'] == 'wake-only' and r['candidate'] != 'baseline']
assert len(rows) == 12 and len({r['candidate'] for r in rows}) == 12
labels = {
    'first-steal': 'First distributed queues + stealing',
    'local-completion-round-robin': 'Local completion: round robin',
    'local-completion-steal': 'Local completion: stealing',
    'local-priority-steal': 'Local priority: stealing',
    'weighted-fixed-no-steal': 'Fixed weights: no stealing',
    'weighted-fixed-steal': 'Fixed weights + stealing',
    'weighted-two-choice': 'Two-choice placement',
    'weighted-last': 'Last-duration estimates',
    'weighted-ewma': 'EWMA duration estimates',
    'weighted-clock-only': 'Clock-only control',
    'weighted-observe-only': 'Observe-only control',
    'weighted-bare-steal': 'Bare-steal control',
}
out = O / 'interpretation'
out.mkdir(exist_ok=True)
fig, axes = plt.subplots(1, 2, figsize=(13, 7), sharey=True, constrained_layout=True)
for ax, metric, title, color in zip(axes, ['e2e_seconds', 'total_cpu_seconds'],
        ['Game completion time', 'Total process CPU'], ['#2878a8', '#c15b22']):
    ax.axvline(0, color='black', linewidth=1)
    for y, row in enumerate(rows):
        r = row['metrics'][metric]
        lo, hi = r['CI95_independent_linear']
        assert lo <= r['change_percent'] <= hi
        ax.hlines(y, lo, hi, color=color, linewidth=2)
        ax.plot(r['change_percent'], y, 'o', color=color, markersize=5)
    ax.set_title(title)
    ax.set_xlabel('Change versus bounded wakeups (%)\nNegative is lower; paired 95% interval')
    ax.grid(axis='x', alpha=.2)
axes[0].set_yticks(range(12), [labels[r['candidate']] for r in rows], fontsize=10)
axes[0].invert_yaxis()
fig.suptitle('Playground: do the more elaborate schedulers improve on bounded wakeups?\n'
             '32 paired seeds; one unrestricted auto32 game at a time', fontsize=13)
fig.savefig(out / 'versus-bounded-wakeups.png', dpi=160)
fig.savefig(out / 'versus-bounded-wakeups.svg')
plt.close(fig)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
(out / 'receipt.json').write_text(json.dumps({
    'state': 'plot_of_existing_independent_audit_no_experiments',
    'source_sha256': sha(Path(__file__)), 'audit_sha256': sha(audit),
    'runs': 448, 'paired_seeds': 32, 'bootstrap': data['bootstrap'],
    'outputs': {p.name: sha(p) for p in out.glob('versus-bounded-wakeups.*')},
    'limits': 'One host/Playground/CPU/auto32. Exploratory intervals; no multiplicity adjustment. Outcome agreement is not whole-game checksum proof.'
}, indent=2) + '\n')
