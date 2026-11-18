#!/usr/bin/env python3
"""Review-bound Apple listing text only; never submit, release or alter media.

prepare runs without Apple credentials. inspect returns a private-state digest,
not a state dump. apply requires that digest and verifies a full saved readback.
"""
import argparse
import base64
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from urllib.parse import urlencode

from ios_store_release import BUNDLE, Client, jwt_token, one, relation, resource, state

# Canonical-to-Apple identifiers independently checked in the current consoles.
LOCALES = dict(zip(
    'ar pt-BR ca cs de da en es fr el hu it nl pl pt-PT ro ru sk fi sv tr zh-Hant zh-Hans uk id vi ja ko sl hi ur bn nb ms mr ta te th'.split(),
    'ar-SA pt-BR ca cs de-DE da en-US es-ES fr-FR el hu it nl-NL pl pt-PT ro ru sk fi sv tr zh-Hant zh-Hans uk id vi ja ko sl-SI hi ur-PK bn-BD no ms mr-IN ta-IN te-IN th'.split()))
INFO_FIELDS = {'name': 30, 'subtitle': 30}
VERSION_FIELDS = {'promotionalText': 170, 'keywords': 100, 'description': 4000}
INFO_URLS = ('privacyPolicyUrl', 'privacyChoicesUrl')
VERSION_URLS = ('supportUrl', 'marketingUrl')
INFO_EDITABLE = {'PREPARE_FOR_SUBMISSION', 'READY_FOR_REVIEW', 'REJECTED', 'METADATA_REJECTED', 'DEVELOPER_REJECTED'}


def sha(value):
    raw = value.encode() if isinstance(value, str) else json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
    return hashlib.sha256(raw).hexdigest()


class ValidationError(ValueError):
    """A static, safe policy explanation that contains no server field values."""


def require(condition, message):
    if not condition:
        raise ValidationError(message)


def validate_plan(plan):
    require(plan.get('schemaVersion') == 1 and plan.get('bundleId') == BUNDLE, 'Invalid localization plan identity')
    require(plan.get('platform') in ('IOS', 'MAC_OS') and re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?', plan.get('version', ''))
            and re.fullmatch(r'[0-9]+', plan.get('buildNumber', '')), 'Explicit platform, version and build are required')
    require(re.fullmatch(r'[a-f0-9]{40}', plan.get('marketingRef', '')), 'Marketing source must be a full immutable commit')
    require(set(plan.get('locales', {})) == set(LOCALES.values()), 'Plan must contain exactly all 38 canonical Apple locales')
    for wire, item in plan['locales'].items():
        require(LOCALES.get(item.get('canonicalLocale')) == wire, 'Unexpected canonical Apple locale mapping')
        require(set(item.get('appInfo', {})) == set(INFO_FIELDS), 'Unscoped AppInfo attributes in plan')
        limits = dict(VERSION_FIELDS)
        if plan['platform'] == 'IOS':
            limits['whatsNew'] = 4000
        require(set(item.get('version', {})) == set(limits), 'Unscoped version attributes in plan')
        for fields, specs in ((item['appInfo'], INFO_FIELDS), (item['version'], limits)):
            for key, limit in specs.items():
                value = fields[key]
                require(isinstance(value, str) and bool(value) and '\0' not in value, 'Empty or invalid localized text')
                require((len(value.encode()) if key == 'keywords' else len(value)) <= limit, 'Localized field exceeds Apple character/UTF-8-byte limit')
    require(plan.get('sourceSha256') == sha({k: v for k, v in plan.items() if k != 'sourceSha256'}), 'Localization plan digest is stale')
    return plan


