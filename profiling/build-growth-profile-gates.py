import shlex,subprocess,json
from pathlib import Path
root=Path.cwd();out=root/'artifacts/resource-growth/profiling';commands={}
for mode,log,cwd in [('candidate','candidate-build-final.log',root),('legacy','baseline-build.log',root/'artifacts/resource-growth/baseline-src')]:
 src=(cwd/'src/app/cli/Headless.cpp').read_text()
 gate='''
        const auto profileControl = [](const char* message) {
            for (const char* prefix : {"GLOB2_PERF_STAT", "GLOB2_PERF_RECORD"}) {
                const auto key = std::string(prefix);
                if (const char* path = std::getenv((key + "_CTL").c_str())) {
                    std::ofstream control(path); control << message << std::endl;
                    if (const char* reply = std::getenv((key + "_ACK").c_str())) {
                        std::ifstream ack(reply); std::string result; std::getline(ack, result);
                        if (result != "ack") throw std::runtime_error("profiling control failed");
                    }
                }
            }
        };
        profileControl("enable");
'''
 src=src.replace('\t\tconst auto runStart =',gate+'\t\tconst auto runStart =',1)
 src=src.replace('const auto runEnd = std::chrono::steady_clock::now();','const auto runEnd = std::chrono::steady_clock::now();\n        profileControl("disable");',1)
 path=out/(mode+'-Headless.cpp');path.write_text(src)
 lines=(root/'artifacts/resource-growth'/log).read_text().splitlines()
 line=next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o build/linux/client/release/src/app/cli/Headless.o '))
 cmd=shlex.split(line);cmd[cmd.index('-o')+1]=str(out/(mode+'-Headless.o'));cmd[-1]=str(path)
 # Preserve release optimization; only these profiling start/stop hooks differ.
 with (out/(mode+'-gated-build.log')).open('w') as f:subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True)
 link=json.loads((out/(mode+'-relink-command.json')).read_text());link[2]=str(out/(mode+'-gated'));link=[str(out/(mode+'-Headless.o')) if a=='build/linux/client/release/src/app/cli/Headless.o' else a for a in link]
 with (out/(mode+'-gated-build.log')).open('a') as f:subprocess.run(link,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True)
 commands[mode]={'compile':cmd,'link':link,'cwd':str(cwd)};print(mode,'gated build ready',flush=True)
(out/'gated-build-commands.json').write_text(json.dumps(commands,indent=2))
