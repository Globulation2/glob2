from pathlib import Path
import json,hashlib,re,subprocess
root=Path.cwd();b=root/'artifacts/marathi-localization/integration-ffabe';raw=json.loads((b/'google-draft.json').read_text());entries=raw['entries'];assert len(entries)==111 and raw['source_sha256']=='822cf8613451177b0e2fa803451eaf2308168f21f4d04b7af2aa36cfe64bc3fc';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
# Each explicit row records an actual independent source/draft/context decision.
rows='''1|झटपट हल्ला|AI strategy tag means an early attack, not generic haste; FR/HI confirm attack.
2|बचावात्मक|Correct defensive AI style adjective; compact and natural.
3|अर्थव्यवस्था|Correct economy tag and established Marathi gameplay term.
4|विस्तार|Correct expansion strategy noun.
5|प्रायोगिक|Correct experimental tag.
6|नवशिक्यांसाठी सोपे|Natural concise beginner suitability label rather than bureaucratic अनुकूल.
7|फाइल आणि API प्रोफाइल|Technical API identifier remains recognizable as in existing Marathi JavaScript API reference.
8|वाक्यरचना आणि प्रोफाइलवरील निर्बंध|Case ending makes restrictions apply naturally to the API profile.
9|सुरुवात आणि कॉलबॅक|Startup check is initialization; retain established programming callback loanword.
10|स्थितीचे जतन|Persistent AI state check concerns retained/saved state, not a promise of permanence.
11|ठरावीक बीजासह खेळ|Seeded gameplay is a fixed random-seed execution check; replace untranslated adjective सीडेड.
12|पुनरावृत्ती करण्यायोग्य सिम्युलेशन|Accurate repeatability check heading; technical simulation loanword is natural here.
13|जतन करा आणि पुढे सुरू ठेवा|Resume continues saved execution rather than starting it anew.
14|चांगले|Connection quality rating good; appropriate compact generic quality label.
15|मध्यम|Middle connection quality category, not fairness or suitability.
16|खराब|Poor network quality, not a person being impoverished.
17|खेळाडू आणि रिले यांच्यात संदेश जाऊन परत येण्यासाठी लागणारा वेळ. सामन्यापूर्वी (झटपट सामना, सुरुवातीची तपासणी यादी) हा त्या प्रदेशाशी जोडणी तपासून काढलेला अंदाज असतो.|Metric is round-trip time, not a journey; pre-match probe is an estimate of connection to a region.
18|विलंब|Correct input-delay metric label.
19|तुमचा इनपुट विलंब: क्लिक केल्यापासून ती कृती सर्व खेळाडूंच्या खेळात होईपर्यंतचा वेळ. हा तुमच्या पिंगच्या निम्मा वेळ आणि नेटवर्कमधील विलंबाचे चढउतार सांभाळण्यासाठी खेळात ठेवलेला बफर यांच्या बेरजेइतका असतो.|Retains everyone, half-ping and added jitter buffer; replaces broken raw अधिक construction.
20|खेळाडूचा खेळ सामन्याच्या घड्याळापेक्षा किती मागे आहे. एका सेकंदापेक्षा कमी अंतर सामान्य असते (त्यात इनपुट विलंबही असतो); हे अंतर २ सेकंद झाल्यापासून रिले त्या खेळाडूला धीमा म्हणून नोंदवतो.|Behind is temporal lag; preserve under-one-second normal and at-least-two-second slow threshold.
21|क्लासिक चकचकीत|Classic glossy material preset; familiar style loanword and adjective fit compact picker.
22|मूलभूत|Skin material group heading, so extra गोष्टी is unnecessary.
23|मॅट|Established material finish loanword, not English exception; matte means non-glossy.
24|रिव्हेट लावलेला धातू|Actual riveted finish; fixes raw metal number/agreement while preserving rivet detail.
25|धातू|Correct material group noun.
26|केसाळ|Correct fur/hair surface finish adjective.
27|सेंद्रिय|Correct organic-inspired material category.
28|कार्टून|Familiar Marathi loanword for the visual style, no unchanged-English exemption.
29|विणलेले कापड|Correct woven fabric material.
30|कलाकुसर|Correct craft-inspired material group.
31|ओघळणारा चिकट पदार्थ|Oozy goo is a flowing viscous material, not adhesive glue.
32|ओले|Correct wet material group label, covering goo/lava/slime finishes.
33|लाकूड|Correct wood material.
34|दगड|Correct stone material.
35|खनिज|Correct mineral category.
36|खवले|Animal-scale surface texture, not numerical scales or a measuring scale.
37|चामडे|Correct leather material.
38|स्फटिक|Natural Marathi crystal material noun; compact and clear.
39|लावा|Established Marathi name for molten-rock material.
40|क्रोम|Metallic chrome finish, not the browser brand; accepted Devanagari technical loanword.
41|कार्बन फायबर|Recognizable material name in standard Marathi transliteration.
42|मखमल|Material noun velvet rather than adjective मखमली standing alone.
43|मधाचे पोळे|Honeycomb pattern/material; distinguish context from the Swarm building while retaining honeycomb sense.
44|बर्फ|Correct ice material.
45|ढग|Correct cloud-like material.
46|हातोड्याने ठोकलेला धातू|Correct hammered-metal surface finish, raw already has natural agreement.
47|कँडी|Familiar candy visual-style loanword, no English exception.
48|चिकट जेल|Viscous slime material; descriptive Marathi is clearer than opaque स्लाइम.
49|एआय खेळाडू|Correct AI player route heading with established एआय term.
50|वित्त|Correct compact finances administration section heading.
51|खाती|Correct accounts administration section heading.
52|ऑडिट|Administration event audit, not financial-account auditing only; matches existing ऑडिट इतिहास.
53|१ तास|Correct mute-duration option; numeric meaning retained.
54|१ दिवस|Correct mute-duration option.
55|१ आठवडा|Correct mute-duration option.
56|३० दिवस|Correct thirty-day mute-duration option.
57|सर्वाधिक खेळलेले|Map sorting caption describes maps played most; use adjectival form.
58|संगीताची रचना आणि तिचा विकास लक्ष देऊन ऐका.|Explicit listening task for musical form/development, not general visual inspection.
59|तिन्ही मूडमधील संगीत नीट वाजते याची खात्री करा.|Playback of all three musical mood tracks; no human mental-state reading.
60|मूड बदलताना आवाजाच्या पातळीत अनपेक्षित उड्या येतात का ते ऐका.|Unexpected loudness jumps during music mood changes, not emotional states.
61|लूप पुन्हा सुरू होताना क्लिकचा आवाज, खंड किंवा अचानक बदल ऐकू येतो का ते तपासा.|Loop seam is an audible click/gap/change, not a mouse click.
62|संगीतातील छोटे तुकडे अतिशय यांत्रिकपणे पुन्हा पुन्हा वाजतात का ते ऐका.|Phrases are musical motifs, not linguistic sentences.
63|मूड बदलून लयीत किंवा स्वरमेळात होणारे बदल ऐका.|Timing/harmony in music; सुसंवाद would imply social conversation.
64|«बांधकाम» आणि «लढाई» या मूडमधील संगीत «शांत» मूडपेक्षा वेगळे जाणवते याची खात्री करा.|Names the actual three translated music moods; not a test of construction gameplay.
65|कमी आवाजाच्या भागांत सळसळ किंवा खरखर ऐकू येते का ते तपासा.|Quiet musical passages and audible hiss/scratch defects, not literal whispering.
66|अस्पष्ट बास किंवा उच्च वारंवारतेचे कर्कश आवाज ऐकू येतात का ते तपासा.|Audio mix balance; preserve muddy bass and harsh high-frequency meaning.
67|खेळाच्या आवाजासोबत संगीताचे शांत भागही ऐकू येत राहतात याची खात्री करा.|Quiet passages are music and must remain audible beside game sound.
68|अनपेक्षितपणे तुटणाऱ्या सुरांकडे लक्ष द्या.|Unexpected note dropout, not an intentional musical ending.
69|कामगार|Exact established native worker anchor.
70|योद्धा|Exact established native warrior anchor.
71|शोधक|Exact established native explorer anchor.
72|पोळे|Actual Swarm building mesh/model, French Ruche and native glossary; not a flock थवा.
73|क्लासिक|Original classic Swarm shape; keep consistent with Classic glossy style label.
74|मुकुट|Correct crown-shaped Swarm variant.
75|अंड्यांचा समूह|Clutch shape is six brood eggs, not a mechanical clutch.
76|अळंबी|Toadstool is a mushroom-shaped Swarm, not a frog; concise Marathi mushroom noun.
77|रुंद छत्रीच्या अळंबीभोवती छोट्या अळंब्या.|Visible broad mushroom cap with young caps; fixes raw gender and avoids inventing toxicity mechanics.
78|प्रवाळ|Correct coral-inspired branching shape.
79|विणलेले पोळे|Skep is a coiled/woven beehive shape; descriptive Marathi rather than opaque स्केप.
80|आधारावर ठेवलेले, वेटोळ्यांसारखे विणलेले पोळे; त्याचे दार तुमच्याकडे आहे.|Coiled hive on stand with door facing viewer, not a live hive curling itself.
81|उमललेले फूल|Bloom is a flower-shaped object/variant, not the verb फुलणे.
82|सार्वजनिक: कॅटलॉगमध्ये सूचीबद्ध|Correct public-map visibility option; existing catalog terminology preserved.
83|सूचीबद्ध नसलेले: लिंक असलेल्या कोणालाही उपलब्ध|Unlisted option is accessible to link holders, with complete natural Marathi predicate.
84|खाजगी: फक्त तुम्ही|Correct private visibility option.
85|सर्व्हरने हा सामना पुन्हा चालवला आणि प्रत्येक खेळाडूच्या खेळाची स्थिती त्याच्याशी जुळली.|Verification compares game state, not players expressing agreement.
86|सर्व्हर या सामन्याची तपासणी करत आहे. पडताळणी झाल्यावर रेटिंग बदलतील.|Accurate pending-check status and rating update condition.
87|एका खेळाडूच्या खेळाची स्थिती सर्व्हरच्या रीप्लेशी जुळली नाही; सर्व्हरचा निकालच ग्राह्य धरला जातो.|Actual disagreement happened; remove raw conditional नसला तरी and retain authoritative server result.
88|सर्व्हरला हा सामना तसाच पुन्हा चालवता आला नाही, त्यामुळे रेटिंगमध्ये कोणताही बदल होत नाही.|Could not reproduce the same simulation; avoids implying merely scheduling a new game.
89|या सामन्याची तपासणी केलेली नाही.|Accurate unchecked status, no pending/success inference.
90|सर्व्हरला या सामन्याची तपासणी पूर्ण करता आली नाही, त्यामुळे सध्या रेटिंगमध्ये कोणताही बदल होत नाही. प्रशासक ही तपासणी पुन्हा चालवू शकतात.|Failed/incomplete check with temporary no-rating effect and admin retry, no permanent failure claim.
91|चालणे|Animation action label, noun/infinitive consistent with other actions.
92|पोहणे|Correct swimming animation action.
93|गोळा करणे|Worker harvest gathers resources including non-crops, not agricultural harvesting only.
94|लढणे|Animation action infinitive consistent with चालणे and पोहणे, not imperative लढा.
95|उडणे|Explorer flight animation action, not the insect माशी.
96|प्रत्यक्ष पेमेंट|Live payment environment means real transactions, not a live broadcast or direct method.
97|चाचणी पेमेंट|Correct test-payment environment label; existing पेमेंट anchor retained.
98|अवर्गीकृत पेमेंट|Correct unclassified payment-environment label.
99|चालू कालावधी|Correct current reporting window.
100|मागील कालावधी|Correct previous reporting window.
101|क्रेडिट्सचे अनुदान|Credit grant is platform usage credits, not a bank loan/pat creditworthiness.
102|परत केलेले क्रेडिट्स|Correct returned unused-credit ledger category; consistent क्रेडिट्स anchor.
103|वाद|Payment dispute category; concise and neutral, not an adjudicated outcome.
104|वाद बंद झाला|Closed dispute status, not necessarily resolved/settled in anyone's favor.
105|सुरू झालेले सामने|Count/statistic caption for started matches, not a new event announcement.
106|पूर्ण झालेले सामने|Count/statistic caption for completed matches.
107|रद्द झालेले सामने|Count/statistic caption for cancelled matches.
108|{value0}: प्रकाशने|value0 is translated library label, not a numeric publication count; colon avoids unsafe Marathi inflection.
109|{value0}: नोंदवलेले डाउनलोड्स|value0 is library label; recorded means tracked downloads, not video recordings.
110|{value0}: पूर्ण होण्यास लागलेला वेळ (सेकंद)|value0 is product label, not duration; retain seconds as unit and unambiguous caption structure.
111|{value0}: पूर्णतेच्या वेळेच्या नोंदी|Completion samples are recorded duration observations for product value0, not sample products or a percent-complete marker.'''
decisions={}
for row in rows.splitlines():
 index,target,reason=row.split('|',2);index=int(index);assert index not in decisions;decisions[index]=(target,reason)