def prepare(root, marketing_ref, platform, version, build_number):
    """Consume the existing strict exporter and the separately reviewed notes."""
    require(re.fullmatch(r'[a-f0-9]{40}', marketing_ref), 'Marketing source must be a full immutable commit')
    require(platform in ('IOS', 'MAC_OS') and re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?', version)
            and re.fullmatch(r'[0-9]+', build_number), 'Explicit platform, version and build are required')
    actual = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    require(actual == marketing_ref, 'Marketing checkout differs from its pinned commit')
    dirty = subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain', '--untracked-files=all', '--', 'translations'], text=True)
    require(not dirty, 'Marketing translations are dirty or untracked; use the exact clean pinned source')
    translations = root / 'translations'
    spec = importlib.util.spec_from_file_location('canonical_storefront', translations / 'tools/storefront.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    config = json.loads((translations / 'platforms.json').read_text())
    key = 'ios' if platform == 'IOS' else 'mac'
    require(config['platforms'][key]['locales'] == LOCALES, 'Canonical Apple mapping changed; independently verify it first')
    notes = None
    if platform == 'IOS':
        path = translations / 'releases' / f'ios-{version}.json'
        notes = json.loads(path.read_text())
        review = json.loads((path.parent / 'reviews' / path.name).read_text())
        for name, expected in (('schemaVersion', 1), ('platform', 'ios'), ('version', version), ('status', 'reviewed')):
            require(notes.get(name) == review.get(name) == expected, 'Release-note identity/review is invalid')
        require(re.fullmatch(r'[a-f0-9]{40}', notes.get('sourceCommit', '')) and re.fullmatch(r'v[0-9.]+', notes.get('gameTag', '')), 'Release-note source identity missing')
        require(all(notes.get(k) == review.get(k) for k in ('sourceCommit', 'gameTag', 'translator'))
                and isinstance(notes.get('translator'), str) and notes['translator'].strip()
                and review.get('reviewer') and review['reviewer'] != notes.get('translator') and review.get('issues') == [], 'Independent release review is missing')
        require(set(notes['translations']) == set(LOCALES) and set(review['translationSha256']) == set(LOCALES), 'Release-note locale coverage differs')
        require(notes['englishSource'] == notes['translations']['en'] and sha(notes['englishSource']) == review['sourceSha256'], 'Release English hash differs')
        require(all(sha(text) == review['translationSha256'][locale] for locale, text in notes['translations'].items()), 'Release translation hashes differ')
    plan = dict(schemaVersion=1, bundleId=BUNDLE, marketingRef=marketing_ref, platform=platform,
                version=version, buildNumber=build_number, locales={})
    if notes:
        plan['releaseSourceCommit'] = notes['sourceCommit']
        plan['releaseGameTag'] = notes['gameTag']
    with tempfile.TemporaryDirectory(prefix='apple-reviewed-export-') as directory:
        module.export(translations, Path(directory), require_review=True)
        for canonical, wire in LOCALES.items():
            fields = json.loads((Path(directory) / key / wire / 'listing.json').read_text())['fields']
            desired = {name: fields[name] for name in VERSION_FIELDS}
            if notes:
                desired['whatsNew'] = notes['translations'][canonical]
            plan['locales'][wire] = dict(canonicalLocale=canonical, appInfo={name: fields[name] for name in INFO_FIELDS}, version=desired)
    plan['sourceSha256'] = sha(plan)
    return validate_plan(plan)


def compact(item):
    require(isinstance(item, dict) and re.fullmatch(r'[A-Za-z0-9-]{1,128}', str(item.get('id', ''))), 'Unexpected Apple resource identity')
    return {key: copy.deepcopy(item[key]) for key in ('type', 'id', 'attributes') if key in item}


def ordered(items):
    return sorted((compact(item) for item in items), key=lambda item: item['id'])


def locale_map(items):
    result = {}
    for item in items:
        locale = item['attributes']['locale']
        require(locale not in result, 'Duplicate Apple localization')
        result[locale] = item
    return result


def media(api, localization_id):
    result = {}
    for groups, assets in (('appScreenshotSets', 'appScreenshots'), ('appPreviewSets', 'appPreviews')):
        sets = []
        for group in ordered(api.collect(f'/v1/appStoreVersionLocalizations/{localization_id}/{groups}')):
            # Preserve asset IDs, delivery state, checksums and image references/order.
            values = [compact(item) for item in api.collect(f"/v1/{groups}/{group['id']}/{assets}")]
            for item in values:
                item.get('attributes', {}).pop('uploadOperations', None)  # Expiring upload credentials are not asset identity.
            sets.append(dict(group=group, assets=values))
        result[groups] = sets
    return result


def snapshot(api, plan):
    app = compact(one(api.collect('/v1/apps?' + urlencode({'filter[bundleId]': BUNDLE})), 'Globulation 2 app'))
    require(app['attributes'].get('bundleId') == BUNDLE and app['attributes'].get('primaryLocale') == 'en-US', 'Unexpected app or primary locale')
    versions = ordered(api.collect(f"/v1/apps/{app['id']}/appStoreVersions"))
    version = one([v for v in versions if v['attributes'].get('platform') == plan['platform'] and v['attributes'].get('versionString') == plan['version']], 'explicit platform/version draft')
    require(state(version) in {'PREPARE_FOR_SUBMISSION', 'DEVELOPER_REJECTED'}, 'Target must be an unsubmitted editable draft')
    attached = api.request('GET', f"/v1/appStoreVersions/{version['id']}/relationships/build").get('data')
    require(attached is not None, 'Target draft has no attached build')
    builds = api.collect('/v1/builds?' + urlencode({'filter[app]': app['id'], 'filter[version]': plan['buildNumber'],
                         'filter[preReleaseVersion.version]': plan['version'], 'filter[preReleaseVersion.platform]': plan['platform']}))
    build = compact(one(builds, 'explicit attached build'))
    require(build['id'] == attached.get('id') and build['attributes'].get('version') == plan['buildNumber']
            and build['attributes'].get('processingState') == 'VALID' and build['attributes'].get('expired') is False, 'Attached build identity or processing differs')
    prerelease = compact(api.request('GET', f"/v1/builds/{build['id']}/preReleaseVersion")['data'])
    require(prerelease['attributes'].get('platform') == plan['platform'] and prerelease['attributes'].get('version') == plan['version'], 'Attached build platform/version differs')
    infos = ordered(api.collect(f"/v1/apps/{app['id']}/appInfos"))
    candidates = [v for v in infos if (v['attributes'].get('state') or v['attributes'].get('appStoreState')) in INFO_EDITABLE]
    require(len(candidates) <= 1, 'Ambiguous editable shared AppInfo')
    info_locales = {v['id']: ordered(api.collect(f"/v1/appInfos/{v['id']}/appInfoLocalizations")) for v in infos}
    if not candidates:
        # Another platform may already have submitted the shared text. It may
        # be read for invariants only when every planned name/subtitle is exact;
        # operations() then emits no shared-info writes. Never select live info.
        for candidate in infos:
            if (candidate['attributes'].get('state') or candidate['attributes'].get('appStoreState')) not in {'WAITING_FOR_REVIEW', 'IN_REVIEW'}:
                continue
            records = locale_map(info_locales[candidate['id']])
            if all(locale in records and all(records[locale]['attributes'].get(k) == value
                   for k, value in wanted['appInfo'].items()) for locale, wanted in plan['locales'].items()):
                candidates.append(candidate)
    require(len(candidates) == 1, 'Exactly one editable or already-matching pending shared AppInfo is required; never the live record')
    info = candidates[0]
    version_locales, assets = {}, {}
    for v in versions:
        records = ordered(api.collect(f"/v1/appStoreVersions/{v['id']}/appStoreVersionLocalizations"))
        version_locales[v['id']] = records
        for record in records:
            assets[record['id']] = media(api, record['id'])
    require('en-US' in locale_map(info_locales[info['id']]) and 'en-US' in locale_map(version_locales[version['id']]), 'Current English listing is required for exact inheritance')
    return dict(app=app, versions=versions, versionId=version['id'], attachedBuild=attached, build=build,
                preReleaseVersion=prerelease, appInfos=infos, infoId=info['id'], infoLocalizations=info_locales,
                versionLocalizations=version_locales, media=assets)


def inherited_attributes(primary, keys, required):
    result = {}
    for key in keys:
        value = primary.get(key)
        require(value or key not in required, 'Required English inheritance URL is missing')
        require(not value or (isinstance(value, str) and value.startswith('https://')), 'English inheritance URL must be HTTPS')
        if value is not None:
            result[key] = value
    return result


def operations(before, plan):
    result = []
    for kind, parent, records, text_key, urls, required in (
            ('appInfoLocalizations', before['infoId'], before['infoLocalizations'][before['infoId']], 'appInfo', INFO_URLS, {'privacyPolicyUrl'}),
            ('appStoreVersionLocalizations', before['versionId'], before['versionLocalizations'][before['versionId']], 'version', VERSION_URLS, {'supportUrl'})):
        existing = locale_map(records)
        primary = existing['en-US']['attributes']
        inheritance = inherited_attributes(primary, urls, required)
        for locale, wanted in plan['locales'].items():
            attrs = wanted[text_key]
            item = existing.get(locale)
            if item:
                changed = {k: v for k, v in attrs.items() if item['attributes'].get(k) != v}
                # Shared AppInfo creation can leave platform locales with no
                # required support URL. Preserve populated URLs, fill only blanks.
                if kind == 'appStoreVersionLocalizations' and not item['attributes'].get('supportUrl'):
                    changed['supportUrl'] = inheritance['supportUrl']
                if changed:
                    result.append(dict(kind=kind, parent=parent, locale=locale, id=item['id'], attributes=changed,
                                       beforeAttributes=item['attributes']))
            else:
                result.append(dict(kind=kind, parent=parent, locale=locale, id=None,
                                   attributes=dict(locale=locale, **attrs, **inheritance)))
    return result


def inherited_or_target(item, primary, desired):
    """Allow only explained server-created defaults, never concurrent edits."""
    for key, value in item['attributes'].items():
        if key == 'locale':
            continue
        require(value in (None, '', primary.get(key), desired.get(key)), 'Unexpected content in an automatically created locale; inspect again')


def verify(before, after, plan):
    for key in ('app', 'versions', 'versionId', 'attachedBuild', 'build', 'preReleaseVersion', 'appInfos', 'infoId'):
        require(before[key] == after[key], 'App/build/release/shared-info invariant changed during localization')
    created = set()
    for section, selected, desired_key in (('infoLocalizations', before['infoId'], 'appInfo'), ('versionLocalizations', before['versionId'], 'version')):
        require(set(before[section]) == set(after[section]), 'Unrelated parent resource changed')
        for parent, old_records in before[section].items():
            old = locale_map(old_records); new = locale_map(after[section][parent])
            require(set(old) <= set(new), 'An existing localization disappeared')
            for locale, item in old.items():
                require(item['id'] == new[locale]['id'] and item['type'] == new[locale]['type'], 'Existing localization identity changed')
                attrs = dict(item['attributes'])
                if parent == selected and locale in plan['locales']:
                    attrs.update(plan['locales'][locale][desired_key])
                    if section == 'versionLocalizations' and not attrs.get('supportUrl'):
                        attrs['supportUrl'] = old['en-US']['attributes']['supportUrl']
                require(attrs == new[locale]['attributes'], 'Saved text or untouched localized fields differ')
            for locale in set(new) - set(old):
                require(locale in plan['locales'], 'Unexpected new locale')
                created.add(new[locale]['id'])
                primary = old['en-US']['attributes']
                if parent == selected:
                    wanted = plan['locales'][locale][desired_key]
                    require(all(new[locale]['attributes'].get(k) == v for k, v in wanted.items()), 'New locale saved text differs')
                    url_keys = INFO_URLS if section == 'infoLocalizations' else VERSION_URLS
                    for key in url_keys:
                        require(new[locale]['attributes'].get(key) == primary.get(key), 'New locale URLs differ from English inheritance')
                    inherited_or_target(new[locale], primary, wanted)
                else:
                    require(section == 'versionLocalizations', 'Unrelated shared AppInfo locale appeared')
                    inherited_or_target(new[locale], primary, {})
            if parent == selected:
                require(set(plan['locales']) <= set(new), 'Some canonical Apple locales were not saved')
    for identity, old_assets in before['media'].items():
        require(after['media'].get(identity) == old_assets, 'Existing screenshot/preview assets or order changed')
    for identity in set(after['media']) - set(before['media']):
        require(identity in created and all(not groups for groups in after['media'][identity].values()), 'New localization acquired unexpected media assets')


def run(api, plan, stage, baseline=''):
    validate_plan(plan)
    before = snapshot(api, plan)
    baseline_hash = sha(dict(sourceSha256=plan['sourceSha256'], snapshot=before))
    actions = operations(before, plan)
    result = dict(stage=stage, sourceSha256=plan['sourceSha256'], baselineSha256=baseline_hash,
                  planSha256=sha(actions), targetLocales=len(plan['locales']), pendingWrites=len(actions),
                  creates=sum(a['id'] is None for a in actions), updates=sum(a['id'] is not None for a in actions),
                  componentSha256={key: sha(value) for key, value in before.items()})
    if stage == 'inspect':
        return result
    require(stage == 'apply' and re.fullmatch(r'[a-f0-9]{64}', baseline) and baseline == baseline_hash, 'Stale or missing baseline; inspect again before applying')
    for action in actions:
        kind = action['kind']; attrs = action['attributes']; identity = action['id']
        if identity is None:
            parent_kind = 'appInfos' if kind == 'appInfoLocalizations' else 'appStoreVersions'
            # Apple may auto-create a locale while its shared/platform counterpart is added.
            current = locale_map(api.collect(f"/v1/{parent_kind}/{action['parent']}/{kind}"))
            found = current.get(action['locale'])
            if found:
                desired = plan['locales'][action['locale']]['appInfo' if kind == 'appInfoLocalizations' else 'version']
                original_records = (before['infoLocalizations'] if kind == 'appInfoLocalizations' else before['versionLocalizations'])[action['parent']]
                inherited_or_target(found, locale_map(original_records)['en-US']['attributes'], desired)
                identity = found['id']
                # This record was absent at baseline: fill empty inherited URLs as
                # well as text, while never changing a baseline-existing URL.
                attrs = {k: v for k, v in attrs.items() if k != 'locale' and found['attributes'].get(k) != v}
            else:
                relationship = 'appInfo' if kind == 'appInfoLocalizations' else 'appStoreVersion'
                api.request('POST', f'/v1/{kind}', resource(kind, attributes=attrs,
                            relationships={relationship: relation(parent_kind, action['parent'])}))
                continue
        if attrs:
            if action['id'] is not None:
                current = api.request('GET', f'/v1/{kind}/{identity}')['data']
                if current.get('attributes') == dict(action['beforeAttributes'], **attrs):
                    continue  # A retry/other writer already saved exactly the reviewed fields.
                require(current.get('attributes') == action['beforeAttributes'], 'Localization changed after baseline; inspect again')
            api.request('PATCH', f'/v1/{kind}/{identity}', resource(kind, identity, attrs))
    after = snapshot(api, plan)
    verify(before, after, plan)
    require(not operations(after, plan), 'Final saved text is incomplete')
    return dict(result, verified=True, readbackSha256=sha(after), verifiedLocales=len(plan['locales']))


class RefreshingClient(Client):
    def __init__(self, key, key_id, issuer):
        super().__init__('')
        self.key, self.key_id, self.issuer, self.refreshed = key, key_id, issuer, 0

    def request(self, method, path, body=None):
        if time.time() - self.refreshed >= 450:
            self.token = jwt_token(self.key, self.key_id, self.issuer)
            self.refreshed = time.time()
        return super().request(method, path, body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('prepare', 'inspect', 'apply'))
    parser.add_argument('--marketing-root', type=Path)
    parser.add_argument('--marketing-ref')
    parser.add_argument('--platform', choices=('IOS', 'MAC_OS'))
    parser.add_argument('--version')
    parser.add_argument('--build-number')
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--baseline-sha256', default='')
    args = parser.parse_args()
    try:
        if args.stage == 'prepare':
            require(all((args.marketing_root, args.marketing_ref, args.platform, args.version, args.build_number)), 'Prepare requires explicit source and target inputs')
            plan = prepare(args.marketing_root, args.marketing_ref, args.platform, args.version, args.build_number)
            args.plan.write_text(json.dumps(plan, ensure_ascii=False, indent=2) + '\n')
            result = dict(stage='prepare', sourceSha256=plan['sourceSha256'], targetLocales=len(plan['locales']))
        else:
            plan = validate_plan(json.loads(args.plan.read_text()))
            api = RefreshingClient(base64.b64decode(os.environ['IOS_ASC_KEY_P8_BASE64'], validate=True),
                                   os.environ['IOS_ASC_KEY_ID'], os.environ['IOS_ASC_ISSUER_ID'])
            result = run(api, plan, args.stage, args.baseline_sha256)
        print(json.dumps(result, sort_keys=True))
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        # Source/catalog policy errors and HTTP failures must not dump private response values.
        message = 'Apple localization failed validation; inspect the scoped policy/source conditions before retrying.'
        if isinstance(error, ValidationError):
            message = str(error)
        elif isinstance(error, ValueError) and str(error).startswith('App Store API rejected '):
            message = re.sub(r'(/v1/[^/ ]+/)[A-Za-z0-9-]+', r'\1<id>', str(error))[:500]
        print(message, file=__import__('sys').stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
