#pragma once
#include "runtime/ConfigMigration.h"
#ifndef PRAXIS_CONFIG_STORAGE_TEST
#include "ll/api/reflection/Serialization.h"
#include "ll/api/reflection/Deserialization.h"
#endif

namespace praxis::companion {
// Startup and queued profile loads share raw validation, projection and v1
// migration. The caller retains the original document before normalization.
inline bool decodeRuntimeConfig(ConfigJson const& original,RuntimeConfig& result) {
    auto document=original;
    if(!normalizeFeatureKeybinds(document)||!document.contains("version"))return false;
    auto const sourceVersion=document["version"].get<unsigned>();
    if(sourceVersion!=1&&sourceVersion!=2)return false;
    RuntimeConfig candidate;
    auto defaults=ll::reflection::serialize<ConfigJson>(candidate).value();
    auto typed=typedConfigDocument(document,defaults);
    if(!validConfigDocument(typed,defaults))return false;
    if(sourceVersion==1) {
        if(document.contains("zoomMultiplier")&&document["zoomMultiplier"].is_number())
            typed["zoomOptions"]["targetFov"]=document["zoomMultiplier"].get<float>()*70.f;
        if(document.contains("brightness")&&document["brightness"].is_number())
            typed["brightness"]=std::clamp(document["brightness"].get<float>(),1.f,25.f);
    }
    typed["version"]=2;
    ll::reflection::deserialize(candidate,typed).value();
    if(!validRuntimeConfig(candidate))return false;
    result=std::move(candidate);return true;
}
}
