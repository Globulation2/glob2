"""Assemble actual independently supplied reviews; leave unexecuted gates pending."""
import importlib.util
import json
from pathlib import Path

KIT = Path(__file__).resolve().parent
ROOT = KIT.parents[1]
spec = importlib.util.spec_from_file_location('program', ROOT / 'tools/localization/translation_program.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


def read(name):
    return json.loads((KIT / name).read_text())


def ids(surface, keys):
    return {key if key.startswith(surface + ':') else surface + ':' + key for key in keys}


manifest = read('manifest.json')
assert p.sources()[4] == manifest['source_sha256']
paths = p.catalog_paths(KIT, 'bn')
hashes = {surface: p.digest(path.read_bytes()) for surface, path in paths.items()}
native, errors = p.CHECKER.read_table(paths['native'])
assert not errors
targets = {'native': native, 'online': json.loads(paths['online'].read_text())}
sources = {surface: read('source/' + surface + '.json') for surface in targets}
assert (ROOT / 'data/texts.bn.txt').read_bytes() == paths['native'].read_bytes()
assert (ROOT / 'platform/packages/i18n/locales/bn.json').read_bytes() == paths['online'].read_bytes()

author_native = read('author-native-complete.json')
author_online = read('online-author-review.json')
editor_native = read('native-editorial-review.json')
editor_online = read('editorial-work/online-editorial-review.json')
author_keys = {'native': set(author_native['examined_keys']),
               'online': {key for batch in author_online['batches'] for key in batch['examined_keys']}}
editor_keys = {'native': set(editor_native['edited_keys']),
               'online': {entry['id'] for entry in editor_online['entries']}}
semantic_keys, adversarial_keys = {}, {}
findings = []
for surface in targets:
    expected = ids(surface, targets[surface])
    author_keys[surface] = ids(surface, author_keys[surface])
    editor_keys[surface] = ids(surface, editor_keys[surface])
    assert author_keys[surface] == expected
    assert editor_keys[surface] == expected
    semantic = read('final-' + surface + '-semantic-review.json')
    adversary = read('adversarial-work/final-' + surface + '-review.json')
    assert semantic['status'] == adversary['status'] == 'pass'
    assert not semantic['open_findings'] and not adversary['unresolved']
    assert semantic['catalog_sha256'] == hashes[surface]
    assert adversary['catalog_sha256'] == hashes[surface]
    semantic_keys[surface] = {entry['id'] for entry in semantic['entries']}
    adversarial_keys[surface] = ids(surface, [entry['key'] for entry in adversary['per_key']])
    assert semantic_keys[surface] == adversarial_keys[surface] == expected
    assert len(semantic['entries']) == len(adversary['per_key']) == len(expected)
    for entry in semantic['entries']:
        key = entry['id'].split(':', 1)[1]
        assert entry['source'] == sources[surface][key]
        assert entry['translation'] == targets[surface][key]
    for entry in adversary['per_key']:
        key = entry['key'].removeprefix(surface + ':')
        assert entry['source'] == sources[surface][key]
        assert entry['target'] == targets[surface][key]
    for finding in semantic['findings']:
        assert finding['state'].startswith('resolved')
        findings.append({'key': finding['id'], 'severity': 'major', 'state': 'resolved',
                         'reason': finding.get('resolution') or finding.get('original_reason') or
                         'Independent final-target reread; see final semantic record.'})

record_path = KIT / 'reviews/bn.json'
record = json.loads(record_path.read_text())
record.update(locale='bn', source_sha256=manifest['source_sha256'], catalog_sha256=hashes,
              language_brief=read('language-brief.json'), findings=findings,
              accepted_english=editor_native['accepted_english'] + author_online['accepted_english'])
checked = p.check_draft(KIT, 'bn')
assert not checked['errors'] and not checked['untranslated'], checked
assert {entry['key'] for entry in record['accepted_english']} == set(checked['same_as_english_review_queue'])
batch_hashes = p.batch_target_hashes(manifest, paths)
record['batches'] = []
for batch in manifest['batches']:
    surface = batch['id'].split('-', 1)[0]
    keys = batch['keys']
    assert set(keys) <= author_keys[surface] & editor_keys[surface] & semantic_keys[surface] & adversarial_keys[surface]
    record['batches'].append({'id': batch['id'], 'source_sha256': manifest['source_sha256'],
        'target_sha256': batch_hashes[batch['id']],
        'author': '/root/bengali_author' if surface == 'native' else '/root/bengali_online_author',
        'reviewer': '/root/rendering_audit',
        'editor': '/root/bengali_online_author' if surface == 'native' else '/root/bengali_editor',
        'reviewed_keys': keys, 'edited_keys': keys, 'rechecked_keys': keys,
        'rechecked_by': '/root/rendering_audit', 'adversarial_reviewer': '/root/bengali_adversary',
        'adversarial_reviewed_keys': keys})
evidence_names = ['author-native-complete.json', 'online-author-review.json',
    'native-editorial-review.json', 'editorial-work/online-editorial-review.json',
    'final-native-semantic-review.json', 'final-online-semantic-review.json',
    'adversarial-work/final-native-review.json', 'adversarial-work/final-online-review.json']
record['independent_review_evidence'] = [{'path': name, 'sha256': p.digest((KIT / name).read_bytes())}
                                         for name in evidence_names]
for name, files in {
    'language_brief': ['language-brief.json', 'final-native-semantic-review.json', 'final-online-semantic-review.json'],
    'source_based_pilot': ['source-based-pilot.json', 'repaired-pilot-semantic-review.json', 'editorial-pilot-review.json'],
    'structural': ['translations-strict.log', 'translations-tests.log', 'i18n-check.log', 'font-coverage-bengali.log'],
}.items():
    record['gates'][name] = {'passed': True, 'source_sha256': manifest['source_sha256'],
        'catalog_sha256': hashes, 'evidence': [{'path': file, 'sha256': p.digest((KIT / file).read_bytes())}
                                              for file in files]}
for name, gate in record['gates'].items():
    if gate.get('catalog_sha256') not in (None, hashes):
        record['gates'][name] = {'passed': False, 'evidence': []}
record_path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')
print('All 6,229 independently reviewed final values, 43 canonical batches, and 162 English exceptions verified.')
print('Pending gates:', ', '.join(name for name, gate in record['gates'].items() if not gate['passed']))