assert set(decisions)==set(range(1,112))
refs={l:json.loads((root/f'platform/packages/i18n/locales/{l}.json').read_text()) for l in ['hi','fr','ms']}
files=['platform/packages/protocol/src/ais.ts','platform/packages/protocol/src/connectionQuality.ts','platform/packages/protocol/src/skins.ts','platform/apps/web/src/App.tsx','platform/apps/web/src/admin/Admin.tsx','platform/apps/web/src/admin/Accounts.tsx','platform/apps/web/src/admin/presentation.ts','platform/apps/web/src/pages/Maps.tsx','platform/apps/web/src/music/Validation.tsx','platform/apps/web/src/skins/atlas.ts','platform/apps/web/src/skins/swarmShapes.ts','platform/apps/web/src/pages/Match.tsx','platform/apps/web/src/skins/geometry.ts']
contexts=[(1,13,'AI tag/filter and compatibility-check heading','platform/packages/protocol/src/ais.ts',['platform/apps/web/src/pages/Ais.tsx']), (14,20,'Connection rating or measured latency help, not simulated rules','platform/packages/protocol/src/connectionQuality.ts',['platform/apps/web/src/pages/Match.tsx','platform/apps/web/src/components/ConnectionQuality.tsx']), (21,48,'Skin material finish name or grouped picker heading; protocol material IDs remain unchanged','platform/packages/protocol/src/skins.ts',['platform/apps/web/src/skins/StudioControls.tsx','platform/apps/web/src/skins/PatternDialog.tsx']), (49,49,'AI player page title','platform/apps/web/src/App.tsx',['platform/apps/web/src/pages/Ais.tsx']), (50,52,'Administration tab label; Audit is administrative event history, not only financial accounts','platform/apps/web/src/admin/Admin.tsx',['platform/apps/web/src/admin/Moderation.tsx']), (53,56,'Account mute duration option','platform/apps/web/src/admin/Accounts.tsx',[]), (57,57,'Map list sorting option','platform/apps/web/src/pages/Maps.tsx',[]), (58,68,'Human listening guidance in music validation; three translated tracks/moods Calm Building Combat','platform/apps/web/src/music/Validation.tsx',['platform/apps/web/src/music/Player.tsx']), (69,72,'Skin model picker names; Swarm is the unit-producing building','platform/apps/web/src/skins/atlas.ts',['platform/apps/web/src/skins/StudioControls.tsx']), (73,81,'Swarm mesh shape labels/descriptions; preserve canonical mesh IDs','platform/apps/web/src/skins/swarmShapes.ts',['platform/apps/web/src/skins/geometry.ts']), (82,84,'Map publishing visibility choices; access semantics, not canonical visibility IDs','platform/apps/web/src/pages/Maps.tsx',[]), (85,90,'Match verdict explanatory prose; distinguish verified/pending/diverged/unverifiable/unchecked/error','platform/apps/web/src/pages/Match.tsx',[]), (91,95,'Unit preview animation selector action labels, not animation asset IDs','platform/apps/web/src/skins/geometry.ts',['platform/apps/web/src/skins/MeshPreview.tsx']), (96,111,'Administration finance/analytics display label; final4 placeholders are product/library names, not numbers','platform/apps/web/src/admin/presentation.ts',['platform/apps/web/src/admin/Finances.tsx'])]
packet=[];review=[]
for i,e in enumerate(entries,1):
 target,reason=decisions[i];assert e['source']==e['key'];reg=next(c for c in contexts if c[0]<=i<=c[1]);file=reg[3];code=(root/file).read_text();hits=[j+1 for j,line in enumerate(code.splitlines()) if e['source'] in line];assert hits,(i,e['key'],file)
 references={l:{'value':cat.get(e['key']),'equals_English_source':cat.get(e['key'])==e['source'],'missing':e['key'] not in cat} for l,cat in refs.items()};entry={'number':i,'key':e['key'],'source':e['source'],'raw_google_target':e['raw_google_target'],'context':{'description':reg[2],'registry':file,'literal_lines':hits,'UI_callers':[p for p in reg[4] if (root/p).exists()]},'reference_translations':references};packet.append(entry);review.append({**entry,'recommended_target':target,'disposition':'edit' if target!=e['raw_google_target'] else 'accept_raw','rationale':reason,'accepted_english':False})
