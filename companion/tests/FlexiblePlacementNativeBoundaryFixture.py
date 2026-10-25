"""Compile actual production placement hook bodies with explicit native-boundary doubles.

This tests parent/origin ownership and support-relative click conversion, not real
Minecraft server application. Run on Windows with clang-cl and VS BuildTools.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, tempfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,default=Path(tempfile.gettempdir())/'praxis-flexible-native-boundary')
parser.add_argument('--clang-cl',type=Path)
parser.add_argument('--vcvars',type=Path)
args=parser.parse_args()
root=args.out.resolve();root.mkdir(parents=True,exist_ok=True)
companion=Path(__file__).resolve().parents[1]
compiler=args.clang_cl or shutil.which('clang-cl.exe')
if not compiler: parser.error('clang-cl.exe was not found; set PATH or --clang-cl')
compiler=Path(compiler).resolve()
vcvars=args.vcvars
if not vcvars:
    installer=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    if installer.exists():
        vs=subprocess.check_output([str(installer),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
        if vs: vcvars=Path(vs)/'VC/Auxiliary/Build/vcvars64.bat'
if not vcvars or not Path(vcvars).is_file(): parser.error('VS vcvars64.bat missing; pass --vcvars')
vcvars=Path(vcvars).resolve()
s=(companion/'src/platform/FlexiblePlacement.cpp').read_text(encoding='utf-8-sig')
helper=s[s.index('enum class FlexibleTransformDecision'):s.index('LL_TYPE_INSTANCE_HOOK(FlexibleStartBuild')]
def hook(name):
    p=s.index('LL_TYPE_INSTANCE_HOOK('+name)
    p=s.index(' {',p)+2
    # brace-balanced extraction; bodies currently contain no brace characters in literals.
    depth=1;q=p
    while depth:
        if s[q]=='{':depth+=1
        elif s[q]=='}':depth-=1
        q+=1
    return s[p:q-1]
prefix=r'''
#include "FlexiblePlacementPolicy.h"
#include <atomic>
#include <map>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace praxis::companion;
using uchar=unsigned char;
struct Vec3{float x{},y{},z{};};struct Vec2{float x{},y{};};
struct BlockPos{int x{},y{},z{};bool operator==(BlockPos const&)const=default;bool operator<(BlockPos const& b)const{return std::tie(x,y,z)<std::tie(b.x,b.y,b.z);}};
struct BlockType{bool interactive{};bool isInteractiveBlock()const{return interactive;}};
struct Block{bool air=true,interactive=false;unsigned mNetworkId=7;bool isAir()const{return air;}BlockType getBlockType()const{return {interactive};}};
struct ItemStack{int mCount=64,id=3,aux=0,net=101;bool block=true;int getId()const{return id;}int getIdAux()const{return aux;}bool matchesNetIdVariant(ItemStack const& b)const{return net==b.net;}bool isNull()const{return mCount==0;}};
struct Inventory{ItemStack selected;ItemStack const& getItem(int)const{return selected;}};
struct Region{std::map<BlockPos,Block> cells;Block air{};Block const& getBlock(BlockPos p)const{auto it=cells.find(p);return it==cells.end()?air:it->second;}};
struct Player{Inventory inventory;Region region;Vec2 angles{};int slot{};Inventory& getInventory(){return inventory;}Region& getDimensionBlockSource(){return region;}Vec2 getRotation()const{return angles;}int getSelectedItemSlot()const{return slot;}float getPickRange()const{return 6;}};
using LocalPlayer=Player;
enum class HitResultType{Tile,Miss};struct HitResult{HitResultType mType=HitResultType::Tile;uchar mFacing{};BlockPos mBlock{};Vec3 mPos{};};
Player localPlayer,serverPlayer;
struct Client{Player* player=&localPlayer;HitResult hit;Player* getLocalPlayer(){return player;}HitResult const& getLatestHitResult()const{return hit;}}client;
namespace ll::service{Client* getClientInstance(){return &client;}}
enum class Feature{FlexibleBlockPlacement};enum class HandSlot{Mainhand,Offhand};struct InteractionResult{bool mSuccess{},mSwing{};};
bool enabled=true,allowed=true,restockBusy=false,occupied=false,throwOriginal=false;unsigned currentModes=FlexibleRotation;
int startOriginal=0,buildOriginal=0,useOriginal=0,vanillaSends=0,queued=0;
FlexiblePlacementPlan lastPlan{};
struct Settings{bool on(Feature)const{return enabled;}}setting;
Settings const* settings(){return &setting;}bool useAllowed(){return allowed;}unsigned heldModes(Settings const&){return currentModes;}
struct NativeHookLifetime{struct Lease{Lease(NativeHookLifetime&){}bool admitted()const{return true;}};}lifetime;
NativeHookLifetime& nativeHookLifetime(){return lifetime;}
enum class LholoPlacementState{Absent,Idle,Owned,ExternalPending,AbiUnavailable};LholoPlacementState state=LholoPlacementState::Idle;
LholoPlacementState lholoPlacementState(){return state;}
std::atomic_bool lholoNoticeShown{};void logGuiInfo(char const*){}void lholoAbiNotice(){}void admissionNotice(FlexibleAdmissionReason){}
bool blockItem(ItemStack const& i){return i.block&&i.mCount>0;}bool handRestockPlacementBusy(){return restockBusy;}namespace inventory{bool requestPathBusy(){return false;}}
FlexibleCell cellOf(BlockPos p){return {p.x,p.y,p.z};}BlockPos blockPosOf(FlexibleCell p){return {p.x,p.y,p.z};}
std::optional<FlexibleUV> relativeHitUV(Vec3 const& h,FlexibleFace f){return flexibleUVFromRelativeHit({h.x,h.y,h.z},f);}
std::optional<FlexibleUV> selectionHitUV(Vec3 const& h,FlexibleFace f,FlexibleHorizontal y){return flexibleUVFromRelativeHitForFacing({h.x,h.y,h.z},f,y);}
bool supportlessBlockAllowed(ItemStack const&){return true;}bool withinReach(Player const&,Vec3 const&){return true;}
struct FlexibleDispatchGeometry{FlexibleCell requestSupport{};bool airTarget{};FlexibleHitPoint worldHit{};};
FlexibleDispatchGeometry dispatchGeometry(FlexiblePlacementPlan const& p,FlexibleUV uv,Block const& b){auto support=b.isAir()?p.destination:p.useSupport;return {support,b.isAir(),flexibleWorldHit(support,p.placementFace,uv)};}
bool queueLholoPlacement(ItemStack const&,FlexiblePlacementPlan const& p,FlexibleUV,Block const&,bool,Player const&,unsigned){++queued;lastPlan=p;state=LholoPlacementState::ExternalPending;return true;}
InteractionResult safeFlexibleFailure(){return {false,false};}
class GameMode{public:Player& mPlayer;explicit GameMode(Player& p):mPlayer(p){}
void origin(BlockPos const& p,uchar f,HandSlot h){++startOriginal;if(throwOriginal)throw std::runtime_error("native side effect then throw");runBuild(p,f,h,false);}
bool origin(BlockPos const& p,uchar f,HandSlot h,bool){++buildOriginal;auto& i=mPlayer.inventory.selected;Vec3 hit{.5f,.5f,.5f};auto b=mPlayer.region.getBlock(p);auto result=runUse(i,p,f,hit,h,&b,true);if(result.mSuccess)++vanillaSends;return result.mSuccess;}
InteractionResult origin(ItemStack&,BlockPos const&,uchar,Vec3 const&,HandSlot,Block const*,bool){++useOriginal;if(throwOriginal)throw std::runtime_error("native use side effect then throw");return {true,true};}
void runStart(BlockPos const&,uchar,HandSlot);bool runBuild(BlockPos const&,uchar,HandSlot,bool);InteractionResult runUse(ItemStack&,BlockPos const&,uchar,Vec3 const&,HandSlot,Block const*,bool);
};
'''
# Higher-level eligibility helper may be added; extract by name if present between latestHit and macros automatically in helper above.
methods='\nvoid GameMode::runStart(BlockPos const& pos,uchar face,HandSlot hand){'+hook('FlexibleStartBuild')+'}\n'
methods+='bool GameMode::runBuild(BlockPos const& pos,uchar face,HandSlot hand,bool isSimTick){'+hook('FlexibleBuildBlock')+'}\n'
methods+='InteractionResult GameMode::runUse(ItemStack& item,BlockPos const& pos,uchar face,Vec3 const& hit,HandSlot hand,Block const* target,bool first){'+hook('FlexibleUseOn')+'}\n'
main=r'''
int main(){int checks=0,failed=0;auto ck=[&](bool v,char const* msg){++checks;if(!v){++failed;std::cout<<"FAIL "<<msg<<" starts="<<startOriginal<<" builds="<<buildOriginal<<" uses="<<useOriginal<<" vanilla="<<vanillaSends<<" queues="<<queued<<'\n';}};
GameMode local{localPlayer},server{serverPlayer};BlockPos pos{-23,64,117};
auto reset=[&](uchar face){enabled=allowed=true;restockBusy=throwOriginal=false;currentModes=FlexibleRotation;state=LholoPlacementState::Idle;localPlayer=Player{};serverPlayer=Player{};client.player=&localPlayer;Vec3 hit{.5f,.5f,.5f};if(face<2)hit.y=face;else if(face<4)hit.z=face-2;else hit.x=face-4;client.hit={HitResultType::Tile,face,pos,{pos.x+hit.x,pos.y+hit.y,pos.z+hit.z}};localPlayer.region.cells[pos]={false,false,12};serverPlayer.region.cells[pos]={false,false,12};startOriginal=buildOriginal=useOriginal=vanillaSends=queued=0;};
for(unsigned mode:{unsigned(FlexibleRotation),unsigned(FlexibleOffset),unsigned(FlexibleAdjacent),unsigned(FlexibleRotation|FlexibleOffset)})for(unsigned f=0;f<6;++f){reset(uchar(f));currentModes=mode;local.runStart(pos,uchar(f),HandSlot::Mainhand);ck(queued==1,"real start hook queues intended transformed action away from origin");ck(startOriginal==0&&buildOriginal==0&&useOriginal==0&&vanillaSends==0,"native parent cannot send original-coordinate duplicate");local.runBuild(pos,uchar(f),HandSlot::Mainhand,false);ck(queued==1&&vanillaSends==0,"pending request prevents repeat parent duplicate");}
reset(3);enabled=false;local.runStart(pos,3,HandSlot::Mainhand);ck(startOriginal==1&&buildOriginal==1&&useOriginal==1&&vanillaSends==1&&queued==0,"disabled native hierarchy passes once");
reset(3);server.runStart(pos,3,HandSlot::Mainhand);ck(startOriginal==1&&vanillaSends==1&&queued==0,"integrated server identity not hijacked");
reset(3);local.runStart(pos,3,HandSlot::Offhand);ck(startOriginal==1&&vanillaSends==1&&queued==0,"offhand hierarchy not converted to mainhand");
reset(3);enabled=false;throwOriginal=true;try{local.runStart(pos,3,HandSlot::Mainhand);}catch(...){}ck(startOriginal==1,"original exception never retried");
reset(3);restockBusy=true;local.runStart(pos,3,HandSlot::Mainhand);ck(queued==0&&vanillaSends==0,"blocked transform never falls back to ordinary placement");
std::cout<<"SUPERVISOR_PARENT_PRODUCTION_BODY checks="<<checks<<" failures="<<failed<<"; parent/SDK boundary model, live game NOT_RUN\n";return failed?1:0;
}
'''
(root/'supervisor_parent_fixture.cpp').write_text(prefix+helper+methods+main,encoding='utf-8')
cmd=f'''@echo off
call "{vcvars}" >nul
if errorlevel 1 exit /b 2
cd /d "{root}"
"{compiler}" /nologo /std:c++20 /EHsc /utf-8 /I"{companion / 'src/core'}" supervisor_parent_fixture.cpp /Fosupervisor_parent_fixture.obj /Fesupervisor_parent_fixture.exe
if errorlevel 1 exit /b 2
supervisor_parent_fixture.exe
exit /b %errorlevel%
'''
(root/'supervisor_parent_fixture.cmd').write_text(cmd,encoding='utf-8')
r=subprocess.run(['cmd.exe','/d','/c',str(root/'supervisor_parent_fixture.cmd')],cwd=root,capture_output=True)
(root/'supervisor-parent-current.log').write_bytes(r.stdout+r.stderr)
(root/'supervisor-parent-current.json').write_text(json.dumps({'actual_source_sha256':hashlib.sha256(s.encode()).hexdigest(),'exit':r.returncode,'live_game':False},indent=2))
print((r.stdout+r.stderr).decode('utf-8',errors='replace'));print('PARENT_FIXTURE_EXIT='+str(r.returncode))

raise SystemExit(r.returncode)
