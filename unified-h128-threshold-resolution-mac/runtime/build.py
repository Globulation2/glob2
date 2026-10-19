import datetime, fcntl, hashlib, json, os, pathlib, shlex, subprocess, traceback

B = pathlib.Path(__file__).resolve().parent
O = B / 'build'
O.mkdir(exist_ok=True)
S = O / 'source'
OLD = pathlib.Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2')
REV = '4a2cf3dac9eb4eab0aab932307ca5096be953491'
ELF = '995f3decd7c42ec002110424294b0f4a681f04cb8c173dbfb00dd75b3c3fa1f0'
guard = (O / 'lock').open('a')
fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
sha = lambda p: hashlib.file_digest(pathlib.Path(p).open('rb'), 'sha256').hexdigest()
state = dict(status='building-research-only', source_base=REV, frozen_base_elf=ELF, pid=os.getpid(),
             started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
def save(): (O / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
def run(argv, **kwargs): return subprocess.run(argv, check=True, **kwargs)
save()
try:
    assert not subprocess.check_output(['git','-C',str(OLD),'diff',REV,'HEAD','--','src','libgag','libusl'],text=True).strip()
    assert not subprocess.check_output(['git', '-C', str(OLD), 'status', '--porcelain'], text=True).strip()
    assert sha(OLD / 'build/candidate-release/src/glob2') == ELF
    run(['git', 'clone', '--shared', '--no-checkout', str(OLD), str(S)], stdout=subprocess.DEVNULL)
    run(['git', '-C', str(S), 'checkout', '--detach', REV], stdout=subprocess.DEVNULL)
    os.symlink(OLD / 'build', S / 'build', target_is_directory=True)  # Read-only generated inputs.
    f = S / 'src/map/gradient/AdaptiveGradientState.cpp'
    text = f.read_text()
    helper = '''
// ISOLATED RESEARCH ONLY. Fixed gen003 scores; only the admission threshold varies.
std::int64_t researchGradientThreshold() {
    static const std::int64_t value=[] {
        const char* raw=std::getenv("GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI");
        if(!raw)return AdaptiveGradientState::Threshold;
        if(!*raw)throw std::invalid_argument("Invalid research gradient threshold: expected positive integer");
        std::int64_t parsed=0;
        for(const char* p=raw;*p;++p) {
            if(*p<'0'||*p>'9')throw std::invalid_argument("Invalid research gradient threshold: expected positive integer");
            const int digit=*p-'0';
            if(parsed>(std::numeric_limits<std::int64_t>::max()-digit)/10)
                throw std::invalid_argument("Invalid research gradient threshold: integer overflow");
            parsed=parsed*10+digit;
        }
        if(parsed<=0)throw std::invalid_argument("Invalid research gradient threshold: expected positive integer");
        return parsed;
    }();
    return value;
}
'''
    assert text.count('contradictions())>=Threshold') == 1
    text = text.replace('#include <stdexcept>', '#include <stdexcept>\n#include <cstdlib>\n#include <limits>')
    text = text.replace('\nnamespace {', helper + '\nnamespace {', 1)
    f.write_text(text.replace('contradictions())>=Threshold', 'contradictions())>=researchGradientThreshold()'))
    f = S / 'src/app/cli/Headless.cpp'
    text = f.read_text().replace('namespace fs = std::filesystem;', 'std::int64_t researchGradientThreshold();\nnamespace fs = std::filesystem;', 1)
    anchor = '\tstatic int game(const Options &options, const fs::path &output)\n\t{'
    addition = '''
        // Research override is eagerly checked even if no model evaluation occurs.
        const auto researchThreshold=researchGradientThreshold();
        const char* researchRaw=std::getenv("GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI");
        const std::string researchInput=nlohmann::json({
            {"schema",1},{"research_only_binary",true},
            {"environment_key","GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI"},
            {"environment_value",researchRaw?nlohmann::json(researchRaw):nlohmann::json(nullptr)},
            {"threshold_milli_direction_cost",researchThreshold},
            {"base_threshold_milli_direction_cost",AdaptiveGradientState::Threshold},
            {"model_contract",AdaptiveGradientModel::ContractId}}).dump();
        Headless::writeJson((output/"research-gradient-threshold-input.json").string(),researchInput);
        const auto researchInputHash=Online::Sha256::hex(researchInput+"\\n");
'''
    assert text.count(anchor) == 1
    text = text.replace(anchor, anchor + addition)
    old = '<< ",\\\"threshold_milli_direction_cost\\\":" << AdaptiveGradientState::Threshold'
    assert text.count(old) == 1
    text = text.replace(old, '''<< ",\\\"threshold_milli_direction_cost\\\":" << researchThreshold
            << ",\\\"research_only_binary\\\":true"
            << ",\\\"research_threshold_override\\\":" << (researchRaw?"true":"false")
            << ",\\\"research_input_sha256\\\":" << quote(researchInputHash)''')
    f.write_text(text)
    guards={
        'src/app/cli/CommandLine.cpp':('        requireParent("--resource-gradient-exploration", "--resource-gradient-probe");','        // RESEARCH ONLY: fixed intervals may run without the diagnostic observer.'),
        'src/app/cli/Headless.cpp':('if (one(options,"--resource-gradient-probe","false")!="true" || recordReplay)','if ((options.count("--resource-gradient-fork-field") && one(options,"--resource-gradient-probe","false")!="true") || recordReplay)'),
        'src/map/MapStep.cpp':('if (!globalContainer || !globalContainer->structuredHeadless || !gradientRuntime->qualityProbe)','if (!globalContainer || !globalContainer->structuredHeadless || (forkField>=0 && !gradientRuntime->qualityProbe))')}
    for source,(old,new) in guards.items():
        f=S/source;text=f.read_text();assert text.count(old)==1;f.write_text(text.replace(old,new))
    changes = ['src/map/gradient/AdaptiveGradientState.cpp','src/app/cli/Headless.cpp','src/app/cli/CommandLine.cpp','src/map/MapStep.cpp']
    assert set(subprocess.check_output(['git', '-C', str(S), 'diff', '--name-only'], text=True).splitlines()) == set(changes)
    (O / 'threshold-and-fixed-guards.patch').write_bytes(subprocess.check_output(['git', '-C', str(S), 'diff', '--', *changes]))
    logs = [pathlib.Path('/Users/bradley/glob2/artifacts/gradient-unified-20261019/builds/macos/integration-pass3.log'), pathlib.Path('/Users/bradley/glob2/artifacts/gradient-unified-20261019/round-003/candidate-gen003/macos-build.log')]
    lines = [line for log in logs for line in log.read_text().splitlines()]
    replacements, commands = {}, []
    for source in changes:
        target = 'build/candidate-release/' + str(pathlib.Path(source).with_suffix('.o'))
        original = shlex.split(next(line for line in reversed(lines) if ' -o ' + target + ' ' in line and ' -c ' in line))
        argv = list(original)
        obj = O / (pathlib.Path(source).stem + '.o')
        argv[argv.index('-o')+1] = str(obj)
        assert argv[-1] == source
        argv[-1] = str(S / source)
        replacements[target] = str(obj)
        commands.append(dict(original_argv=original, argv=argv, cwd=str(S), source_sha256=sha(S / source), old_object_sha256=sha(OLD / target)))
        (O / 'compile-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        with (O / (pathlib.Path(source).stem + '.log')).open('w') as log:
            run(['nice', '-n', '19', *argv], cwd=S, stdout=log, stderr=subprocess.STDOUT)
    original_link = next(shlex.split(line) for line in reversed(lines) if line.startswith('g++ -o build/candidate-release/src/glob2 '))
    link = [replacements.get(v, v) for v in original_link]
    binary = O / 'glob2'
    link[link.index('-o')+1] = str(binary)
    reused = {v: sha(OLD / v) for v in link if v.endswith(('.o', '.a')) and not pathlib.Path(v).is_absolute()}
    (O / 'reused-link-inputs.json').write_text(json.dumps(reused, indent=2) + '\n')
    (O / 'link-command.json').write_text(json.dumps(dict(original_argv=original_link, argv=link, cwd=str(OLD)), indent=2) + '\n')
    with (O / 'link.log').open('w') as log:
        run(['nice', '-n', '19', *link], cwd=OLD, stdout=log, stderr=subprocess.STDOUT)
    assert all(sha(OLD / p) == h for p, h in reused.items())
    assert sha(OLD / 'build/candidate-release/src/glob2') == ELF
    assert not subprocess.check_output(['git', '-C', str(OLD), 'status', '--porcelain'], text=True).strip()
    state.update(status='built-research-only', binary=str(binary), binary_sha256=sha(binary),
                 patch_sha256=sha(O / 'threshold-and-fixed-guards.patch'), reused_inputs=len(reused),
                 model_sha256=sha(OLD / 'tools/models/adaptive-gradient.json'),
                 header_sha256=sha(OLD / 'src/map/gradient/AdaptiveGradientModel.h'),
                 finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    save()
except BaseException:
    state.update(status='failed', error=traceback.format_exc())
    save()
    raise