assert all(re.findall(r'\{[A-Za-z][A-Za-z0-9_]*\}',x['source'])==re.findall(r'\{[A-Za-z][A-Za-z0-9_]*\}',x['recommended_target']) for x in review)
assert all(x['recommended_target']!=x['source'] for x in review)
base={'reviewer':'/root/locale_integration','role':'independent editorial','scope':'Actual all111 English/rawGoogle/context/HI-FR-MS reference read. No other reviewers proposals opened before these decisions. No canonical/draft mutation, API call or build.','source_sha256':raw['source_sha256'],'google_draft_sha256':sha(b/'google-draft.json'),'raw_mr_draft_sha256':sha(b/'kit/drafts/mr/mr.json'),'source_snapshot_sha256':sha(b/'kit/source/online.json'),'canonical_old_mr_sha256':sha(root/'platform/packages/i18n/locales/mr.json'),'frozen_source_revision':json.loads((b/'kit/manifest.json').read_text()).get('source_revision'),'references_sha256':{l:sha(root/f'platform/packages/i18n/locales/{l}.json') for l in refs},'context_files_sha256':{p:sha(root/p) for p in set(files+[p for c in contexts for p in c[4] if (root/p).exists()])}}
(b/'new-online-context.json').write_text(json.dumps({**base,'coverage_count':111,'new_english_exceptions':[],'notes':['All HI/FR/MS entries exist; equality-to-English is marked per entry and is not treated as an independent translation reference.','Technical API remains literal inside Marathi text; Chrome is a metal finish, not a browser brand. No whole-key English exception is proposed.','Old MR2836 remain exact carry; decisions here concern only111 new fixed metadata sources.'],'entries':packet},ensure_ascii=False,indent=2)+'\n')
report={**base,'all111_reviewed':True,'coverage_count':111,'accepted_raw_count':sum(x['disposition']=='accept_raw' for x in review),'edit_count':sum(x['disposition']=='edit' for x in review),'reviewed_keys':[x['key'] for x in review],'new_english_exceptions':[],'decisions':review,'status':'Independent first editorial proposals; root resolves union and final actual canonical reread still required.'};(b/'editorial-new111.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print(json.dumps({'coverage':111,'edits':report['edit_count'],'accepted_raw':report['accepted_raw_count'],'review_sha256':sha(b/'editorial-new111.json'),'context_sha256':sha(b/'new-online-context.json')},indent=2))
