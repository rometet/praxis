#include "resourcepacks/WorldPackParticipant.h"
extern "C" __declspec(dllexport) int praxis_resource_reload_v4(
    praxis::companion::packs::WorldPackReloadCommand command,
    praxis::companion::packs::WorldPackReloadRequest const* request,
    praxis::companion::packs::WorldPackReloadReply* reply) noexcept {
    return praxis::companion::packs::praxisWorldParticipant().command(command,request,reply);
}

// Older coordinators must not interpret NativeManaged as a v3 fence receipt.
extern "C" __declspec(dllexport) int praxis_resource_reload_v3(
    praxis::companion::packs::WorldPackReloadCommand,
    praxis::companion::packs::WorldPackReloadRequest const*,
    praxis::companion::packs::WorldPackReloadReply* reply) noexcept {
    if(reply && reply->bytes==sizeof(*reply)){*reply={};reply->error=1;}
    return 0;
}
