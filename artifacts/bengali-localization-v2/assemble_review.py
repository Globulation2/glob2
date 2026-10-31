"""Reproduce V2 audit from preserved actual V1 reviews plus six real delta reviews.

This does not perform or claim a fresh linguistic reading of carried entries.
Run from any directory; logs used for structural gates must already exist.
"""
import copy
import importlib.util
import json
from pathlib import Path

KIT = Path(__file__).resolve().parent
ROOT = KIT.parents[1]
OLD = KIT.with_name('bengali-localization')
spec = importlib.util.spec_from_file_location('translation_program', ROOT / 'tools/localization/translation_program.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
read = lambda path: json.loads(path.read_text())
hashfile = lambda path: p.digest(path.read_bytes())

def evidence(path):
    import os
    return {'path': os.path.relpath(path, KIT), 'sha256': hashfile(path)}

def write(name, value):
    path = KIT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')

manifest = read(KIT / 'manifest.json')
assert p.sources()[4] == manifest['source_sha256']
paths = p.catalog_paths(KIT, 'bn')
hashes = {surface: hashfile(path) for surface, path in paths.items()}
native, errors = p.CHECKER.read_table(paths['native'])
assert not errors
targets = {'native': native, 'online': read(paths['online'])}
sources = {s: read(KIT / 'source' / (s + '.json')) for s in targets}
assert (ROOT / 'data/texts.bn.txt').read_bytes() == paths['native'].read_bytes()
assert (ROOT / 'platform/packages/i18n/locales/bn.json').read_bytes() == paths['online'].read_bytes()
carry = read(KIT / 'carry-forward.json')
assert carry['new_source_sha256'] == manifest['source_sha256']
assert carry['native_approved_catalog_sha256'] == hashes['native']
assert carry['online_catalog_sha256'] == hashes['online']
assert hashfile(OLD / 'author-delta.json') == carry['author_delta_sha256']
previous = read(OLD / 'reviews/bn.json')
assert previous['source_sha256'] == carry['old_source_sha256']

# Check all preserved review values, not just catalog hashes or filled key counts.
prior_semantic, prior_adversary = {}, {}
prior_paths = [OLD / 'reviews/bn.json']
for surface in targets:
    semantic_path = OLD / ('final-' + surface + '-semantic-review.json')
    adversary_path = OLD / 'adversarial-work' / ('final-' + surface + '-review.json')
    sem, adv = read(semantic_path), read(adversary_path)
    assert sem['status'] == adv['status'] == 'pass'
    assert not sem['open_findings'] and not adv['unresolved']
    assert sem['catalog_sha256'] == adv['catalog_sha256'] == previous['catalog_sha256'][surface]
    for entry in sem['entries']:
        assert entry['id'] not in prior_semantic
        prior_semantic[entry['id']] = (entry['source'], entry['translation'])
    for entry in adv['per_key']:
        ident = entry['key'] if entry['key'].startswith(surface + ':') else surface + ':' + entry['key']
        assert ident not in prior_adversary
        prior_adversary[ident] = (entry['source'], entry['target'])
    prior_paths += [semantic_path, adversary_path]
assert len(prior_semantic) == len(prior_adversary) == 6229
assert prior_semantic == prior_adversary
carry_rows = {entry['id']: entry for entry in carry['unchanged_source_target_pairs']}
assert len(carry_rows) == len(carry['unchanged_source_target_pairs']) == 6229
assert set(carry_rows) == set(prior_semantic)
for ident, entry in carry_rows.items():
    surface, key = ident.split(':', 1)
    assert (entry['source'], entry['target']) == prior_semantic[ident]
    assert (sources[surface][key], targets[surface][key]) == prior_semantic[ident]

# V1 aggregate binds all author/editor/semantic/adversarial actual coverage identities.
prior_batch_by_key = {}
for batch in previous['batches']:
    keys = set(batch['reviewed_keys'])
    assert keys == set(batch['edited_keys']) == set(batch['rechecked_keys']) == set(batch['adversarial_reviewed_keys'])
    assert len({batch[x] for x in ['author', 'reviewer', 'editor', 'adversarial_reviewer']}) == 4
    for key in keys:
        assert key not in prior_batch_by_key
        prior_batch_by_key[key] = batch
assert set(prior_batch_by_key) == set(carry_rows)
for item in previous['independent_review_evidence']:
    path = OLD / item['path']
    assert hashfile(path) == item['sha256'], str(path) + ': preserved V1 evidence changed'
    prior_paths.append(path)

# Fresh actual six-pair author, semantic, editorial and adversarial examinations.
author = read(OLD / 'author-delta.json')
semantic = read(KIT / 'semantic-delta.json')
editor = read(KIT / 'editor-delta.json')
adversary = read(KIT / 'adversarial-delta.json')
assert semantic['status'] == adversary['verdict'] == 'pass'
assert not semantic['findings'] and not editor['findings'] and not adversary['findings']
assert semantic['source_sha256'] == editor['source_sha256'] == adversary['new_source_sha256'] == manifest['source_sha256']
assert semantic['online_catalog_sha256'] == adversary['online_catalog_sha256'] == hashes['online']
assert editor['catalog_sha256'] == hashes
assert editor['input_carry_forward_sha256'] == adversary['carry_forward_sha256'] == hashfile(KIT / 'carry-forward.json')
assert editor['input_author_delta_sha256'] == hashfile(OLD / 'author-delta.json')
new_rows = {entry['id']: entry for entry in author['entries']}
sem_rows = {entry['id']: entry for entry in semantic['entries']}
ed_rows = {entry['id']: entry for entry in editor['entries']}
adv_rows = {'online:' + entry['key']: entry for entry in adversary['freshly_reviewed_new_pairs']}
assert len(new_rows) == 6
assert set(new_rows) == set(sem_rows) == set(ed_rows) == set(adv_rows)
for ident, row in new_rows.items():
    key = ident.split(':', 1)[1]
    pair = (sources['online'][key], targets['online'][key])
    assert pair == (row['source'], row['target'])
    assert pair == (sem_rows[ident]['source'], sem_rows[ident]['translation'])
    assert pair == (ed_rows[ident]['source'], ed_rows[ident]['final_target'])
    assert pair == (adv_rows[ident]['source'], adv_rows[ident]['target'])
    assert sem_rows[ident]['verdict'] == adv_rows[ident]['verdict'] == 'pass'
    assert prior_semantic['native:' + row['native_key']] == pair
expected = {key for b in manifest['batches'] for key in b['keys']}
assert expected == set(carry_rows) | set(new_rows)
assert not set(carry_rows) & set(new_rows) and len(expected) == 6235

checked = p.check_draft(KIT, 'bn')
assert not checked['errors'] and not checked['untranslated'], checked
write('structural-current-check-draft.json', {'command': 'translation_program.check_draft(current V2, bn)',
    'source_sha256': manifest['source_sha256'], 'catalog_sha256': hashes,
    'result': checked, 'exit_status': 0})
accepted = previous['accepted_english']
assert len(accepted) == len({e['key'] for e in accepted}) == 162
assert {e['key'] for e in accepted} == set(checked['same_as_english_review_queue'])
assert all(e['reason'].strip() for e in accepted)

record = {'locale': 'bn', 'source_sha256': manifest['source_sha256'], 'catalog_sha256': hashes,
    'certification': 'Agent-reviewed; not native-human-certified', 'batches': [],
    'language_brief': copy.deepcopy(previous['language_brief']), 'findings': copy.deepcopy(previous['findings']),
    'accepted_english': copy.deepcopy(accepted), 'coverage_provenance': [], 'gates': {}}
batch_hashes = p.batch_target_hashes(manifest, paths)
for batch in manifest['batches']:
    surface = batch['id'].split('-', 1)[0]
    people = {'author': '/root/bengali_author' if surface == 'native' else '/root/bengali_online_author',
              'reviewer': '/root/rendering_audit',
              'editor': '/root/bengali_online_author' if surface == 'native' else '/root/bengali_editor',
              'adversarial_reviewer': '/root/bengali_adversary'}
    for ident in batch['keys']:
        if ident in carry_rows:
            assert all(prior_batch_by_key[ident][role] == value for role, value in people.items())
    keys = batch['keys']
    record['batches'].append({'id': batch['id'], 'source_sha256': manifest['source_sha256'],
        'target_sha256': batch_hashes[batch['id']], **people, 'reviewed_keys': keys, 'edited_keys': keys,
        'rechecked_keys': keys, 'rechecked_by': people['reviewer'], 'adversarial_reviewed_keys': keys,
        'carried_prior_actual_read_keys': [k for k in keys if k in carry_rows],
        'fresh_delta_actual_read_keys': [k for k in keys if k in new_rows],
        'coverage_note': 'Carried keys inherit actual prior review by exact source-target equality; only six added online keys received new direct reading.'})
    for ident in keys:
        s, key = ident.split(':', 1)
        record['coverage_provenance'].append({'id': ident, 'source': sources[s][key], 'target': targets[s][key],
            'basis': 'prior actual independent reading carried by exact pair equality; no fresh-reading claim' if ident in carry_rows else 'fresh author plus three independent six-key delta reviews',
            'prior_batch': prior_batch_by_key[ident]['id'] if ident in carry_rows else None})
record['coverage_summary'] = {'complete_keys': 6235, 'carried_actual_prior_read_pairs': 6229,
    'freshly_reviewed_new_pairs': 6, 'native_keys': 3393, 'online_keys': 2842,
    'mechanical_rebinding_is_not_fresh_review': True}
new_evidence = [KIT / name for name in ['carry-forward.json', 'semantic-delta.json', 'editor-delta.json', 'adversarial-delta.json']]
new_evidence.append(OLD / 'author-delta.json')
record['independent_review_evidence'] = [evidence(path) for path in dict.fromkeys(prior_paths + new_evidence)]
for gate, names in {
    'language_brief': ['language-brief.json', 'final-native-semantic-review.json', 'final-online-semantic-review.json'],
    'source_based_pilot': ['source-based-pilot.json', 'repaired-pilot-semantic-review.json', 'editorial-pilot-review.json'],
}.items():
    for name in names:
        assert (OLD / name).is_file()
    record['gates'][gate] = {'passed': True, 'source_sha256': manifest['source_sha256'], 'catalog_sha256': hashes,
        'basis': 'Preserved V1 brief/pilot workflow carried through exact equality of every prior source-target pair, with six added labels independently reviewed against unchanged brief; not a new pilot execution.',
        'evidence': [evidence(OLD / name) for name in names] + [evidence(path) for path in new_evidence]}
structural_names = ['structural-current-check-draft.json', 'translations-strict.log', 'translations-tests.log', 'i18n-check.log', 'i18n-tests.log']
assert 'OK' in (KIT / 'translations-tests.log').read_text()
assert '2842 messages' in (KIT / 'i18n-check.log').read_text()
assert '86 passed' in (KIT / 'i18n-tests.log').read_text()
record['gates']['structural'] = {'passed': True, 'source_sha256': manifest['source_sha256'], 'catalog_sha256': hashes,
    'basis': 'Current V2 check-draft, current strict native check and translation tests, and current 37×2842 i18n inventory/86 tests; no inherited V1 structural run.',
    'evidence': [evidence(KIT / name) for name in structural_names]}
for gate in p.GATES:
    if gate not in record['gates']:
        record['gates'][gate] = {'passed': False, 'evidence': [], 'reason': 'Intentionally pending: assembly does not claim rendering, startup or platform execution.'}
write('reviews/bn.json', record)
issues = p.audit_locale(KIT, manifest, record, 'bn')
expected_issues = [gate + ' is pending or lacks evidence' for gate in p.GATES if not record['gates'][gate]['passed']]
assert issues == expected_issues, issues
write('assembly-audit.json', {'source_sha256': manifest['source_sha256'], 'catalog_sha256': hashes,
    'coverage': record['coverage_summary'], 'accepted_english': 162, 'canonical_batches': len(record['batches']),
    'check_draft_errors': checked['errors'], 'audit_issues': issues,
    'status': 'Review/structural provenance valid; release blocked by pending execution gates'})
print(json.dumps({'keys': 6235, 'carried': 6229, 'fresh': 6, 'batches': len(record['batches']),
    'accepted_english': 162, 'remaining_gates': issues}, indent=2))
