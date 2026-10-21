#include "resourcepacks/PackPlan.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion::packs;
int main(){
 unsigned checks{};
 auto check=[&](bool ok,char const* why){++checks;if(!ok)throw std::runtime_error(why);};
 try{
  Catalog c;c.complete=true;c.generation=7;
  c.packs={{"a","A","ua","1.0.0","","textures",true},
           {"b","B","ub","1.0.0","","textures",true},
           {"c","C","uc","1.0.0","","textures",true},
           {"font","Font","uf","1.0.0","","font",false}};
  Selection s;s.reconcile(c);check(s.enable(c,"a",true)&&s.enable(c,"b",true),"candidate selection");
  auto before=s.order;
  AppliedStackView unknown;auto p=makePackPlan(c,s,unknown,false);
  check(p.validation==ApplyResult::Valid&&!p.currentKnown,"valid candidate is not a current snapshot");
  check(p.proposed.size()==2&&p.proposed[0].change==ProposalChange::Unknown&&p.disabledCandidates.empty(),"unknown is not a verified empty or disabled stack");
  check(!appliedTo(c.packs[0],unknown).has_value()&&!p.canApply(),"no active-state claim or Apply for unknown");
  AppliedStackView empty;empty.known=true;p=makePackPlan(c,s,empty,false);
  check(appliedTo(c.packs[0],empty)==false&&p.proposed[0].change==ProposalChange::Enable,"verified empty is distinct from unknown");
  AppliedStackView current{true,{{"ub","1.0.0",""},{"ua","1.0.0",""},{"uc","1.0.0",""},
                              {"uf","1.0.0",""},{"server-only","4.0.0",""},{"ua","1.0.0","sub"}}};
  auto currentBefore=current.order;p=makePackPlan(c,s,current,false);
  check(p.proposed[0].change==ProposalChange::Keep&&p.priorityChanged,"existing enable state and order change shown separately");
  check(p.disabledCandidates==std::vector<std::string>{"c"},"only selectable catalog identities get disable proposals");
  check(p.outsideScope.size()==3,"font, unknown and subpack remain out of proposal scope");
  check(!matchesApplied(c.packs[0],{"ua","2.0.0",""})&&!matchesApplied(c.packs[0],{"ua","1.0.0","sub"}),"version/subpack do not alias ordinary pack");
  check(!matchesApplied(c.packs[0],{"ua","1.0.0","",3}),"copy-protected identity cannot alias an ordinary texture by UUID/version");
  AppliedStackView protectedCurrent{true,{{"ua","1.0.0","",3}}};auto protectedPlan=makePackPlan(c,s,protectedCurrent,false);
  check(protectedPlan.outsideScope.size()==1&&protectedPlan.disabledCandidates.empty(),"protected pack remains outside proposal disable/order edits");
  check(!p.canApply()&&s.order==before&&current.order.size()==currentBefore.size(),"projection is nonmutating and fixture identity never binds native backend");
  check(s.move("b",-1),"proposal reorder");p=makePackPlan(c,s,current,false);
  check(!p.priorityChanged,"unchanged relative order of retained packs");
  s.order.clear();p=makePackPlan(c,s,current,false);
  check(p.disabledCandidates.size()==3&&p.outsideScope.size()==3&&!p.canApply(),"empty proposal preserves unmanaged identities and remains disabled");
  p=makePackPlan(c,s,current,true);check(p.validation==ApplyResult::Busy&&p.proposed.empty()&&!p.canApply(),"busy invalidates projection");
  s.generation=6;p=makePackPlan(c,s,current,false);check(p.validation==ApplyResult::StaleCatalog&&!p.canApply(),"stale candidate refused");
  s.generation=7;s.order={"font"};p=makePackPlan(c,s,current,false);check(p.validation==ApplyResult::UnsupportedPack&&p.proposed.empty(),"unsupported candidate not presented as ready");
  s.order={"a","a"};p=makePackPlan(c,s,current,false);check(p.validation==ApplyResult::UnsupportedPack&&!p.canApply(),"duplicate rejected");
  check(std::string(packReasonJa("Protected content is excluded"))!="Protected content is excluded","protected reason translated");
  check(std::string(packReasonJa("future reason"))=="future reason","unknown reason not hidden");
  std::cout<<"PackPlan "<<checks<<" checks PASS; current fixtures are not native runtime evidence\n";return 0;
 }catch(std::exception const& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
