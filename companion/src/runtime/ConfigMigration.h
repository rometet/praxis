#pragma once
#include "core/Settings.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
namespace praxis::companion {
using ConfigJson=nlohmann::ordered_json;
inline bool configUnsigned(ConfigJson const& value,std::uint64_t maximum) {
 if(!value.is_number_integer())return false;
 if(value.is_number_unsigned())return value.get<std::uint64_t>()<=maximum;
 auto n=value.get<std::int64_t>();return n>=0 && static_cast<std::uint64_t>(n)<=maximum;
}
inline bool normalizeFeatureKeybinds(ConfigJson& document) {
 if(!document.is_object())return false;
 for(auto name:{"featureSlotSchema","version"}) {
  auto p=document.find(name);if(p==document.end())continue;
  auto maximum=std::string_view(name)=="version"?2u:featureSlotSchema;
  if(!configUnsigned(*p,maximum)||*p==0)return false;
 }
 auto count=static_cast<std::size_t>(featureCount);
 for(auto name:{"keybinds","keybindModifiers"}) {
  auto p=document.find(name);if(p==document.end())continue;
  if(!p->is_array() || p->size()<7 || p->size()>featureRegistryBits)return false;
  auto maximum=std::string_view(name)=="keybinds"?255u:anyModifiers;
  for(auto const& value:*p)if(!configUnsigned(value,maximum))return false;
  count=std::max(count,p->size());
 }
 if(document.contains("keybinds")&&document.contains("keybindModifiers")
  &&document["keybinds"].size()!=document["keybindModifiers"].size())return false;
 for(auto name:{"zoomModifiers","mapToggleModifiers","favoriteModules"}) {
  auto p=document.find(name);if(p!=document.end()&&!configUnsigned(*p,std::string_view(name)=="favoriteModules"?UINT32_MAX:anyModifiers))return false;
 }
 auto next=document;
 if(next.contains("keybinds")||next.contains("keybindModifiers"))for(auto name:{"keybinds","keybindModifiers"}) {
  auto const sourceSize = next.contains(name)&&next[name].is_array() ? next[name].size() : 0u;
  if(!next.contains(name))next[name]=ConfigJson::array();
  while(next[name].size()<count)next[name].push_back(std::string_view(name)=="keybinds"?0:anyModifiers);
  if(std::string_view(name)=="keybinds"
   &&sourceSize<=static_cast<std::size_t>(Feature::FlexibleBlockPlacement)
   &&count>static_cast<std::size_t>(Feature::FlexibleBlockPlacement))
   next[name][static_cast<std::size_t>(Feature::FlexibleBlockPlacement)]='P';
 }
 next["featureSlotSchema"]=featureSlotSchema;document.swap(next);return true;
}
inline std::uint32_t registryMask(unsigned count) noexcept {
 return count>=featureRegistryBits?UINT32_MAX:(1u<<count)-1u;
}
inline unsigned sourceRegistryExtent(ConfigJson const& source) {
 // The raw key span declares favorite authority. Padded modifier tails and a
 // schema marker alone must not manufacture missing favorite authority.
 for(auto name:{"keybinds","keybindModifiers"})if(source.contains(name)&&source[name].is_array())return static_cast<unsigned>(source[name].size());
  return 0;
}
// Raw source overlays retain nested unknown/null values. Binding arrays merge
// by index; a shorter source never erases a longer destination's tail.
inline void overlayProfileFields(ConfigJson& destination,ConfigJson const& source,bool root=true) {
 if(!destination.is_object()||!source.is_object())throw std::runtime_error("Profile must be an object");
 for(auto const& [key,value]:source.items()) {
  if(root&&(key=="keybinds"||key=="keybindModifiers")) {
   auto& target=destination[key];if(target.is_null())target=ConfigJson::array();
   if(!target.is_array()||!value.is_array())throw std::runtime_error("Binding must be an array");
   while(target.size()<value.size())target.push_back(key=="keybinds"?0:anyModifiers);
   for(std::size_t i=0;i<value.size();++i)target[i]=value[i];continue;
  }
  if(root&&key=="favoriteModules") {
   if(!configUnsigned(value,UINT32_MAX))throw std::runtime_error("Invalid favorites");
   auto old=destination.value(key,UINT32_C(0));auto incoming=value.get<std::uint32_t>();
    auto extent=sourceRegistryExtent(source);
    auto mask=(extent?registryMask(extent):ownedFeatureSlotMask)|incoming;
   destination[key]=(old&~mask)|(incoming&mask);continue;
  }
  auto p=destination.find(key);
  if(value.is_object()&&p!=destination.end()&&p->is_object())overlayProfileFields(*p,value,false);
  else destination[key]=value;
 }
}
inline void overlayOwnedConfig(ConfigJson& document,ConfigJson const& typed,bool freshTypedDocument=false,
                              std::uint32_t ownedSlots=ownedFeatureSlotMask) {
 if(freshTypedDocument){overlayProfileFields(document,typed);return;}
 for(auto const& [key,value]:typed.items()) {
  if(((key=="weatherChanger"||key=="weatherOptions")&&!(ownedSlots&(1u<<8)))||(key=="hitboxOptions"&&!(ownedSlots&(1u<<9))))continue;
  if((key=="inventoryHud"||key=="inventoryHudOptions")&&!(ownedSlots&featureRegistryBit(23)))continue;
  if((key=="antiEffect"||key=="antiEffectOptions")&&!(ownedSlots&featureRegistryBit(24)))continue;
  if(key=="keybinds"||key=="keybindModifiers") {
   auto& target=document[key];if(target.is_null())target=ConfigJson::array();
   auto count=std::max(target.size(),value.size());while(target.size()<count)target.push_back(key=="keybinds"?0:anyModifiers);
   for(unsigned i=0;i<featureCount;++i)if(ownedSlots&(1u<<i))target[i]=value[i];continue;
  }
  if(key=="favoriteModules") {
   auto old=document.value(key,UINT32_C(0));document[key]=(old&~ownedSlots)|(value.get<std::uint32_t>()&ownedSlots);continue;
  }
  if(key=="informationOptions" && value.is_object()) {
   auto active=value;
   if(active.contains("hudLayouts"))active["hudLayouts"].erase("inventory");
   auto p=document.find(key);
   if(p!=document.end()&&p->is_object())overlayProfileFields(*p,active,false);
   else document[key]=active;
   continue;
  }
  auto p=document.find(key);
  if(value.is_object()&&p!=document.end()&&p->is_object())overlayProfileFields(*p,value,false);
  else document[key]=value;
 }
 // Match key/modifier extent even when source/target arrays had different sizes.
 auto count=std::max(document["keybinds"].size(),document["keybindModifiers"].size());
 for(auto name:{"keybinds","keybindModifiers"})while(document[name].size()<count)document[name].push_back(std::string_view(name)=="keybinds"?0:anyModifiers);
}
// Project only known fields for SDK decoding. The unprojected source is owned
// separately and crosses the profile queue boundary as a serialized string.
inline ConfigJson typedConfigDocument(ConfigJson const& source,ConfigJson const& defaults) {
 auto result=defaults;
 if(!source.is_object()||!defaults.is_object())return source;
 for(auto const& [key,value]:source.items())if(defaults.contains(key)) {
  if(value.is_object()&&defaults[key].is_object())result[key]=typedConfigDocument(value,defaults[key]);
  else result[key]=value;
 }
 return result;
}
inline bool validConfigDocument(ConfigJson const& value,ConfigJson const& shape,bool root=true) {
 if(shape.is_object()) {
  if(!value.is_object()||value.size()!=shape.size())return false;
  for(auto const& [key,child]:value.items()) {
   if(!shape.contains(key))return false;
   if(root&&(key=="keybinds"||key=="keybindModifiers")) {
    if(!child.is_array()||child.size()<featureCount||child.size()>featureRegistryBits)return false;
    for(auto const& v:child)if(!configUnsigned(v,key=="keybinds"?255:anyModifiers))return false;
   }else if(!validConfigDocument(child,shape.at(key),false))return false;
  }return true;
 }
 if(shape.is_array()) {
  if(!value.is_array()||value.size()!=shape.size())return false;
  for(std::size_t i=0;i<shape.size();++i)if(!validConfigDocument(value[i],shape[i],false))return false;return true;
 }
 if(shape.is_boolean())return value.is_boolean();
 if(shape.is_number_integer()) {
  if(!value.is_number_integer())return false;
  if(shape.is_number_unsigned())return configUnsigned(value,UINT32_MAX);
  if(value.is_number_unsigned())return value.get<std::uint64_t>()<=INT32_MAX;
  auto n=value.get<std::int64_t>();return n>=INT32_MIN&&n<=INT32_MAX;
 }
 if(shape.is_number_float())return value.is_number()&&std::isfinite(value.get<double>())&&std::abs(value.get<double>())<=std::numeric_limits<float>::max();
 return value.type()==shape.type();
}
}
