import pathlib,json,hashlib,datetime
root=pathlib.Path.cwd();base=root/'artifacts/bengali-localization-v7';sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest();src=json.load(open(base/'delta-105-sources.json'));target=json.load(open(base/'author-output/bn.json'));author=json.load(open(base/'author-output/author-delta.json'));manifest=json.load(open(base/'manifest.json'));assert sha(base/'author-output/bn.json')=='b72dd0184067245ce3c289693dd1b787e2c7eec6eac902671448359a779dc47a'
# Human review notes written after actually reading all105 target values and actual contexts.
notes='''File/API validation terminology remains technical and clear.
Syntax restrictions are not confused with user account settings.
Startup and callback terminology is accurate in AI validation.
Saved/persistent controller state is conveyed.
Fixed seed follows the existing technical seed glossary.
Repeatability correctly means identical simulation on repetition.
Resume wording should explicitly convey continuation, not merely restart.
Good is connection quality.
Fair correctly avoids the unrelated fairness meaning.
Poor correctly denotes weak connection quality.
Round-trip/probe timing and pre-match estimate caveat are preserved.
Delay heading is accurate.
Half-ping plus anti-jitter buffer and shared input effect are preserved.
Behind-clock metric, normal subsecond range and2second threshold are preserved.
Classic glossy material finish is conveyed.
Basic material family is clear.
Matte means an unshiny finish here.
Rivets remain explicit.
Metal material group is clear.
Fur-bearing material is conveyed.
Organic material family is clear.
Cartoon surface style is preserved.
Woven cloth texture is clear.
Craft material family avoids manufacturing-gameplay instructions.
Oozing sticky substance retains its motion and texture.
Wet material family is clear.
Wood material is clear.
Stone material is clear.
Mineral material family is clear.
Scales means an animal surface, not measurement scales.
Leather material is clear.
Crystal material is clear.
Lava material is clear.
Chrome is the coating material.
Carbon fibre material is recognizable.
Velvet material is clear.
Honeycomb names the cells, distinct from the Swarm building.
Ice material is clear.
Cloud material is clear.
Hammered-metal texture is clear.
Candy material transliteration is recognizable.
Slime remains a slippery sticky material.
AI player is consistent with existing terminology.
Finance navigation label is accurate.
Account navigation is consistent with existing catalog wording.
Audit is bookkeeping/administration review.
One-hour filter preserves its duration.
One-day filter preserves its duration.
One-week filter preserves its duration.
Thirty-day filter preserves its duration.
Most played is map play count, not music playback.
Musical structure and development are preserved.
All three music moods must play correctly.
Volume jumps between moods remain explicit.
Loop seam click/gap/abrupt change are all preserved.
Mechanical repetition of short musical phrases is preserved.
Mood switching checks timing alignment and harmonic continuity.
Building/combat versus calm music contrast is preserved.
Hiss/scratchiness in quiet passages are both preserved.
Muddy bass and harsh high frequencies are both conveyed.
Quiet music must remain audible alongside game audio.
Unexpectedly cut-off sustained notes are conveyed.
Worker matches existing native unit family.
Warrior matches existing native unit family.
Explorer matches existing native unit family.
Swarm matches the existing hive-building terminology.
Classic names the original shape.
Crown shape is clear.
Clutch correctly names a brood-egg group, not a mechanical clutch.
Toadstool uses a natural Bengali fungus name.
Broad main cap and small young caps are conveyed.
Coral shape is clear.
Skep correctly names a woven hive.
Coiled stand-mounted hive and door direction are preserved.
Bloom shape is clear.
Public catalog listing is explicit.
Unlisted link access is explicit and respectful.
Private visibility remains only the owner.
Server replay and all-player agreement are preserved.
Success condition must be explicit: completion of a check does not imply verification.
Divergence and authoritative server result are preserved.
Unreproducible match causes no rating change.
Not-applicable verdict is correctly treated as a match not checked.
Failed/incomplete check causes no rating change for now and permits administrator retry.
Walk animation label is parallel to other action nouns.
Swim animation label is parallel to other action nouns.
Harvest animation means collection, including non-crop resources.
Fight animation label is clear.
Fly animation label is clear.
Live payment mode means actual payments rather than streaming.
Test payment mode is clear.
Unclassified payment mode is clear.
Current reporting period is accurate.
Previous reporting period is accurate.
Credit grant means providing credits, not a loan obligation.
Returned credits remain distinct from cash refund terminology.
Dispute is specifically a payment objection.
Dispute closed preserves the completed objection process.
Started-match count label is clear.
Completed-match count label is clear.
Cancelled-match count label is clear.
Library publications preserve the substituted library name.
Recorded download observations preserve the substituted library name.
Completion-duration metric retains seconds and the product placeholder.
Completion sample-count label preserves the product placeholder and sample meaning.'''.splitlines();assert len(notes)==105,len(notes)
findings=[{'id':'bn-v7-pending-verification-condition','key':'online:The server is checking this match. Ratings change once it is verified.','severity':'major','state':'open','target':target['The server is checking this match. Ratings change once it is verified.'],'reason':'যাচাই শেষ হলে means when checking finishes; English verified requires a successful verification. The adjacent failed/unverifiable verdicts explicitly change no rating.','suggested_target':'সার্ভার এই ম্যাচ যাচাই করছে। যাচাই সফল হলে রেটিং বদলাবে।','context':'platform/apps/web/src/pages/Match.tsx VERDICT_TEXT pending/failed/unverifiable'}, {'id':'bn-v7-resume-clarity','key':'online:Save and resume','severity':'minor','state':'open','target':target['Save and resume'],'reason':'আবার চালু করা can suggest restarting. This AI validation label tests continuation from saved state.','suggested_target':'সংরক্ষণ ও চালিয়ে যাওয়া','context':'platform/packages/protocol/src/ais.ts AI_CHECK_LABELS.continuation'}]
rows=[]
for (k,s),note in zip(src.items(),notes):
 assert s==k;f=[x['id'] for x in findings if x['key']=='online:'+k];rows.append({'id':'online:'+k,'source':s,'target':target[k],'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'target_sha256':hashlib.sha256(json.dumps(target[k],ensure_ascii=False,sort_keys=True).encode()).hexdigest(),'actually_read':True,'verdict':'revise' if f else 'accept','finding_ids':f,'review_note':note})
refkeys=['Save and resume','Seeded gameplay','Persistent state','This match is not checked.','Credit grant','Returned credits','Dispute','Skep','Clutch','{value0} completion samples'];references={}
for locale in ['hi','ur','fr','ms']:
 p=root/f'platform/packages/i18n/locales/{locale}.json';catalog=json.load(open(p));references[locale]={'path':str(p.relative_to(root)),'file_sha256':sha(p),'actual_selected_reference_values_read':{k:catalog[k] for k in refkeys}}
contexts=['platform/packages/protocol/src/ais.ts','platform/packages/protocol/src/connectionQuality.ts','platform/packages/protocol/src/skins.ts','platform/apps/web/src/pages/Match.tsx','platform/apps/web/src/pages/Maps.tsx','platform/apps/web/src/skins/atlas.ts','platform/apps/web/src/skins/geometry.ts','platform/apps/web/src/skins/swarmShapes.ts','platform/apps/web/src/music/Validation.tsx','platform/apps/web/src/admin/presentation.ts']
old=root/'artifacts/bengali-localization-v6';oe=json.load(open(old/'source/online.json'));ne=json.load(open(base/'source/online.json'));ot=json.load(open(old/'drafts/bn/bn.json'));assert len(oe)==len(ot)==2842
assert all(ne[k]==s and target[k]==ot[k] for k,s in oe.items());assert (old/'source/native.json').read_bytes()==(base/'source/native.json').read_bytes();assert (old/'drafts/bn/texts.bn.txt').read_bytes()==(base/'drafts/bn/texts.bn.txt').read_bytes()
r={'reviewer_identity':'/root/rendering_audit','role':'Independent initial semantic reviewer, distinct from author/editor/adversary','reviewed_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'NEEDS_REVISION','source_sha256':manifest['source_sha256'],'authored_catalog_sha256':sha(base/'author-output/bn.json'),'author_receipt_sha256':sha(base/'author-output/author-delta.json'),'new_online_keys_actually_read':105,'old_6253_pairs':'Exact carry proof only, not a new full-catalog reading. All3411 native sources/targets and2842 old online sources/targets preserved.','reference_policy':'English and actual game semantics primary. Selected Hindi/Urdu/French/Malay references read for10 ambiguous or technical keys; not a claim all105 keys were read in every reference language.','context_files_sha256':{p:sha(root/p) for p in contexts},'selected_reference_evidence':references,'pairs':rows,'findings':findings,'unresolved_uncertainty':[],'catalog_edits_by_reviewer':False,'limits':['Agent linguistic review, not native-human certification.','No rendering, formatting, actual API charge or platform execution claim from this semantic review.']};(base/'semantic-initial.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n');print('Saved105 actual reads,2 actionable findings, no catalog edits.')
