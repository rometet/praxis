"""F1/RT03/RT09: compile production methods with explicit platform test doubles.

renderFrame's admission prefix is compiled verbatim, up to its first visibility
check. GPU/Win32/loader functions are injected; no native hooks are installed.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--source-root',type=Path,default=ROOT)
parser.add_argument('--output',type=Path,default=ROOT/'build/approved-regressions')
parser.add_argument('--case',choices=['F1','RT03_RT09'])
args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
vs=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC')
msvc=sorted(vs.iterdir())[-1]; sdk=Path('C:/Program Files (x86)/Windows Kits/10')
version=sorted((sdk/'Include').iterdir())[-1].name
env=os.environ.copy(); env['PATH']=str(msvc/'bin/Hostx64/x64')+os.pathsep+env['PATH']
env['INCLUDE']=os.pathsep.join(map(str,[msvc/'include',sdk/'Include'/version/'ucrt']))
env['LIB']=os.pathsep.join(map(str,[msvc/'lib/x64',sdk/'Lib'/version/'ucrt/x64',sdk/'Lib'/version/'um/x64']))

def function(path,signature):
    s=(args.source_root/path).read_text(encoding='utf-8'); start=s.index(signature)
    opening=s.index('{',start); depth,end=1,opening+1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[start:end]

host='src/gui/StandaloneGuiHost.cpp'
cases={}
if args.case in [None,'F1']:
    cases['F1']=(ROOT/'tests/approved/ModLifecycle.cpp.in').read_text(encoding='utf-8').replace('@MOD_CLASS@',function('src/Mod.cpp','class CompanionMod')+';')
if args.case in [None,'RT03_RT09']:
    render=function(host,'void StandaloneGuiHost::renderFrame(')
    render=render[:render.index('    if (!visible()')]+ '\n}'
    cases['RT03_RT09']=(ROOT/'tests/approved/StandaloneRetirement.cpp.in').read_text(encoding='utf-8').replace('@METHODS@','\n'.join([
        function(host,'bool StandaloneGuiHost::stop('),function(host,'bool StandaloneGuiHost::installWndProc('),
        function(host,'bool StandaloneGuiHost::releaseGraphics('),function(host,'void StandaloneGuiHost::handlePresentResult('),
        function(host,'HRESULT STDMETHODCALLTYPE StandaloneGuiHost::resize('),
        function(host,'HRESULT STDMETHODCALLTYPE StandaloneGuiHost::resize1('),render]))
results=[]
for name,code in cases.items():
    cpp=args.output/(name+'.cpp'); cpp.write_text(code,encoding='utf-8'); exe=args.output/(name+'.exe')
    build=subprocess.run([str(msvc/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++20','/EHsc','/W4','/WX',str(cpp),'/Fe:'+str(exe),'/Fo:'+str(args.output/(name+'.obj'))],cwd=args.output,env=env,capture_output=True,text=True,errors='replace')
    (args.output/(name+'-build.log')).write_text(build.stdout+build.stderr,encoding='utf-8')
    run=None if build.returncode else subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
    output=build.stdout+build.stderr if run is None else run.stdout+run.stderr
    (args.output/(name+'.log')).write_text(output,encoding='utf-8')
    results.append(dict(name=name,buildExit=build.returncode,runExit=None if run is None else run.returncode,generatedSha256=hashlib.sha256(cpp.read_bytes()).hexdigest()))
    print(name,output.strip(),flush=True)
(args.output/'results.json').write_text(json.dumps(dict(sourceRoot=str(args.source_root),sources={p:hashlib.sha256((args.source_root/p).read_bytes()).hexdigest() for p in ['src/Mod.cpp',host,'src/gui/StandaloneGuiHost.h']},results=results),indent=2),encoding='utf-8')
raise SystemExit(0 if all(r['buildExit']==0 and r['runExit']==0 for r in results) else 1)
