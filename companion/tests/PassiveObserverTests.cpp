#define PRAXIS_PASSIVE_OBSERVER_LIFECYCLE_TEST
#include "resourcepacks/PassiveReloadObserver.cpp"
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool value){++checks;if(!value){std::cerr<<"FAIL observer lifecycle check "<<checks<<'\n';std::exit(1);}}
void reset(){
    check(installers==0);
    fixtureLifetime=std::make_unique<praxis::companion::NativeHookLifetime>();
    installAttempted=false;observerStopping=false;passiveObserversInstalled=false;
    fixtureGuard=false;fixturePin=true;fixtureThrow=false;fixtureFail=-1;fixtureBlock=-1;
    fixtureGuards=0;fixturePins=0;fixtureHooks=0;fixtureStops=0;fixtureEnabled=false;fixtureRegistration=0;
    fixtureEntered=false;fixtureRelease=false;
}
void offAndUnknown(){
    reset();installPassiveReloadObserver();
    check(fixtureGuards==1&&fixturePins==0&&fixtureHooks==0&&!lifetime.admitted());
    check(fixtureRegistration==0);
    installPassiveReloadObserver();check(fixtureGuards==1&&fixtureHooks==0);
    check(stopPassiveReloadObserver());check(installers==0&&!fixtureEnabled);
    reset();check(stopPassiveReloadObserver());fixtureGuard=true;installPassiveReloadObserver();
    check(fixtureGuards==0&&fixturePins==0&&fixtureHooks==0&&!lifetime.admitted());
}
void successAndInflight(){
    reset();fixtureGuard=true;installPassiveReloadObserver();
    check(fixtureGuards==1&&fixturePins==1&&fixtureHooks==10&&passiveObserversInstalled);
    check(lifetime.admitted()&&lifetime.retained()&&fixtureEnabled);
    check(fixtureRegistration==2);
    installPassiveReloadObserver();check(fixtureHooks==10);
    {
        praxis::companion::NativeHookLifetime::Lease lease(lifetime);check(lease.admitted());
        check(!stopPassiveReloadObserver());check(!lease.admitted());
        check(!fixtureEnabled&&lifetime.retained());
    }
    check(stopPassiveReloadObserver());check(lifetime.retained());
    installPassiveReloadObserver();check(fixtureHooks==10&&!lifetime.admitted());
}
void registrationFailures(){
    for(int fail=0;fail<10;++fail)for(bool exception:{false,true}){
        reset();fixtureGuard=true;fixtureFail=fail;fixtureThrow=exception;
        installPassiveReloadObserver();
        check(fixtureHooks==unsigned(fail+1));check(lifetime.retained());
        check(!lifetime.admitted()&&!passiveObserversInstalled&&!fixtureEnabled);
        check(fixtureRegistration==3);
        check(stopPassiveReloadObserver());check(lifetime.retained());
    }
    reset();fixtureGuard=true;fixturePin=false;installPassiveReloadObserver();
    check(fixtureHooks==0&&!lifetime.retained()&&!lifetime.admitted()&&!fixtureEnabled);
    check(fixtureRegistration==3);
    check(stopPassiveReloadObserver());
}
void stopDuringRegistration(){
    reset();fixtureGuard=true;fixtureBlock=3;
    std::thread installer([]{installPassiveReloadObserver();});
    {
        std::unique_lock lock(fixtureMutex);
        fixtureCondition.wait(lock,[]{return fixtureEntered;});
    }
    check(installers==1&&fixtureHooks==4&&lifetime.retained());
    // No registration mutex is held: stop returns while the hook boundary is
    // deliberately still blocked, retaining resources for this installer.
    check(!stopPassiveReloadObserver());check(!lifetime.admitted()&&!fixtureEnabled);
    {std::lock_guard lock(fixtureMutex);fixtureRelease=true;}fixtureCondition.notify_all();installer.join();
    check(installers==0&&fixtureHooks==10&&!lifetime.admitted()&&!passiveObserversInstalled);
    check(stopPassiveReloadObserver());check(lifetime.retained());
}
}
int main(){
    offAndUnknown();successAndInflight();registrationFailures();
    for(unsigned i=0;i<100;++i)stopDuringRegistration();
    std::cout<<"PassiveObserver actual install/stop bodies checks="<<checks<<" PASS; OS/SDK registration doubled; game NOT_RUN\n";
}
