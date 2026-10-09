import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'mobile'))
import ios_release
import ios_store_release as store


def provenance():
    return dict(schemaVersion=1,tag='v0.11.0.0',sourceCommit='a'*40,runId='123',bundleId=store.BUNDLE,
        appStoreEligible=True,buildNumber='1001234',marketingVersion='0.11.0',archiveBinarySha256='b'*64)


class FakeAPI:
    def __init__(self, state='PREPARE_FOR_SUBMISSION', missing=False, attached='build-1'):
        self.calls=[];self.missing=missing;self.attached=attached
        self.app=dict(id='app-1',attributes={'primaryLocale':'en-US'})
        self.build=dict(id='build-1',attributes={'processingState':'VALID','buildAudienceType':'APP_STORE_ELIGIBLE','usesNonExemptEncryption':False})
        self.version=dict(id='version-1',attributes={'appVersionState':state,'releaseType':'MANUAL','copyright':'Globulation 2'})
    def collect(self,path):
        self.calls.append(('GET',path,None))
        if path.startswith('/v1/apps?'): return [self.app]
        if path.startswith('/v1/builds?'): return [self.build]
        if '/appStoreVersions?' in path: return [] if self.missing else [self.version]
        if '/reviewSubmissions?' in path: return []
        raise AssertionError(path)
    def request(self, method, path, body=None):
        self.calls.append((method,path,body))
        if path.endswith('/relationships/build'): return {'data':None if self.attached is None else {'type':'builds','id':self.attached}}
        if path=='/v1/appStoreVersions': return {'data':self.version}
        if path=='/v1/reviewSubmissions': return {'data':{'id':'review-1','attributes':{'state':'READY_FOR_REVIEW'}}}
        return {}


class MetadataAPI(FakeAPI):
    def __init__(self, missing=None):
        super().__init__();self.missing=missing
    def collect(self,path):
        if path.endswith('/appStoreVersionLocalizations'):
            return [{'id':'locale-1','attributes':{'locale':'en-US','description':'Verified game','supportUrl':'https://example.com/support'}}]
        if path.endswith('/appScreenshotSets'):
            return [{'id':'phone','attributes':{'screenshotDisplayType':'APP_IPHONE_67'}},
                    {'id':'pad','attributes':{'screenshotDisplayType':'APP_IPAD_PRO_3GEN_129'}}]
        if path.endswith('/appScreenshots'):
            if self.missing=='screenshots' and '/pad/' in path:return []
            return [{'id':'image-1','attributes':{'assetDeliveryState':{'state':'COMPLETE'}}}]
        if path.endswith('/appInfos'):return [{'id':'info-1','attributes':{'state':'PREPARE_FOR_SUBMISSION'}}]
        if path.endswith('/appInfoLocalizations'):
            return [{'id':'info-locale','attributes':{'locale':'en-US','privacyPolicyUrl':None if self.missing=='privacy' else 'https://example.com/privacy'}}]
        return super().collect(path)
    def request(self,method,path,body=None):
        if path.endswith('/appStoreReviewDetail'):
            attrs={'contactFirstName':'Reviewer','contactLastName':'Contact','contactPhone':'555','contactEmail':'review@example.com','demoAccountRequired':False}
            if self.missing=='contact':attrs.pop('contactEmail')
            return {'data':{'attributes':attrs}}
        return super().request(method,path,body)


