from pathlib import Path
import json,hashlib,sys
out=Path(__file__).resolve().parent/'native-interactive';p=out/'profile/preferences.txt';d=dict(line.split('=',1) for line in p.read_text().splitlines() if '=' in line)
stage=sys.argv[1];expected=sys.argv[2]
result={'stage':stage,'username':d['username'],'username_utf8_hex':d['username'].encode().hex(),'username_utf8_bytes':len(d['username'].encode()),'language':d['language'],'preferences_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'expected_username':expected,'name_matches':d['username']==expected,'bengali_selected':d['language']=='bn'}
(out/(stage+'-profile.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(result,ensure_ascii=False));assert result['name_matches'] and result['bengali_selected']
