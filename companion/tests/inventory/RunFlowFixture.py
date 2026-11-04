"""Extract current production job/lease bodies; run only a local host fixture."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
ROOT=Path(__file__).resolve().parents[3]
MSVC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
SDK=Path('C:/Program Files (x86)/Windows Kits/10')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def function(text,marker):
    start=text.index(marker);opening=text.index('{',start);depth=1;i=opening+1
    while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[start:i]
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',required=True);args=p.parse_args()
    out=Path(args.out).resolve();assert out.is_relative_to(ROOT);out.mkdir(parents=True,exist_ok=True)
    request=ROOT/'companion/src/platform/InventoryRequest.cpp';actions=ROOT/'companion/src/platform/InventoryActions.cpp'
    req=request.read_text(encoding='utf-8');act=actions.read_text(encoding='utf-8')
    globals=req[req.index('std::mutex mutex;'):req.index('ItemStackNetManagerClient* currentManager')]
    body='namespace praxis::companion::inventory { namespace {\n'+globals+'}\n'
    for marker in ['std::uint64_t beginRequest(', 'void finishRequest(', 'RequestResult requestResult(', 'void releaseRequest(', 'void resetRequests(']:body+=function(req,marker)+'\n'
    body+='}\n'+act[act.index('namespace praxis::companion {'):act.index('bool foreground()')]
    for marker in ['void stop(', 'std::vector<StackValue> flatten(', 'Address const* address(', 'bool predict(', 'Snapshot liveSnapshot(', 'void tick(']:body+=function(act,marker)+'\n'
    body+='}\n}\n';generated=out/'ActualInventoryFlow.inc';generated.write_text(body,encoding='utf-8')
    env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[MSVC/'include']+[SDK/'Include/10.0.26100.0'/x for x in ('ucrt','shared','um','winrt')]))
    env['LIB']=';'.join(map(str,[MSVC/'lib/x64',SDK/'Lib/10.0.26100.0/ucrt/x64',SDK/'Lib/10.0.26100.0/um/x64']));env['PATH']=str(MSVC/'bin/HostX64/x64')+';'+env['PATH'];env['TEMP']=env['TMP']=str(out)
    compiler=Path('E:/rufu client/.tools/llvm-22.1.8/bin/clang-cl.exe');exe=out/'InventoryFlowTests.exe'
    command=[str(compiler),'/nologo','/std:c++20','/EHsc','/MD','/O2','/utf-8','/I'+str(ROOT/'companion/src'),'/I'+str(Path(__file__).parent),'/I'+str(out),str(Path(__file__).with_name('FlowTests.cpp')),'/Fo'+str(out/'FlowTests.obj'),'/Fe'+str(exe)]
    build=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace');(out/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    run=subprocess.run([str(exe)],cwd=out,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace') if not build.returncode else None
    output=(run.stdout+run.stderr) if run else build.stdout+build.stderr;(out/'run.log').write_text(output,encoding='utf-8');print(output)
    result={'source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),'actual_source_sha256':{str(s.relative_to(ROOT)):sha(s) for s in [request,actions]},'extracted_body_sha256':sha(generated),'fixture_source_sha256':{s.name:sha(s) for s in [Path(__file__),Path(__file__).with_name('FlowHarness.h'),Path(__file__).with_name('FlowTests.cpp')]},'command':command,'compile_exit':build.returncode,'run_exit':run.returncode if run else None,'output':output,'native_boundaries':'host doubles','Minecraft':'NOT_RUN','runtimeResult':'not-tested'}
    (out/'RESULTS.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');return run.returncode if run else build.returncode
if __name__=='__main__':raise SystemExit(main())
