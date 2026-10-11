import hashlib,json,platform,shutil,subprocess
from datetime import datetime,timezone,timedelta
from pathlib import Path

ROOT=Path('D:/vn-sim')
LOCAL=ROOT/'build/native-metal-p2c-c2b-baseline'
ARCHIVE=ROOT/'docs/native-metal-p2c-c2b-baseline'
REPOS={'main':ROOT,'runtime':ROOT/'Engine/KRKRRuntime/Source','core':ROOT/'Engine/KRKRRuntime/Source/cpp'}
def git(repo,*args):
    return subprocess.run(['git','-C',str(repo),*args],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE).stdout
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def info(path):return {'path':path.relative_to(ROOT).as_posix(),'bytes':path.stat().st_size,'sha256':digest(path)}
before=json.loads((LOCAL/'before.json').read_text(encoding='utf-8'))
heads={name:git(repo,'rev-parse','HEAD').decode().strip() for name,repo in REPOS.items()}
assert heads==before['heads']
for name,value in before['inputLogHashes'].items():assert digest(ROOT/name)==value
for name,folder in [('layer','metal-layer-tests'),('backend','metal-render-tests'),('tjs','tjs-shutdown-tests')]:
    shutil.copyfile(ROOT/'build'/folder/'Testing/Temporary/LastTest.log',LOCAL/(name+'.after.last-test.log'))
ARCHIVE.mkdir(parents=True,exist_ok=True)
evidence={}
for source in sorted(LOCAL.iterdir()):
    if source.suffix=='.log' or source.name in {'before.json','plan.before.md','check-production-syntax.py','archive-evidence.py'} or '.before.' in source.name:
        target=ARCHIVE/source.name;shutil.copyfile(source,target);evidence[target.name]=info(target)
diffs,statuses,snapshots={},{},{}
for name,repo in REPOS.items():
    patch=LOCAL/(name+'.after.patch')
    patch.write_bytes(git(repo,'diff','--binary','HEAD','--','.',':(exclude)docs/native-metal-p2c-c2b-baseline/**'))
    diffs[name]=info(patch)
    status=LOCAL/(name+'.after.status');status.write_bytes(git(repo,'status','--short'));statuses[name]=info(status)
    git(repo,'diff','--check')
    names=git(repo,'ls-files','-m','-z').split(b'\0')+git(repo,'ls-files','--others','--exclude-standard','-z').split(b'\0')
    for raw in sorted(set(names)):
        if not raw:continue
        relative=raw.decode('utf-8')
        if name=='main' and relative.startswith('docs/native-metal-p2c-c2b-baseline/'):continue
        source=repo/relative
        if not source.is_file():continue
        target=LOCAL/'snapshots'/name/relative
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        snapshots[name+':'+relative]=info(target)
old=json.loads((LOCAL/'c2a-analysis.json').read_text(encoding='utf-8'))
old_summary=[{'file':r['file'],'sha256':r['sha256'],'windows':r['coverage']['selectedWorkWindows'],
    'issues':len(r['issues']),'layerSpanVersion':r['layerSpans']['version']} for r in old]
