import copy
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'platforms/mobile'))
import apple_store_localize as tool


def item(kind, identity, **attrs):
    return dict(type=kind, id=identity, attributes=attrs)


def plan(platform='IOS'):
    result = dict(schemaVersion=1, bundleId=tool.BUNDLE, marketingRef='a'*40,
                  platform=platform, version='0.11.601' if platform == 'IOS' else '0.11.6',
                  buildNumber='1002501' if platform == 'IOS' else '2026111801', locales={})
    for canonical, wire in tool.LOCALES.items():
        fields = dict(promotionalText=wire+' promo', keywords='strategy,colony', description=wire+' description')
        if platform == 'IOS': fields['whatsNew'] = wire+' reviewed notes\n'
        result['locales'][wire] = dict(canonicalLocale=canonical, appInfo=dict(name='Globulation 2', subtitle=wire+' strategy'), version=fields)
    result['sourceSha256'] = tool.sha(result)
    return result


class FakeAPI:
    def __init__(self, selected=None, automatic=False):
        self.plan = selected or plan(); self.calls = []; self.automatic = automatic; self.counter = 0
        self.after_write = None; self.before_patch = None
        self.app = item('apps', 'app', bundleId=tool.BUNDLE, primaryLocale='en-US', sku='private-sku')
        self.versions = [item('appStoreVersions', 'target', platform=self.plan['platform'], versionString=self.plan['version'],
                              appVersionState='PREPARE_FOR_SUBMISSION', releaseType='MANUAL', copyright='Keep'),
                         item('appStoreVersions', 'other', platform='MAC_OS' if self.plan['platform']=='IOS' else 'IOS',
                              versionString='other-version', appVersionState='PREPARE_FOR_SUBMISSION', releaseType='AFTER_APPROVAL')]
        self.attached = {'type':'builds', 'id':'build'}
        self.build = item('builds', 'build', version=self.plan['buildNumber'], processingState='VALID', expired=False)
        self.prerelease = item('preReleaseVersions', 'prerelease', platform=self.plan['platform'], version=self.plan['version'])
        self.infos = [item('appInfos', 'draft', state='PREPARE_FOR_SUBMISSION'), item('appInfos', 'live', state='READY_FOR_DISTRIBUTION')]
        self.info_locales = {'draft':[], 'live':[]}; self.version_locales = {'target':[], 'other':[]}
        for parent in self.info_locales:
            for locale in ('en-US','fr-FR','en-GB'):
                self.info_locales[parent].append(item('appInfoLocalizations', parent+'-'+locale, locale=locale,
                    name='Old name', subtitle='Old subtitle', privacyPolicyUrl='https://example.com/privacy',
                    privacyChoicesUrl='https://example.com/account', privacyPolicyText=None))
        for parent in self.version_locales:
            for locale in ('en-US','en-GB'):
                self.version_locales[parent].append(item('appStoreVersionLocalizations', parent+'-'+locale, locale=locale,
                    promotionalText='Old promo', keywords='old', description='Old '+parent+' description',
                    whatsNew='Existing '+parent+' notes', supportUrl='https://example.com/'+parent+'/support', marketingUrl='https://example.com/home'))
        self.images = [item('appScreenshots','approved-image',fileName='approved.png',sourceFileChecksum='checksum',
                            imageAsset={'templateUrl':'https://images.example.com/approved'},assetDeliveryState={'state':'COMPLETE'})]

    def collect(self, path):
        self.calls.append(('GET',path,None))
        if path.startswith('/v1/apps?'): values = [self.app]
        elif path == '/v1/apps/app/appStoreVersions': values = self.versions
        elif path.startswith('/v1/builds?'): values = [self.build]
        elif path == '/v1/apps/app/appInfos': values = self.infos
        elif re.fullmatch(r'/v1/appInfos/[^/]+/appInfoLocalizations',path): values = self.info_locales[path.split('/')[3]]
        elif re.fullmatch(r'/v1/appStoreVersions/[^/]+/appStoreVersionLocalizations',path): values = self.version_locales[path.split('/')[3]]
        elif path.endswith('/appScreenshotSets'):
            identity = path.split('/')[3]
            values = [item('appScreenshotSets','primary-set',screenshotDisplayType='APP_IPHONE_67')] if identity=='target-en-US' else []
        elif path.endswith('/appScreenshots'): values = self.images
        elif path.endswith('/appPreviewSets'): values = []
        else: raise AssertionError(path)
        return copy.deepcopy(values)

    def request(self, method, path, body=None):
        self.calls.append((method,path,copy.deepcopy(body)))
        if method == 'GET':
            if path.endswith('/relationships/build'): return {'data':copy.deepcopy(self.attached)}
            if path.endswith('/preReleaseVersion'): return {'data':copy.deepcopy(self.prerelease)}
            kind, identity = path.split('/')[2:]
            if self.before_patch: self.before_patch(self)
            groups = self.info_locales if kind=='appInfoLocalizations' else self.version_locales
            return {'data':copy.deepcopy(next(record for group in groups.values() for record in group if record['id']==identity))}
        data = body['data']; kind = data['type']
        assert kind in ('appInfoLocalizations','appStoreVersionLocalizations')
        groups = self.info_locales if kind=='appInfoLocalizations' else self.version_locales
        if method == 'POST':
            relation = 'appInfo' if kind=='appInfoLocalizations' else 'appStoreVersion'
            parent = data['relationships'][relation]['data']['id'];self.counter += 1
            defaults = dict(privacyPolicyText=None,privacyChoicesUrl=None) if kind=='appInfoLocalizations' else dict(whatsNew=None,marketingUrl=None)
            record = item(kind,'created-'+str(self.counter),**dict(defaults,**data['attributes']));groups[parent].append(record)
            if kind=='appInfoLocalizations' and self.automatic:
                locale = data['attributes']['locale']
                for parent, records in self.version_locales.items():
                    if not any(r['attributes']['locale']==locale for r in records):
                        attrs = copy.deepcopy(records[0]['attributes']);attrs['locale'] = locale
                        records.append(item('appStoreVersionLocalizations','auto-'+parent+'-'+locale,**attrs))
        elif method == 'PATCH':
            record = next(r for group in groups.values() for r in group if r['id']==data['id'])
            record['attributes'].update(data['attributes'])
        else: raise AssertionError(method)
        if self.after_write:self.after_write(self)
        return {'data':copy.deepcopy(record)}

    def writes(self):return [call for call in self.calls if call[0]!='GET']


