from pathlib import Path
import importlib.util,json,hashlib,datetime
campaign=Path('artifacts/marathi-localization');epoch=campaign/'integration-ffabe';kit=epoch/'kit'
spec=importlib.util.spec_from_file_location('gcloud_draft','tools/localization/google_draft.py');d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
project='bradley-personal-20261027'
with d.kit_lock(campaign):
    paths,targets,items=d.entries(kit,'mr')
    assert len(items)==111 and all(i['surface']=='online' and len(d.item_contents(i))==1 for i in items)
    expected=d.catalog_hashes(paths);budget=d.Budget(campaign/'google-budget.json',project,'9')
    payload=d.request_payload(items,'mr');before=budget.characters
    response=d.fetch(project,payload,campaign/'google-responses',budget)
    translated=response['translations'];assert len(translated)==len(items)
    records=[]
    for item,result in zip(items,translated):
        target=d.unmask(result['translatedText'],item['protected'],item['source'])
        targets['online'][item['key']]=target
        records.append({'key':item['key'],'source':item['source'],'raw_google_target':target})
    d.write_targets(paths,targets,expected)
    provenance={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'project':project,'model':'general/nmt','auth':'gcloud auth print-access-token; token memory-only','helper_sha256':hashlib.sha256(Path('tools/localization/google_draft.py').read_bytes()).hexdigest(),'reviewed_helper':'../integration-bc8/helper-independent-review.json','ledger':'../google-budget.json','lock':'exclusive campaign-root kit lock plus Budget ledger locks','reserved_codepoints_before':before,'reserved_codepoints_after':budget.characters,'reserved_usd_after':str(budget.cost),'source_sha256':json.loads((kit/'manifest.json').read_text())['source_sha256'],'entries':records,'scope':'Initial comparison draft only; independent linguistic reviews still required. No canonical catalog changed.'}
    (epoch/'google-draft.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'drafted':len(records),'reserved_codepoints':budget.characters,'reserved_usd':str(budget.cost)}))
