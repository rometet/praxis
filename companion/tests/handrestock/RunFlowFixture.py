"""Run complete, extracted production capture/tick bodies with local host doubles."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT=Path(__file__).resolve().parents[3]
MSVC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207')
SDK=Path('C:/Program Files (x86)/Windows Kits/10')

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def function(text, marker):
    start=text.index(marker);opening=text.index('{',start);depth=1;i=opening+1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[start:i]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--out',required=True);parser.add_argument('--baseline',action='store_true');parser.add_argument('--source-commit');args=parser.parse_args()
    out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'companion/src/platform/HandRestock.cpp'
    raw=subprocess.check_output(['git','-C',str(ROOT),'show',args.source_commit+':companion/src/platform/HandRestock.cpp']) if args.source_commit else source.read_bytes()
    text=raw.decode('utf-8-sig')
    prefix=text[text.index('namespace praxis::companion {'):text.index('LL_TYPE_INSTANCE_HOOK(RestockHudCapture')]
    drop=function(text,'LL_TYPE_INSTANCE_HOOK(RestockDropCancel')
    drop=drop[drop.index('{'):].replace('*this','player').replace('==this','==&player').replace('origin(item,randomly)','true')
    body=prefix+'}\n'+function(text,'void tickHandRestock(')+'\n'+function(text,'void resetHandRestock(')+'\n'+function(text,'void serviceHandRestock(')+'\nbool invokeActualDrop(Player& player) '+drop+'\n}\n'
    generated=out/'ActualHandRestockFlow.inc';generated.write_text(body,encoding='utf-8')
    env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[MSVC/'include']+[SDK/'Include/10.0.26100.0'/x for x in ('ucrt','shared','um','winrt')]))
    env['LIB']=';'.join(map(str,[MSVC/'lib/x64',SDK/'Lib/10.0.26100.0/ucrt/x64',SDK/'Lib/10.0.26100.0/um/x64']))
    env['PATH']=str(MSVC/'bin/HostX64/x64')+';'+env['PATH'];env['TEMP']=env['TMP']=str(out)
    exe=out/'HandRestockFlowTests.exe';compiler=Path('E:/rufu client/.tools/llvm-22.1.8/bin/clang-cl.exe')
    command=[str(compiler),'/nologo','/std:c++20','/EHsc','/MD','/O2','/utf-8','/I'+str(ROOT/'companion/src'),'/I'+str(Path(__file__).parent),'/I'+str(out)]
    if 'bool completed' in text:command+=['/DHAS_COMPLETION_CAPTURE']
    command+=[str(Path(__file__).with_name('FlowTests.cpp')),'/Fo'+str(out/'FlowTests.obj'),'/Fe'+str(exe)]
    build=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace');(out/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:print(build.stdout+build.stderr);return build.returncode
    run=subprocess.run([str(exe)]+(['baseline'] if args.baseline else []),cwd=out,env=env,capture_output=True,text=True)
    (out/'run.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout+run.stderr)
    (out/'RESULTS.json').write_text(json.dumps({'actual_source':str(source),'source_commit':args.source_commit,'actual_source_sha256':hashlib.sha256(raw).hexdigest(),'extracted_body_sha256':sha(generated),'fixture_source_sha256':{p.name:sha(p) for p in (Path(__file__),Path(__file__).with_name('FlowHarness.h'),Path(__file__).with_name('FlowTests.cpp'))},'command':command,'compile_exit':build.returncode,'run_exit':run.returncode,'output':run.stdout+run.stderr,'native_boundaries':'host doubles','Minecraft':'NOT_RUN','runtimeResult':'not-tested'},indent=2),encoding='utf-8')
    return run.returncode

if __name__=='__main__':raise SystemExit(main())
