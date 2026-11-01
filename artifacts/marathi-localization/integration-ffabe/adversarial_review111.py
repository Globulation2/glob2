import json,hashlib
from pathlib import Path
base=Path(__file__).parent
rawp=base/'google-draft.json'; draftp=base/'kit/drafts/mr/mr.json'
raw=json.loads(rawp.read_text(encoding='utf-8'));draft=json.loads(draftp.read_text(encoding='utf-8'))
# Independently read each source/draft and actual consumer; changes keyed by displayed batch index.
changes={
1:('सुरुवातीचा हल्ला','AI strategy tag means an early attack, not the emotion or action of hurrying; French/Hindi/Malay corroborate.'),
7:('फाइल आणि API प्रोफाइल','Preserve the recognized API acronym in this technical validation heading; this is a file/API contract check.'),
11:('निश्चित बीजासह खेळ','AI validation runs a seeded game; opaque सीडेड loses the reproducible-random-seed meaning.'),
15:('मध्यम','Connection quality band between Good and Poor; योग्य means appropriate rather than fair/middling.'),
16:('खराब','Connection quality, not economic poverty. गरीब is a false friend.'),
17:('खेळाडू आणि रिले सर्व्हर यांच्यात संदेश जाऊन परत येण्यासाठी लागणारा वेळ. सामन्यापूर्वी (जलद सामना, सुरुवातीची तपासणी सूची) हा प्रदेशातील जोडणीची चाचणी घेऊन काढलेला अंदाज असतो.','RTT measures elapsed network time; draft describes a journey and leaves relay context unclear. Preserve pre-match estimate distinction.'),
19:('तुमचा इनपुट विलंब: तुम्ही क्लिक केल्यापासून ती कृती सर्व खेळाडूंच्या खेळात होईपर्यंतचा वेळ. तुमच्या पिंगचा निम्मा वेळ आणि नेटवर्क विलंबातील चढउतार सांभाळण्यासाठी खेळाने ठेवलेला बफर यांची बेरीज.','Fix corrupted घटेपर्यंत, preserve everyone seeing execution, express half-ping plus jitter buffer as an additive duration.'),
20:('एखाद्या खेळाडूचा खेळ सामन्याच्या घड्याळापेक्षा किती मागे चालतो. एका सेकंदापेक्षा कमी अंतर सामान्य असते (यात त्या खेळाडूचा इनपुट विलंबही येतो); २ सेकंदांपासून रिले सर्व्हर त्या खेळाडूला संथ म्हणून चिन्हांकित करतो.','Preserve game-clock lag and inclusive 2-second marking threshold; avoid unexplained मॅच क्लॉक and distinguish relay marking from opinion.'),
24:('रिव्हेट लावलेला धातू','Material धातू is singular masculine; draft केलेले धातू has agreement mismatch.'),
27:('जैविक','Material family includes hair, scales and leather. जैविक avoids the narrower agricultural/chemical implication of सेंद्रिय.'),
31:('ओघळणारा चिकट पदार्थ','Goo is viscous material, not adhesive glue; preserve oozy movement.'),
36:('खवले','Skin material is animal scales, not a measurement scale; corroborated by actual material table and French écailles.'),
43:('पोळ्याची जाळी','Honeycomb surface pattern/material differs from the whole hive building; makes the repeated-cell texture clear.'),
48:('चिकट जेल','Viscous surface material; meaningful label instead of opaque स्लाइम, and distinct from flowing goo.'),
57:('सर्वाधिक खेळलेले','Map sorting option needs a descriptive participle, not the finite past सर्वाधिक खेळले.'),
59:('संगीताच्या तिन्ही मनःस्थिती योग्यरीत्या वाजतात याची खात्री करा.','Actual music playback validation; moods are musical variants and play means audible playback, not merely operational status.'),
60:('संगीताच्या मनःस्थिती बदलताना आवाजाच्या पातळीत अचानक होणाऱ्या अनपेक्षित बदलांकडे लक्ष द्या.','Mood refers to music, not the listener’s mental states; preserve sudden volume jumps.'),
62:('अतिशय यांत्रिकपणे पुन्हा पुन्हा वाजणाऱ्या छोट्या संगीतखंडांकडे लक्ष द्या.','Musical phrases, not linguistic phrases; explicit audible repetitions fit the validation task.'),
63:('संगीताची मनःस्थिती बदला आणि वेळेतील किंवा सुरांच्या मेळातील बदल ऐका.','Timing may change independently of rhythm; harmony means simultaneous tonal relationships, not conversational concord.'),
64:('बांधकाम आणि लढाई या संगीताच्या मनःस्थिती शांत मनःस्थितीपेक्षा वेगळ्या जाणवतात याची खात्री करा.','Check contrast of actual named Calm/Building/Combat music variants, not the physical acts of construction/combat.'),
65:('कमी आवाजाच्या संगीतखंडांमध्ये सुसाट किंवा खरखरीत आवाज येतो का ते ऐका.','Quiet passages are low-volume musical passages; hiss is unwanted noise, not human whispering.'),
66:('गढूळ वाटणारे खर्जाचे स्वर किंवा उच्च वारंवारतेचे कर्कश स्वर येतात का ते ऐका.','Audio-specific bass muddiness and harsh high frequencies; retain low/high distinction without untranslated frequency loan.'),
72:('पोळे','This is the colony swarm mesh/building shown alongside worker/warrior/explorer, not a moving flock; existing native [swarm]=पोळे and other-language refs agree.'),
75:('अंड्यांचा समूह','Shape consists of six brood eggs in a rim; mechanical क्लच is a false friend.'),
76:('अळिंबी','Toadstool mesh is a mushroom, not a toad; बेडूक is incorrect.'),
77:('लहान छत्रांसह रुंद छत्र असलेली अळिंबी.','Mushroom caps, not wearable hats; fix mixed singular/plural grammar and preserve large central plus young caps.'),
79:('विणलेले पोळे','Skep is a woven/coiled hive shape, not an opaque proper product name; other-language references make construction explicit.'),
80:('आधारावर ठेवलेले वेटोळ्यांचे पोळे; त्याचे दार तुमच्याकडे आहे.','The hive itself is coiled; it is not an animal coiling up while placed on a stand. Preserve front-facing entrance.'),
81:('फुललेले फूल','Shape label is the open flower form, not the verb फुलणे.'),
85:('सर्व्हरने हा सामना पुन्हा चालवला आणि प्रत्येक खेळाडूच्या खेळाची स्थिती त्याच्याशी जुळली.','Deterministic agreement is matching game state, not human assent; normalize server spelling.'),
87:('एका खेळाडूच्या खेळाची स्थिती सर्व्हरने पुन्हा चालवलेल्या सामन्याशी जुळली नाही; सर्व्हरचा निकाल ग्राह्य धरला जातो.','Actual divergence happened; draft concessive जुळत नसला तरी makes it hypothetical and loses direct state comparison.'),
88:('सर्व्हरला हा सामना त्याच प्रकारे पुन्हा चालवता आला नाही, त्यामुळे कोणत्याही रेटिंगमध्ये बदल होत नाही.','Reproduce means reproduce execution; preserve no rating changes and consistent सर्व्हर spelling.'),
94:('लढणे','Animation selector parallels चालणे/पोहणे; avoid imperative लढा.'),
95:('उडणे','Animation verb Fly, not the insect माशी.'),
96:('वास्तविक देयके','Live versus test payment environments: real transactions, not livestream/direct payments.'),
97:('चाचणी देयके','Align payment noun with real-payment category; no currency amount/unit conversion.'),
98:('अवर्गीकृत देयके','Align financial category wording; preserve unknown classification rather than test/live assumption.'),
101:('क्रेडिट्सचे वाटप','Account balance credits being granted, not a loan/creditworthiness subsidy. Use established virtual-credit glossary.'),
104:('वाद बंद झाला','Payment dispute closed is a process status, not necessarily resolved or settled; मिटला overclaims successful resolution.'),
109:('{value0}: नोंदवलेले डाउनलोड','value0 is a library name, not count. Recorded means counted/logged downloads, not downloaded audio recordings.'),
110:('{value0}: पूर्ण होण्यासाठी लागलेला वेळ (सेकंद)','value0 is a product name; completion duration, not time-of-day timestamp. Preserve seconds unit and token.'),
111:('{value0}: पूर्णतेचे मापन नमुने','Samples are duration measurement observations for the named product, not example finished outputs. Preserve token.'),
}
notes='''AI defensive strategy tag is accurately adjectival.
Economy strategy tag matches existing अर्थव्यवस्था.
Expansion strategy describes territorial growth accurately.
Experimental tag correctly marks an experimental controller.
Beginner-friendly tag is natural Marathi and retains intended audience.
Syntax/profile restrictions correctly describes grammar and allowed API/profile constraints.
Startup and callback are established technical loans in an AI callback validation heading.
Persistent state denotes data surviving continuation; कायमस्वरूपी स्थिती is acceptable here.
Repeatable simulation correctly denotes repeatable execution, not replay file format.
Save and resume preserves both actions and the pause/resumption meaning.
Good is the positive connection-quality band and चांगले fits a neutral quality label.
Delay correctly denotes elapsed latency, with explanation supplied separately.
Glossy material label clearly names classic reflective appearance.
Basics material group uses a natural Marathi category phrase.
Matte is a recognized rendering/material loan; मॅट distinguishes non-gloss surface.
Metal category is the substance धातू.
Hairy labels fur-shell material and केसाळ is direct.
Cartoon is an established visual-style loan.
Woven fabric accurately names the woven texture.
Craft category names handmade material grouping.
Wet group is a valid neutral material-family adjective.
Wood names the material, not a tree.
Stone names the material.
Mineral names the material group.
Leather names the surface material.
Crystal transliteration is a recognized mineral/material label.
Lava is an established material loan.
Chrome is the recognized metallic finish, not the browser brand in this context.
Carbon fibre is a recognized material loan and retains fibre identity.
Velvet is accurately conveyed by मखमली as a surface adjective.
Ice correctly names frozen water material.
Cloud correctly names cloud-like material.
Hammered metal preserves textured manufacture and correct masculine agreement.
Candy is an established visual-material loan.
AI player correctly denotes a player controlled by AI; एआय is acceptable Marathi acronym transliteration.
Finances is an administration financial section, and वित्त is suitable.
Accounts correctly denotes user accounts, not arithmetic.
Audit correctly names the administrative audit section.
One-hour reporting window preserves value and hour unit using Devanagari digit.
One-day reporting window preserves value and day unit.
One-week reporting window preserves value and week unit.
Thirty-day reporting window preserves exact duration.
Musical structure and development are accurately conveyed as रचना आणि विकास.
Loop instruction preserves audible click/gap/abrupt change and repeat timing.
Audibility instruction correctly requires calm passages remain audible alongside game sound.
Unexpected note cutoffs are accurately expressed with सुर and sudden stop.
Worker matches the established कामगार unit name.
Warrior matches native योद्धा unit name.
Explorer matches established शोधक glossary.
Classic identifies the original/default shape and the familiar loan is suitable.
Crown shape correctly refers to मुकुट.
Coral mesh correctly refers to प्रवाळ.
Public visibility explicitly retains listing in the catalog.
Unlisted visibility accurately retains access for anyone with link.
Private visibility accurately restricts visibility to the owner.
Pending server verification and later ratings update are accurately distinguished.
Unchecked match status is correctly stated without implying failed verification.
Incomplete verification status preserves temporary no-rating change and administrator retry.
Walk is correctly a nominal animation verb.
Swim is correctly a nominal animation verb.
Harvest is correctly an animation/activity name rather than a purchase operation.
Current reporting period is accurate.
Previous reporting period is accurate.
Returned credits is accurate and keeps virtual-credit terminology.
Dispute accurately names a payment dispute without asserting its outcome.
Matches-started metric counts matches whose start occurred; Marathi past-state label is acceptable.
Matches-completed metric preserves completion status.
Matches-cancelled metric preserves cancellation status.
Publication metric preserves placeholder and refers to library publications.'''.splitlines()
accepted=[i for i in range(1,112) if i not in changes]
assert len(notes)==len(accepted),(len(notes),len(accepted))
rationale=dict(zip(accepted,notes))
refs={l:json.loads(Path(f'platform/packages/i18n/locales/{l}.json').read_text(encoding='utf-8')) for l in ['fr','hi','ms']}
rows=[]
for i,e in enumerate(raw['entries'],1):
 assert draft[e['key']]==e['raw_google_target'],e['key']
 row={'index':i,'key':e['key'],'source':e['source'],'before':e['raw_google_target'],'proposal':changes[i][0] if i in changes else e['raw_google_target'],'disposition':'edit' if i in changes else 'accept','rationale':changes[i][1] if i in changes else rationale[i]}
 row['references']={l:r.get(e['key']) for l,r in refs.items()}
 rows.append(row)
