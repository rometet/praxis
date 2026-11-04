#include "resourcepacks/Title2660Receipt.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
using praxis::companion::packs::Title2660Receipt;
using Phase=Title2660Receipt::Phase;
using Poll=Title2660Receipt::Poll;
namespace {
int checks{};
void check(bool v,const char* msg){++checks;if(!v){std::fprintf(stderr,"FAILED: %s\n",msg);std::exit(1);}}
std::vector<std::string> old() {return {"resource/A/1/default","resource/B/2/"};}
std::vector<std::string> changed(){return {"resource/B/2/","resource/A/1/default"};}
Title2660Receipt newTransaction(){
    Title2660Receipt r;check(r.prepare(77,0x5678,old(),changed(),200),"prepare exact global diff");
    return r;
}
void nativeApply(Title2660Receipt& r,bool includeFinish=true){
    check(r.beginNativeCall(0x5678,11),"begin bound native call");
    r.setterResult(0x5678,true);
    r.onActive(0x5678);
    if(includeFinish)r.onFinish(0x5678);
}
}
int main(){
    {
        Title2660Receipt r;
        check(!r.prepare(0,99,old(),changed(),99),"zero token refused");
        check(!r.prepare(1,0,old(),changed(),99),"zero manager refused");
        check(!r.prepare(1,99,old(),old(),99),"no-op refused");
        check(!r.prepare(1,99,{},changed(),99),"no original snapshot refused");
        check(!r.prepare(1,99,old(),{},99),"no target snapshot refused");
        check(r.prepare(1,99,old(),changed(),99),"proper prepare");
        check(!r.prepare(2,99,old(),changed(),100),"one in flight");
        check(r.cancelBeforeSetter(),"cancel before mutation");
        check(!r.beginNativeCall(99,5),"cannot begin cancelled mutation");
    }
    {
        auto r=newTransaction();
        check(!r.beginNativeCall(0x9876,15),"wrong manager cannot mutate");
        check(r.phase()==Phase::RollbackRequired,"owner lost before setter fails closed");
    }
    {
        auto r=newTransaction();
        check(r.beginNativeCall(0x5678,11),"admit");
        r.setterResult(0x5678,false);
        check(r.phase()==Phase::Rejected,"rejected setter leaves original state");
        check(!r.endNativeCall(0x5678,12),"rejected setter cannot settle");
    }
    {
        auto r=newTransaction();nativeApply(r);
        check(r.endNativeCall(0x5678,12),"matching active/finish within native scope");
        check(r.poll(0x5678,1,20,changed(),true,false,true)==Poll::Wait,"one matching tick insufficient");
        check(r.poll(0x5678,1,20,changed(),true,false,true)==Poll::Wait,"same tick not independent");
        check(r.poll(0x5678,2,25,changed(),true,false,true)==Poll::Applied,"two distinct matching ticks complete");
        check(r.phase()==Phase::Complete,"complete only after exact ordering");
    }
    {
        auto r=newTransaction();nativeApply(r,false);
        check(!r.endNativeCall(0x5678,15),"missing native finish cannot settle");
        check(r.phase()==Phase::RollbackRequired,"finish missing requires rollback");
        check(r.beginRollback(0x5678,210),"new rollback deadline");
        nativeApply(r);
        check(r.endNativeCall(0x5678,18),"rollback native completion");
        check(r.poll(0x5678,3,20,old(),true,false,true)==Poll::Wait,"restore first reading");
        check(r.poll(0x5678,4,21,old(),true,false,true)==Poll::Restored,"restore exact original");
    }
    {
        auto r=newTransaction();check(r.beginNativeCall(0x5678,11),"admit");
        r.setterResult(0x5678,true);
        r.onFinish(0x5678);
        check(r.phase()==Phase::RollbackRequired,"out-of-order finish is rejected");
    }
    {
        auto r=newTransaction();nativeApply(r);
        r.onActive(0x5678);
        check(r.phase()==Phase::RollbackRequired,"duplicate native callback rejected");
    }
    {
        auto r=newTransaction();nativeApply(r);
        check(r.endNativeCall(0x5678,12),"admitted");
        check(r.poll(0x5678,1,20,old(),true,false,true)==Poll::Wait,"first mismatch needs confirmation");
        check(r.poll(0x5678,2,21,old(),true,false,true)==Poll::NeedRollback,"second mismatch requires rollback");
    }
    {
        auto r=newTransaction();nativeApply(r);check(r.endNativeCall(0x5678,12),"admit");
        check(r.poll(0x5678,1,20,changed(),true,true,true)==Poll::Wait,"pending restack not done");
        check(r.poll(0x5678,1,21,changed(),true,false,true)==Poll::Wait,"only one settled tick");
        check(r.poll(0x5678,2,22,changed(),true,false,false)==Poll::NeedRollback,"other native stack changed");
    }
    {
        auto r=newTransaction();nativeApply(r);check(r.endNativeCall(0x5678,12),"admit");
        check(r.poll(0x9876,1,20,changed(),true,false,true)==Poll::NeedRollback,"owner mismatch fails closed");
    }
    {
        auto r=newTransaction();nativeApply(r);check(r.endNativeCall(0x5678,12),"admit");
        check(r.poll(0x5678,1,201,changed(),true,false,true)==Poll::NeedRollback,"expired receipt fails closed");
    }
    {
        auto r=newTransaction();nativeApply(r);check(r.endNativeCall(0x5678,12),"admit");
        check(r.poll(0x5678,1,20,old(),true,false,true)==Poll::Wait,"wrong first snapshot");
        check(r.poll(0x5678,2,21,changed(),true,false,true)==Poll::Wait,"matching after mismatch is not sufficient");
        check(r.poll(0x5678,3,22,changed(),true,false,true)==Poll::Wait,"fresh matching first tick");
        check(r.poll(0x5678,4,23,changed(),true,false,true)==Poll::Applied,"matching subsequent tick grants completion");
    }
    {
        auto r=newTransaction();nativeApply(r);check(r.endNativeCall(0x5678,12),"admit");
        check(r.poll(0x5678,1,20,changed(),false,false,true)==Poll::NeedRollback,"title transition fails closed");
    }
    std::printf("Title2660ReceiptTests checks=%d PASS\n",checks);
}