class AppleLocalizationTests(unittest.TestCase):
    def inspect_apply(self, api, selected=None):
        selected = selected or api.plan
        baseline = tool.run(api,selected,'inspect')['baselineSha256']
        return tool.run(api,selected,'apply',baseline)

    def test_inspect_is_read_only_and_reports_only_safe_counts_and_digests(self):
        api = FakeAPI();result = tool.run(api,api.plan,'inspect')
        self.assertEqual(result['targetLocales'],38);self.assertFalse(api.writes())
        text = json.dumps(result)
        for private in ('private-sku','Old name','example.com','approved.png'):self.assertNotIn(private,text)
        self.assertFalse(any('/appStoreReview' in path or '/reviewSubmissions' in path for _,path,_ in api.calls))

    def test_stale_baseline_or_changed_source_refuses_every_write(self):
        for change in ('private','source'):
            api = FakeAPI();baseline = tool.run(api,api.plan,'inspect')['baselineSha256']
            if change=='private':api.info_locales['live'][0]['attributes']['subtitle']='Concurrent change'
            else:
                api.plan['marketingRef']='b'*40;api.plan['sourceSha256']=tool.sha({k:v for k,v in api.plan.items() if k!='sourceSha256'})
            with self.subTest(change=change),self.assertRaisesRegex(ValueError,'baseline'):tool.run(api,api.plan,'apply',baseline)
            self.assertFalse(api.writes())

    def test_withdrawn_version_can_be_localized_without_changing_review_or_build(self):
        api = FakeAPI()
        api.versions[0]['attributes']['appVersionState'] = 'DEVELOPER_REJECTED'
        api.infos[0]['attributes']['state'] = 'DEVELOPER_REJECTED'
        original_versions = copy.deepcopy(api.versions)
        original_build = copy.deepcopy(api.attached)
        result = self.inspect_apply(api)
        self.assertTrue(result['verified'])
        self.assertEqual(api.versions, original_versions)
        self.assertEqual(api.attached, original_build)
        self.assertFalse(any('/reviewSubmissions' in path for _, path, _ in api.calls))

    def test_review_and_distribution_states_refuse_every_write(self):
        for state in ('WAITING_FOR_REVIEW', 'IN_REVIEW', 'READY_FOR_DISTRIBUTION',
                      'PENDING_DEVELOPER_RELEASE', 'PENDING_APPLE_RELEASE', 'READY_FOR_REVIEW'):
            api = FakeAPI()
            api.versions[0]['attributes']['appVersionState'] = state
            with self.subTest(state=state), self.assertRaisesRegex(ValueError, 'editable draft'):
                tool.run(api, api.plan, 'inspect')
            self.assertFalse(api.writes())

    def test_wrong_state_build_platform_and_ambiguous_info_refuse_writes(self):
        for failure in ('review','build','platform','ambiguous','live-only','bundle'):
            api=FakeAPI()
            if failure=='review':api.versions[0]['attributes']['appVersionState']='WAITING_FOR_REVIEW'
            if failure=='build':api.build['attributes']['version']='wrong'
            if failure=='platform':api.prerelease['attributes']['platform']='MAC_OS'
            if failure=='ambiguous':api.infos.append(item('appInfos','second',state='PREPARE_FOR_SUBMISSION'))
            if failure=='live-only':api.infos=api.infos[1:]
            if failure=='bundle':api.app['attributes']['bundleId']='unrelated.app'
            with self.subTest(failure=failure),self.assertRaises(ValueError):tool.run(api,api.plan,'inspect')
            self.assertFalse(api.writes())

    def test_all_writes_are_scoped_and_new_urls_inherit_current_english(self):
        api=FakeAPI();original=copy.deepcopy(api);result=self.inspect_apply(api)
        self.assertTrue(result['verified']);self.assertEqual(result['verifiedLocales'],38)
        for method,path,body in api.writes():
            data=body['data'];kind=data['type'];keys=set(data['attributes'])
            allowed=set(tool.INFO_FIELDS) if kind=='appInfoLocalizations' else set(tool.VERSION_FIELDS)|{'whatsNew'}
            if method=='POST':allowed|={'locale'}|set(tool.INFO_URLS if kind=='appInfoLocalizations' else tool.VERSION_URLS)
            self.assertLessEqual(keys,allowed)
            self.assertEqual(path.split('/')[2],kind)
        self.assertEqual(api.versions,original.versions);self.assertEqual(api.attached,original.attached)
        self.assertEqual(api.info_locales['live'],original.info_locales['live'])
        self.assertEqual(api.version_locales['other'],original.version_locales['other'])
        self.assertEqual(api.images,original.images)
        info=tool.locale_map(api.info_locales['draft'])['ar-SA']['attributes']
        self.assertEqual(info['privacyPolicyUrl'],'https://example.com/privacy')
        listing=tool.locale_map(api.version_locales['target'])['ar-SA']['attributes']
        self.assertEqual(listing['supportUrl'],'https://example.com/target/support')
        self.assertEqual(listing['marketingUrl'],'https://example.com/home')
        self.assertEqual(tool.locale_map(api.version_locales['target'])['en-GB'],tool.locale_map(original.version_locales['target'])['en-GB'])

    def test_completed_retry_is_idempotent_with_new_inspection(self):
        api=FakeAPI();self.inspect_apply(api);api.calls=[]
        result=self.inspect_apply(api)
        self.assertEqual(result['pendingWrites'],0);self.assertFalse(api.writes())

    def test_existing_empty_support_urls_are_filled_without_touching_other_urls(self):
        api = FakeAPI(plan('MAC_OS'))
        self.inspect_apply(api)
        records = tool.locale_map(api.version_locales['target'])
        records['ca']['attributes']['supportUrl'] = None
        records['ru']['attributes']['supportUrl'] = ''
        records['th']['attributes']['supportUrl'] = 'https://example.com/custom-support'
        records['ca']['attributes']['marketingUrl'] = None
        original_other = copy.deepcopy(api.version_locales['other'])
        original_info = copy.deepcopy(api.info_locales)
        api.calls = []
        result = self.inspect_apply(api)
        self.assertEqual(result['pendingWrites'], 2)
        for method, path, body in api.writes():
            self.assertEqual(method, 'PATCH')
            self.assertEqual(body['data']['attributes'], {'supportUrl': 'https://example.com/target/support'})
            self.assertIn(body['data']['id'], (records['ca']['id'], records['ru']['id']))
        records = tool.locale_map(api.version_locales['target'])
        self.assertIsNone(records['ca']['attributes']['marketingUrl'])
        self.assertEqual(records['th']['attributes']['supportUrl'], 'https://example.com/custom-support')
        self.assertEqual(api.version_locales['other'], original_other)
        self.assertEqual(api.info_locales, original_info)

    def test_missing_primary_support_url_refuses_every_write(self):
        api = FakeAPI()
        api.version_locales['target'][0]['attributes']['supportUrl'] = None
        with self.assertRaisesRegex(ValueError, 'inheritance URL'):
            tool.run(api, api.plan, 'inspect')
        self.assertFalse(api.writes())

    def test_pending_shared_info_is_read_only_when_all_reviewed_text_matches(self):
        for state in ('WAITING_FOR_REVIEW', 'IN_REVIEW'):
            api = FakeAPI(plan('MAC_OS'))
            self.inspect_apply(api)
            api.infos[0]['attributes']['state'] = state
            tool.locale_map(api.version_locales['target'])['ca']['attributes']['supportUrl'] = None
            original_info = copy.deepcopy(api.info_locales)
            api.calls = []
            result = self.inspect_apply(api)
            self.assertEqual(result['pendingWrites'], 1)
            self.assertEqual(api.info_locales, original_info)
            self.assertTrue(all('/appStoreVersionLocalizations/' in path for _, path, _ in api.writes()))

    def test_pending_shared_info_with_any_untranslated_field_refuses_writes(self):
        api = FakeAPI(plan('MAC_OS'))
        self.inspect_apply(api)
        api.infos[0]['attributes']['state'] = 'WAITING_FOR_REVIEW'
        api.info_locales['draft'][1]['attributes']['subtitle'] = 'Changed in review'
        api.calls = []
        with self.assertRaisesRegex(ValueError, 'shared AppInfo'):
            tool.run(api, api.plan, 'inspect')
        self.assertFalse(api.writes())

    def test_explained_server_auto_creation_is_updated_without_duplicate_posts(self):
        api=FakeAPI(automatic=True);self.inspect_apply(api)
        posts=[body for method,path,body in api.writes() if method=='POST' and path=='/v1/appStoreVersionLocalizations']
        self.assertEqual([body['data']['attributes']['locale'] for body in posts],['fr-FR'])
        self.assertEqual(len(tool.locale_map(api.version_locales['other'])),38)

    def test_unexplained_auto_created_content_is_not_overwritten(self):
        api=FakeAPI(automatic=True)
        def surprise(value):
            for record in value.version_locales['target']:
                if record['id'].startswith('auto-'):record['attributes']['description']='Unexplained other editor text'
        api.after_write=surprise
        with self.assertRaisesRegex(ValueError,'automatically created'):self.inspect_apply(api)
        self.assertFalse(any(method=='PATCH' and '/auto-' in path for method,path,_ in api.writes()))

    def test_new_auto_created_empty_urls_receive_english_inheritance(self):
        api=FakeAPI(automatic=True)
        def clear_once(value):
            for record in value.version_locales['target']:
                if record['id'].startswith('auto-'):
                    record['attributes']['supportUrl']=None;record['attributes']['marketingUrl']=None
            value.after_write=None
        api.after_write=clear_once
        self.inspect_apply(api)
        attrs=tool.locale_map(api.version_locales['target'])['ar-SA']['attributes']
        self.assertEqual(attrs['supportUrl'],'https://example.com/target/support')
        self.assertEqual(attrs['marketingUrl'],'https://example.com/home')

    def test_existing_cell_changed_after_baseline_is_not_overwritten(self):
        api=FakeAPI();api.before_patch=lambda value:value.info_locales['draft'][0]['attributes'].update(subtitle='Concurrent')
        with self.assertRaisesRegex(ValueError,'changed after baseline'):self.inspect_apply(api)
        self.assertFalse(any(method=='PATCH' for method,_,_ in api.writes()))

    def test_existing_cell_already_saved_exactly_can_be_skipped(self):
        api=FakeAPI()
        desired=api.plan['locales']['en-US']['appInfo']
        def already_saved(value):
            value.info_locales['draft'][0]['attributes'].update(desired)
        api.before_patch=already_saved
        self.inspect_apply(api)
        self.assertFalse(any(method=='PATCH' and path=='/v1/appInfoLocalizations/draft-en-US' for method,path,_ in api.writes()))

    def test_prepare_refuses_modified_and_untracked_translation_files_before_export(self):
        for status in (' M translations/fr.md\n','?? translations/unreviewed.json\n'):
            with self.subTest(status=status),patch.object(tool.subprocess,'check_output',side_effect=['a'*40+'\n',status]):
                with self.assertRaisesRegex(ValueError,'dirty or untracked'):
                    tool.prepare(Path('/unused'), 'a'*40, 'IOS', '0.11.601', '1002501')

    def test_prepare_rejects_unaccepted_or_stale_release_notes_before_export(self):
        for failure in ('translation-hash','source-hash','same-reviewer','source-commit','missing-locale'):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);translations=root/'translations'
                (translations/'tools').mkdir(parents=True);(translations/'releases/reviews').mkdir(parents=True)
                (translations/'tools/storefront.py').write_text('def export(*args, **kwargs):\n    raise AssertionError("invalid notes reached export")\n')
                (translations/'platforms.json').write_text(json.dumps({'platforms':{'ios':{'locales':tool.LOCALES}}}))
                notes=dict(schemaVersion=1,platform='ios',version='0.11.601',status='reviewed',translator='author',
                           sourceCommit='d'*40,gameTag='v0.11.6.1',englishSource='notes\n',translations={locale:'notes\n' for locale in tool.LOCALES})
                review={key:notes[key] for key in ('schemaVersion','platform','version','status','translator','sourceCommit','gameTag')}
                review.update(reviewer='independent',issues=[],sourceSha256=tool.sha('notes\n'),translationSha256={locale:tool.sha('notes\n') for locale in tool.LOCALES})
                if failure=='translation-hash':review['translationSha256']['fr']='0'*64
                if failure=='source-hash':review['sourceSha256']='0'*64
                if failure=='same-reviewer':review['reviewer']='author'
                if failure=='source-commit':review['sourceCommit']='e'*40
                if failure=='missing-locale':notes['translations'].pop('fr')
                (translations/'releases/ios-0.11.601.json').write_text(json.dumps(notes))
                (translations/'releases/reviews/ios-0.11.601.json').write_text(json.dumps(review))
                with patch.object(tool.subprocess,'check_output',side_effect=['a'*40+'\n','']):
                    with self.assertRaises(ValueError):tool.prepare(root,'a'*40,'IOS','0.11.601','1002501')

    def test_main_reports_safe_policy_reasons_and_sanitized_apple_codes(self):
        for error,expected in ((tool.ValidationError('Stale or missing baseline; inspect again'), 'Stale or missing baseline'),
                (ValueError('App Store API rejected PATCH /v1/appInfoLocalizations/private-id: HTTP 409 (ENTITY_ERROR.ATTRIBUTE.INVALID at /data/attributes/name).'), 'ENTITY_ERROR.ATTRIBUTE.INVALID')):
            output=io.StringIO()
            with patch.object(sys,'argv',['apple_store_localize.py','prepare','--plan','/unused']), \
                    patch.object(tool,'require',side_effect=error),patch.object(sys,'stderr',output):
                self.assertEqual(tool.main(),1)
            self.assertIn(expected,output.getvalue());self.assertNotIn('private-id',output.getvalue())

    def test_full_readback_detects_media_url_and_unrelated_locale_drift(self):
        for failure in ('media','url','other-locale','allowed-text'):
            api=FakeAPI()
            def drift(value):
                if failure=='media':value.images[0]['attributes']['fileName']='unexpected.png'
                if failure=='url':value.info_locales['live'][0]['attributes']['privacyPolicyUrl']='https://changed.example'
                if failure=='other-locale':value.version_locales['other'][0]['attributes']['description']='Changed'
                if failure=='allowed-text':value.info_locales['draft'][0]['attributes']['name']='Not saved'
            api.after_write=drift
            with self.subTest(failure=failure),self.assertRaises(ValueError):self.inspect_apply(api)

    def test_mac_omits_whats_new_and_preserves_existing_notes(self):
        api=FakeAPI(plan('MAC_OS'));original=api.version_locales['target'][0]['attributes']['whatsNew'];self.inspect_apply(api)
        self.assertEqual(api.version_locales['target'][0]['attributes']['whatsNew'],original)
        self.assertTrue(all('whatsNew' not in body['data']['attributes'] for _,_,body in api.writes()))

    def test_byte_limits_mapping_and_digest_are_enforced(self):
        for change in ('bytes','mapping','digest','scope'):
            selected=plan()
            if change=='bytes':selected['locales']['fr-FR']['version']['keywords']='é'*51
            if change=='mapping':selected['locales']['fr-FR']['canonicalLocale']='en'
            if change=='scope':selected['locales']['fr-FR']['version']['releaseType']='AFTER_APPROVAL'
            if change!='digest':selected['sourceSha256']=tool.sha({k:v for k,v in selected.items() if k!='sourceSha256'})
            else:selected['sourceSha256']='0'*64
            with self.subTest(change=change),self.assertRaises(ValueError):tool.validate_plan(selected)

    def test_jwt_refresh_and_existing_client_off_host_guard(self):
        api=tool.RefreshingClient(b'key','id','issuer')
        with patch.object(tool,'jwt_token',side_effect=['first','second']) as token,patch.object(tool.Client,'request',return_value={}),patch.object(tool.time,'time',side_effect=[1000,1000,1100,1600,1600]):
            api.request('GET','/v1/apps');api.request('GET','/v1/apps');api.request('GET','/v1/apps')
            self.assertEqual(token.call_count,2)
        with self.assertRaisesRegex(ValueError,'outside Apple'):tool.Client('fake').request('GET','https://unrelated.example/v1/apps')


if __name__=='__main__':unittest.main()