assert len(rows)==111
out={'role':'independent adversarial linguistic reviewer','independence':'Own source/context judgment before reading any other reviewer proposals. Every one of the 111 actual pairs was read; no full reread claim for unchanged previous 2836.','source_sha256':raw['source_sha256'],'raw_draft_record_sha256':hashlib.sha256(rawp.read_bytes()).hexdigest(),'complete_draft_sha256':hashlib.sha256(draftp.read_bytes()).hexdigest(),'reviewed_key_count':len(rows),'reviewed_keys':[r['key'] for r in rows],'edit_count':len(changes),'accepted_english':[],'english_exception_note':'No new entire value is accepted unchanged in English. API is intentionally retained as a standard technical acronym inside a Marathi heading; established material loans are Marathi transliterations, not untranslated full UI values.','context_files':['platform/packages/protocol/src/ais.ts','platform/packages/protocol/src/skins.ts','platform/apps/web/src/pages/Match.tsx','platform/apps/web/src/pages/Maps.tsx','platform/apps/web/src/skins/swarmShapes.ts','platform/apps/web/src/skins/geometry.ts','platform/apps/web/src/music/Validation.tsx','platform/apps/web/src/admin/presentation.ts'],'rows':rows,'patches':[r for r in rows if r['disposition']=='edit'],'limitations':'Language review only; no fresh visual fit, API calls or canonical/draft writes. Payment labels do not change any currency/unit/amount. All four dynamic value0 tokens preserved in proposed wording.'}
p=base/'adversarial-new111.json';p.write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(p,len(changes),hashlib.sha256(p.read_bytes()).hexdigest())
