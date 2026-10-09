#!/usr/bin/env python3
"""Prepare, submit or deliberately publish a provenance-bound iOS App Store build.

Endpoints and request shapes follow Apple's App Store Connect OpenAPI spec.
An upload never triggers public distribution. Apple's review/metadata errors
remain hard failures; no approval or source identity is inferred from Apple.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import time
from urllib.error import HTTPError
from urllib.parse import urlencode, urlsplit
from urllib.request import Request, urlopen
from ios_release import marketing_version

API='https://api.appstoreconnect.apple.com'
BUNDLE='org.globulation2.glob2'


def validate_provenance(record, tag, commit, run_id):
    if record.get('schemaVersion')!=1 or record.get('tag')!=tag or record.get('sourceCommit')!=commit:
        raise ValueError('Uploaded build provenance differs from the selected public source')
    if record.get('bundleId')!=BUNDLE or record.get('appStoreEligible') is not True or record.get('runId')!=str(run_id):
        raise ValueError('Provenance must describe an eligible upload from the selected mirror run')
    if record.get('marketingVersion')!=marketing_version(tag.removeprefix('v')):
        raise ValueError('Marketing version does not encode the selected game tag')
    if not re.fullmatch('[0-9]+',str(record.get('buildNumber',''))) or not re.fullmatch('[0-9a-f]{64}',str(record.get('archiveBinarySha256',''))):
        raise ValueError('Uploaded build identity is incomplete')
    return record


def jwt_token(key, key_id, issuer):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    now=int(time.time())
    def encode(value): return base64.urlsafe_b64encode(json.dumps(value,separators=(',',':')).encode()).rstrip(b'=').decode()
    payload=encode(dict(alg='ES256',kid=key_id,typ='JWT'))+'.'+encode(dict(iss=issuer,iat=now,exp=now+600,aud='appstoreconnect-v1'))
    private=serialization.load_pem_private_key(key,None)
    if not isinstance(private,ec.EllipticCurvePrivateKey) or not isinstance(private.curve,ec.SECP256R1):
        raise ValueError('App Store authentication requires its P-256 key')
    r,s=decode_dss_signature(private.sign(payload.encode(),ec.ECDSA(hashes.SHA256())))
    return payload+'.'+base64.urlsafe_b64encode(r.to_bytes(32,'big')+s.to_bytes(32,'big')).rstrip(b'=').decode()


class Client:
    def __init__(self, token): self.token=token
    def request(self, method, path, body=None):
        url=path if path.startswith('https://') else API+path
        if urlsplit(url).netloc!='api.appstoreconnect.apple.com' or urlsplit(url).scheme!='https':
            raise ValueError('Refusing an API pagination URL outside Apple')
        request=Request(url,data=None if body is None else json.dumps(body).encode(),method=method,
                        headers={'Authorization':'Bearer '+self.token,'Content-Type':'application/json'})
        try:
            with urlopen(request,timeout=60) as response:
                data=response.read(); return json.loads(data) if data else {}
        except HTTPError as error:
            # No token, private key, review contacts or server body is logged.
            error.close()
            raise ValueError(f'App Store API rejected {method} {urlsplit(url).path}: HTTP {error.code}. Resolve store metadata, agreements or review requirements before retrying.') from None
    def collect(self,path):
        result=[]; visited=set()
        while path:
            if path in visited or len(visited)>=100: raise ValueError('Invalid App Store pagination')
            visited.add(path);page=self.request('GET',path);result.extend(page['data']);path=page.get('links',{}).get('next')
        return result


def resource(kind, identity=None, attributes=None, relationships=None):
    data={'type':kind}
    if identity: data['id']=identity
    if attributes is not None: data['attributes']=attributes
    if relationships is not None: data['relationships']=relationships
    return {'data':data}


def relation(kind,identity): return {'data':{'type':kind,'id':identity}}


def one(values, description):
    if len(values)!=1: raise ValueError('Expected exactly one '+description)
    return values[0]


def state(version):
    attrs=version['attributes']; return attrs.get('appVersionState') or attrs.get('appStoreState')


def check_metadata(api, app, version):
    attrs=version['attributes']
    if not attrs.get('copyright'): raise ValueError('App Store copyright metadata is missing')
    locales=api.collect(f"/v1/appStoreVersions/{version['id']}/appStoreVersionLocalizations")
    if not locales: raise ValueError('App Store localized listing is missing')
    for locale in locales:
        info=locale['attributes']
        if not info.get('description') or not str(info.get('supportUrl','')).startswith('https://'):
            raise ValueError('Localized description and HTTPS support URL are required')
    # Require complete iPhone/iPad media for the primary listing. Apple validates
    # exact display sizes and any inherited locale media on actual submission.
    primary=one([item for item in locales if item['attributes'].get('locale')==app['attributes']['primaryLocale']], 'primary locale listing')
    screenshots=api.collect(f"/v1/appStoreVersionLocalizations/{primary['id']}/appScreenshotSets")
    devices=set()
    for group in screenshots:
        images=api.collect(f"/v1/appScreenshotSets/{group['id']}/appScreenshots")
        if not images: continue
        if any(image['attributes'].get('assetDeliveryState',{}).get('state')!='COMPLETE' for image in images):
            raise ValueError('App Store screenshot delivery is incomplete')
        display=group['attributes']['screenshotDisplayType']
        if display.startswith('APP_IPHONE_'): devices.add('iphone')
        if display.startswith('APP_IPAD_'): devices.add('ipad')
    if devices!={'iphone','ipad'}: raise ValueError('Complete iPhone and iPad screenshots are required')
    detail=api.request('GET',f"/v1/appStoreVersions/{version['id']}/appStoreReviewDetail")['data']['attributes']
    if any(not detail.get(name) for name in ('contactFirstName','contactLastName','contactPhone','contactEmail')):
        raise ValueError('App Review contact details are incomplete')
    if detail.get('demoAccountRequired') and any(not detail.get(name) for name in ('demoAccountName','demoAccountPassword')):
        raise ValueError('Required App Review demo credentials are missing')
    infos=api.collect(f"/v1/apps/{app['id']}/appInfos")
    privacy=[]
    for info in infos:
        if info['attributes'].get('state') not in ('PREPARE_FOR_SUBMISSION','READY_FOR_REVIEW','READY_FOR_DISTRIBUTION','ACCEPTED'): continue
        privacy.extend(api.collect(f"/v1/appInfos/{info['id']}/appInfoLocalizations"))
    if not any(item['attributes'].get('locale')==app['attributes']['primaryLocale'] and
               str(item['attributes'].get('privacyPolicyUrl','')).startswith('https://') for item in privacy):
        raise ValueError('Primary locale privacy policy URL is missing')


def run(api, stage, provenance):
    app=one(api.collect('/v1/apps?'+urlencode({'filter[bundleId]':BUNDLE})), 'Globulation 2 app')
    build=one(api.collect('/v1/builds?'+urlencode({'filter[app]':app['id'],'filter[version]':provenance['buildNumber'],
        'filter[preReleaseVersion.version]':provenance['marketingVersion'],'filter[preReleaseVersion.platform]':'IOS'})), 'uploaded tagged iOS build')
    if build['attributes'].get('processingState')!='VALID' or build['attributes'].get('expired') is True or build['attributes'].get('buildAudienceType')!='APP_STORE_ELIGIBLE':
        raise ValueError('Selected Apple build is not processed and App Store eligible')
    if build['attributes'].get('usesNonExemptEncryption') is not False:
        raise ValueError('Encryption declaration is unresolved or differs from the uploaded app')
    versions=api.collect(f"/v1/apps/{app['id']}/appStoreVersions?"+urlencode({'filter[platform]':'IOS','filter[versionString]':provenance['marketingVersion']}))
    if not versions:
        if stage!='prepare': raise ValueError('Prepare the exact tagged App Store version first')
        version=api.request('POST','/v1/appStoreVersions',resource('appStoreVersions',attributes={
            'platform':'IOS','versionString':provenance['marketingVersion'],'releaseType':'MANUAL'},
            relationships={'app':relation('apps',app['id']),'build':relation('builds',build['id'])}))['data']
        return {'stage':stage,'versionId':version['id'],'buildId':build['id'],'state':state(version)}
    version=one(versions,'matching iOS App Store version')
    attached=api.request('GET',f"/v1/appStoreVersions/{version['id']}/relationships/build").get('data')
    if attached and attached.get('id')!=build['id']: raise ValueError('App Store version is attached to a different build; refusing to replace it')
    if stage=='prepare':
        if state(version) not in ('PREPARE_FOR_SUBMISSION','READY_FOR_REVIEW','REJECTED','METADATA_REJECTED','DEVELOPER_REJECTED'):
            raise ValueError('App Store version is no longer editable')
        api.request('PATCH',f"/v1/appStoreVersions/{version['id']}",resource('appStoreVersions',version['id'],
            {'releaseType':'MANUAL'},{'build':relation('builds',build['id'])}))
    else:
        if not attached or version['attributes'].get('releaseType')!='MANUAL': raise ValueError('Prepare must bind the tagged build with MANUAL release first')
        if stage=='publish':
            if state(version)!='PENDING_DEVELOPER_RELEASE': raise ValueError('Apple approval and pending developer release are required')
            api.request('POST','/v1/appStoreVersionReleaseRequests',resource('appStoreVersionReleaseRequests',
                relationships={'appStoreVersion':relation('appStoreVersions',version['id'])}))
        elif stage=='submit':
            if state(version) not in ('PREPARE_FOR_SUBMISSION','READY_FOR_REVIEW','REJECTED','METADATA_REJECTED','DEVELOPER_REJECTED'):
                raise ValueError('App Store version cannot be submitted from its current state')
            check_metadata(api,app,version)
            submissions=api.collect(f"/v1/apps/{app['id']}/reviewSubmissions?"+urlencode({'filter[platform]':'IOS'}))
            pending=[]
            for submission in submissions:
                if submission['attributes']['state']=='COMPLETE': continue
                items=api.collect(f"/v1/reviewSubmissions/{submission['id']}/items?include=appStoreVersion")
                ids=[(item.get('relationships',{}).get('appStoreVersion',{}).get('data') or {}).get('id') for item in items]
                if ids and ids!=[version['id']]: raise ValueError('An unrelated review submission is open; resolve it first')
                if ids==[version['id']]: pending.append(submission)
                elif submission['attributes']['state']!='READY_FOR_REVIEW': raise ValueError('An unrelated review submission is pending')
            if pending:
                submission=one(pending,'matching review submission')
                if submission['attributes']['state']!='READY_FOR_REVIEW': raise ValueError('Selected review submission is already pending or has unresolved issues')
            else:
                submission=api.request('POST','/v1/reviewSubmissions',resource('reviewSubmissions',attributes={'platform':'IOS'},relationships={'app':relation('apps',app['id'])}))['data']
                api.request('POST','/v1/reviewSubmissionItems',resource('reviewSubmissionItems',relationships={
                    'reviewSubmission':relation('reviewSubmissions',submission['id']),'appStoreVersion':relation('appStoreVersions',version['id'])}))
            api.request('PATCH',f"/v1/reviewSubmissions/{submission['id']}",resource('reviewSubmissions',submission['id'],{'submitted':True}))
        else: raise ValueError('Unknown iOS production stage')
    return {'stage':stage,'versionId':version['id'],'buildId':build['id'],'state':state(version)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage',choices=('prepare','submit','publish'))
    parser.add_argument('--provenance',type=Path,required=True)
    for name in ('tag','source-commit','run-id'): parser.add_argument('--'+name,required=True)
    args=parser.parse_args();record=validate_provenance(json.loads(args.provenance.read_text()),args.tag,args.source_commit,args.run_id)
    token=jwt_token(base64.b64decode(os.environ['IOS_ASC_KEY_P8_BASE64'],validate=True),os.environ['IOS_ASC_KEY_ID'],os.environ['IOS_ASC_ISSUER_ID'])
    result=run(Client(token),args.stage,record);print(json.dumps(result,sort_keys=True))


if __name__=='__main__':main()