assert sum(r['windows'] for r in old_summary)==177 and all(r['issues']==0 for r in old_summary)
manifest={'schemaVersion':1,'stage':'P2C C2B','date':'2026-10-09','startedDate':before['date'],
    'archivedAtLocal':datetime.now(timezone(timedelta(hours=8))).isoformat(),'archivedAtUTC':datetime.now(timezone.utc).isoformat(),
    'heads':heads,'platform':platform.platform(),'compiler':'MinGW GCC 13.2','deviceDouble':True,'nativeMetal':False,
    'realPlutovgCPU':True,'skip':0,'committed':False,'pushed':False,
    'before':'Three repositories clean; original plan, empty diff/status and historical LastTest outputs saved before edits. Historical outputs are not fresh pre-change builds.',
    'originalPlanSHA256':digest(LOCAL/'plan.before.md'),'inputLogHashes':before['inputLogHashes'],
    'buildTypes':{'metalLayer':'Release / Ninja','metalBackend':'unspecified / MinGW Makefiles','tjsShutdown':'unspecified / Ninja'},
    'environment':{'TEMP':'D:/vn-sim/build/tmp','TMP':'D:/vn-sim/build/tmp','CXX':'C:/Strawberry/c/bin/c++.exe',
        'python':'C:/Users/Ayach/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'},
    'commands':[
        'cmake -S Tests/MetalLayer -B build/metal-layer-tests -DPLUTOVG_ORACLE_SOURCE_DIR=D:/vn-sim/build/plutovg-c2b/plutovg-1.3.3',
        'cmake --build build/metal-layer-tests --parallel 6','ctest --test-dir build/metal-layer-tests --output-on-failure',
        'ctest --test-dir build/metal-layer-tests -R plutovg-exact-capture --output-on-failure --verbose',
        'cmake --build build/metal-render-tests --parallel 6','ctest --test-dir build/metal-render-tests --output-on-failure',
        'cmake --build build/tjs-shutdown-tests --parallel 6','ctest --test-dir build/tjs-shutdown-tests --output-on-failure',
        'python Tests/MetalLayer/test-layer-diagnostics.py','python scripts/check-krkr-frame-times.py',
        'python scripts/check-krkr-point-read-trace.py','python build/native-metal-p2c-c2b-baseline/check-production-syntax.py',
        'c++ -std=c++17 -Wall -Wextra -Werror -I Engine/KRKRRuntime/Source/cpp/core/render Tests/MetalLayer/C2ConsumerTraceTests.cpp build/native-metal-p2c-c2a-baseline/c2a-helper-main.cpp -o build/native-metal-p2c-c2b-baseline/c2b-trace-strict.exe',
        'build/native-metal-p2c-c2b-baseline/c2b-trace-strict.exe',
        'python scripts/analyze-layer-diagnostics.py <three After_P2C_C2A JSONL paths> --output build/native-metal-p2c-c2b-baseline/c2a-analysis.json'],
    'tests':{'metalLayer':'5/5','metalBackend':'1/1','tjsShutdown':'1/1','parserFixtures':34,
        'plutovgOracle':'6 real upstream CPU/capture scenes; 730434 pixels; full byte equality; FNV64 diagnostic 17800086430293495788',
        'spanMath':'2359296 coverage/source-alpha/arbitrary-packed comparisons plus edge/CSR/budget cases',
        'productionBinding':'3 methods, exact pixels and typed result, result constructors/throws once, pre/postcommit, COW/lease/clip/record/borrowed/escaped aliases',
        'facade':'device-double; residency/counters/diagnostic toggle/dirty ROI/reject/cache invalidation',
        'strictDiagnosticHelper':'PASS C++17 Wall Wextra Werror','frameTiming':'PASS','pointTrace':'PASS debug/release',
        'productionSyntax':['core/script/tjsNativeLayer.cpp','plugins/LayerExBase.cpp','plugins/LayerExDraw/LayerExDraw.cpp'],
        'surfaceComparisons':78634,'sessions':3,'C4ExactCases':1962,'C1ExactComparisons':60,
        'oldC2AAnalysis':old_summary},
    'failuresPreserved':['Dense all-cap fixture exceeded 512 row references; split independent calls without changing budget',
        'Test dependency ODR collision produced CPU no-acquire mismatch; renamed test types, no production pixel changes',
        'Multiple scalar definitions at link; extracted helper functions inline',
        'Analyzer initially received a directory instead of paths; retried with explicit JSONL files'],
    'pending':['Apple Objective-C++/MSL and real GPU ordering','default/forced compute, device/simulator and App',
        'Native diagnostic toggle submit/wait and synchronous save/error behavior','New matching device logs and 3-repeat controlled pairs',
        'Remaining C2 domains; C3/P2D/9-nine geometry+blur/async saving outside this batch'],
    'evidence':evidence,'localDiffs':diffs,'localStatuses':statuses,'localSourceSnapshots':snapshots}
(ARCHIVE/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('C2B evidence archived:',len(evidence),'files;',len(snapshots),'source snapshots; heads unchanged; logs unchanged')
