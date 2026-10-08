from pathlib import Path
import re,subprocess
for name,mode in [('src/map/Map.cpp','theirs'),('src/map/MapResourceState.cpp','both'),('src/app/FileFormatVersions.h','both')]:
 p=Path(name);s=p.read_text();s=re.sub(r'<<<<<<< HEAD\n(.*?)=======\n(.*?)>>>>>>> [^\n]*\n',lambda m:m[2] if mode=='theirs' else m[1]+m[2],s,flags=re.S);p.write_text(s)
p=Path('src/app/Version.h');s=p.read_text();s=re.sub(r'<<<<<<< HEAD\n.*?>>>>>>> [^\n]*\n','''#define VERSION_MINOR 146
// version 146 combines embedded artwork with compact delayed resource growth.
// version 145 is the growth branch's compact signed material proposal format.
// version 144 shipped map artwork; the earlier growth prototype also used 144
//             for incarnation counters and pending material masks (detected on load).
''',s,flags=re.S);s=s.replace('#define NET_PROTOCOL_VERSION 63','#define NET_PROTOCOL_VERSION 64\n// protocol 64 combines custom map assets and delayed resource growth.');p.write_text(s)
p=Path('src/app/FileFormatVersions.h');s=p.read_text()+'''\n// Combined artwork and compact growth. Formats 144/145 predate integration.
static constexpr int FILE_FORMAT_VERSION_ASSETS_AND_RESOURCE_GROWTH = 146;
''';p.write_text(s)
p=Path('src/game/SimRevision.h');p.write_text(p.read_text().replace('#define SIM_REVISION 30','#define SIM_REVISION 31'))
p=Path('src/replay/ReplayReader.h');s=p.read_text().replace('REPLAY_MINIMUM_VERSION_MINOR = 145','REPLAY_MINIMUM_VERSION_MINOR = 146');p.write_text(s)
for name in ['test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt','test/fixtures/resources/seeded-compositions.trace']:
 Path(name).write_bytes(subprocess.check_output(['git','show','HEAD:'+name]))