class IOSStoreTests(unittest.TestCase):
    def test_version_mapping_preserves_fourth_component_without_collisions(self):
        self.assertEqual(ios_release.marketing_version('0.11.0.0'),'0.11.0')
        self.assertEqual(ios_release.marketing_version('0.11.1.23'),'0.11.123')
        self.assertEqual(ios_release.marketing_version('0.11.2.0'),'0.11.200')
        for version in ('0.11.1.100','0.11.0','0.11.01.0','0.11.0.0.0'):
            with self.assertRaises(ValueError): ios_release.marketing_version(version)

    def test_provenance_requires_exact_public_source_and_eligible_successful_upload(self):
        record=provenance();store.validate_provenance(record,'v0.11.0.0','a'*40,'123')
        for key,value in [('sourceCommit','c'*40),('tag','v0.11.1.0'),('appStoreEligible',False),('runId','124'),('marketingVersion','0.9.5'),('bundleId','other')]:
            modified={**record,key:value}
            with self.subTest(key=key),self.assertRaises(ValueError): store.validate_provenance(modified,'v0.11.0.0','a'*40,'123')

    def test_prepare_creates_manual_version_with_exact_uploaded_build_without_submitting(self):
        api=FakeAPI(missing=True);store.run(api,'prepare',provenance())
        create=[body for method,path,body in api.calls if method=='POST' and path=='/v1/appStoreVersions'][0]
        self.assertEqual(create['data']['attributes']['releaseType'],'MANUAL')
        self.assertEqual(create['data']['relationships']['build']['data']['id'],'build-1')
        self.assertFalse(any('reviewSubmission' in path or 'ReleaseRequest' in path for _,path,_ in api.calls))

    def test_submit_checks_metadata_then_creates_review_without_publishing(self):
        api=FakeAPI()
        with patch.object(store,'check_metadata') as metadata: store.run(api,'submit',provenance())
        metadata.assert_called_once()
        self.assertIn(('PATCH','/v1/reviewSubmissions/review-1',store.resource('reviewSubmissions','review-1',{'submitted':True})),api.calls)
        self.assertFalse(any('ReleaseRequest' in path for _,path,_ in api.calls))

    def test_submission_retry_reuses_only_exact_review_item_relationship(self):
        class RetryAPI(MetadataAPI):
            def collect(self,path):
                if '/reviewSubmissions?' in path:
                    return [{'id':'review-1','attributes':{'state':'READY_FOR_REVIEW'}}]
                if '/reviewSubmissions/review-1/items' in path:
                    self.calls.append(('GET',path,None))
                    if 'include=appStoreVersion' not in path:raise AssertionError('Missing explicit review linkage include')
                    return [{'id':'item-1','relationships':{'appStoreVersion':{'data':{'type':'appStoreVersions','id':'version-1'}}}}]
                return super().collect(path)
        api=RetryAPI();store.run(api,'submit',provenance())
        self.assertFalse(any(method=='POST' for method,_,_ in api.calls))
        self.assertIn(('PATCH','/v1/reviewSubmissions/review-1',store.resource('reviewSubmissions','review-1',{'submitted':True})),api.calls)

    def test_missing_metadata_blocks_submission_writes(self):
        api=FakeAPI()
        with patch.object(store,'check_metadata',side_effect=ValueError('Missing screenshots')),self.assertRaises(ValueError): store.run(api,'submit',provenance())
        self.assertFalse(any(method!='GET' for method,_,_ in api.calls))

    def test_publish_requires_approved_manual_version_and_exact_build(self):
        for state,attached in [('IN_REVIEW','build-1'),('PENDING_DEVELOPER_RELEASE','different')]:
            api=FakeAPI(state=state,attached=attached)
            with self.assertRaises(ValueError): store.run(api,'publish',provenance())
            self.assertFalse(any(method!='GET' for method,_,_ in api.calls))
        api=FakeAPI(state='PENDING_DEVELOPER_RELEASE');store.run(api,'publish',provenance())
        releases=[body for method,path,body in api.calls if path=='/v1/appStoreVersionReleaseRequests']
        self.assertEqual(releases,[store.resource('appStoreVersionReleaseRequests',relationships={'appStoreVersion':store.relation('appStoreVersions','version-1')})])

    def test_concrete_listing_metadata_complete_and_missing_gates(self):
        complete=MetadataAPI();store.check_metadata(complete,complete.app,complete.version)
        for missing in ('screenshots','privacy','contact'):
            api=MetadataAPI(missing=missing)
            with self.subTest(missing=missing),self.assertRaises(ValueError):store.run(api,'submit',provenance())
            self.assertFalse(any(method!='GET' for method,_,_ in api.calls))

    def test_invalid_internal_or_unprocessed_apple_build_is_rejected_before_writes(self):
        for field,value in [('processingState','PROCESSING'),('buildAudienceType','INTERNAL_ONLY'),('usesNonExemptEncryption',None)]:
            api=FakeAPI();api.build['attributes'][field]=value
            with self.assertRaises(ValueError):store.run(api,'prepare',provenance())
            self.assertFalse(any(method!='GET' for method,_,_ in api.calls))

    def test_client_rejects_pagination_to_other_origin_before_sending_token(self):
        api=store.Client('secret-token')
        with self.assertRaisesRegex(ValueError,'outside Apple'): api.request('GET','https://example.com/data')


if __name__=='__main__':unittest.main()
