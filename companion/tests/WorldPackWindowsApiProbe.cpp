#include "resourcepacks/WorldPackReloadApi.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/gui/screens/models/MinecraftScreenModel.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/TextureGroup.h"
#include "resourcepacks/LocalPngDecodeWin.h"
#include "mc/client/renderer/texture/TextureAtlas.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/client/renderer/texture/TextureHotReloader.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/client/renderer/game/LevelRendererCamera.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePack.h"
#include "mc/resources/PackInstance.h"
#include "mc/deps/core/file/Path.h"
#include "mc/resources/ResourceLoadManager.h"
#include <functional>
#include <memory>
#include <type_traits>
#include <cstddef>

// Signature/layout proof only; this object performs no native call or hook.
using ApplyScreen=std::shared_ptr<AbstractScene>(SceneFactory::*)(std::function<void(MinecraftScreenModel&)>);
using PushScreen=void(ISceneStack::*)(std::shared_ptr<AbstractScene>,bool);
using BlockInit=void(*)(ResourcePackManager&,Experiments const&);
using FrameUpdate=void(LevelRendererPlayer::*)(ClientFrameUpdateContext&);
using BeforeDimension=void(LevelRendererCamera::*)(Player&);
using Compose=void(ResourcePackManager::*)();
using GeometryRebuild=void(LevelRenderer::*)();
using NativePackBytes=bool(ResourcePack::*)(Core::Path const&,std::string&,int)const;
using NativePackInstanceBytes=bool(PackInstance::*)(Core::Path const&,std::string&)const;
using NativeUvProbe=TextureUVCoordinateSet (*)(std::string const&,int,int);
static_assert(std::is_same_v<decltype(&BlockGraphics::getTextureUVCoordinateSet),NativeUvProbe>);
using TextureRefresh=void(mce::TextureGroup::*)();
using TextureUpdate=bool(mce::TextureGroup::*)(ResourceLocation const&,cg::ImageBuffer);
using ResourceWinner=std::pair<int,std::string_view>(ResourcePackManager::*)(
    ResourceLocation const&,gsl::span<std::string const>) const;
static_assert(std::is_same_v<decltype(&mce::TextureGroup::reloadAllTextures),TextureRefresh>);
static_assert(std::is_same_v<decltype(&mce::TextureGroup::updateTextureInPlace),TextureUpdate>);
static_assert(std::is_same_v<decltype(&ResourcePackManager::getPackStackIndexOfResource),ResourceWinner>);

static_assert(std::is_same_v<decltype(&SceneFactory::createApplyResourcePacksScreen),ApplyScreen>);
static_assert(std::is_same_v<decltype(&ISceneStack::pushScreen),PushScreen>);
static_assert(std::is_same_v<decltype(&BlockGraphics::initBlocks),BlockInit>);
static_assert(std::is_same_v<decltype(&BlockGraphics::reloadBlockTextures),BlockInit>);
static_assert(std::is_same_v<decltype(&LevelRenderer::rebuildAllRenderChunkGeometry),GeometryRebuild>);
static_assert(std::is_same_v<decltype(&LevelRendererPlayer::frameUpdate),FrameUpdate>);
static_assert(std::is_same_v<decltype(&LevelRendererCamera::preDimensionChanged),BeforeDimension>);
static_assert(std::is_same_v<decltype(&ResourcePackManager::_composeFullStack),Compose>);
static_assert(offsetof(ResourceLoadManager,mUnk8953d0)==0x48);
using Request=praxis::companion::packs::WorldPackReloadRequest;
using Reply=praxis::companion::packs::WorldPackReloadReply;
static_assert(sizeof(Request)==56&&alignof(Request)==8&&offsetof(Request,token)==8);
static_assert(sizeof(Reply)==88&&alignof(Reply)==8&&offsetof(Reply,token)==8);

#include "mc/client/gui/screens/controllers/GeneralSettingsScreenController.h"
#include "mc/client/gui/screens/models/MainMenuScreenModel.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/controllers/SettingsScreenController.h"
#include "mc/client/gui/screens/AbstractScene.h"
using SettingsCtor=void*(GeneralSettingsScreenController::*)(std::shared_ptr<MainMenuScreenModel>,bool,bool,std::string const&,bool);
using SettingsSet=void(GeneralSettingsScreenController::*)();
using SettingsGlobal=void(GeneralSettingsScreenController::*)(std::vector<PackInstanceId> const&,bool,bool);
using NativeApply=void(MainMenuScreenModel::*)(std::function<void(MinecraftScreenModel&)>);
using NativeCompose=int(ResourcePackManager::*)(ResourcePackStack&,ResourcePackStack const&,ResourcePackStack const&)const;
using ClientPreGame=bool(ClientInstance::*)()const;
static_assert(std::is_same_v<decltype(&GeneralSettingsScreenController::$ctor),SettingsCtor>);
static_assert(std::is_same_v<decltype(&GeneralSettingsScreenController::_setResourcePackStack),SettingsSet>);
static_assert(std::is_same_v<decltype(&GeneralSettingsScreenController::_setGlobalStack),SettingsGlobal>);
static_assert(std::is_same_v<decltype(&MainMenuScreenModel::navigateToApplyResourcePacksScreen),NativeApply>);
static_assert(std::is_same_v<decltype(&ResourcePackManager::composeFullStack),NativeCompose>);
static_assert(std::is_same_v<decltype(&ClientInstance::$isPreGame),ClientPreGame>);
static_assert(offsetof(ResourcePackManager,mUseGlobalPackStack)==0xEA);
static_assert(offsetof(ResourcePackManager,mGameplayResourcesLoaded)==0xEB);
static_assert(offsetof(GeneralSettingsScreenController,mIsGlobalSettings)==0xD60);
static_assert(offsetof(GeneralSettingsScreenController,mReloadTexturePacksOnExit)==0xD61);
static_assert(offsetof(GeneralSettingsScreenController,mGlobalResourcePacksVisible)==0xD62);
static_assert(offsetof(GeneralSettingsScreenController,mResourceContentSource)==0xDD0);
static_assert(offsetof(GeneralSettingsScreenController,mMainMenuScreenModel)==0xD48);
using SettingsOpen=void(SettingsScreenController::*)();
using SettingsScene=uint(MinecraftScreenController::*)()const;
using TopScene=std::shared_ptr<AbstractScene>(ISceneStack::*)()const;
static_assert(std::is_same_v<decltype(&SettingsScreenController::$onOpen),SettingsOpen>);
static_assert(std::is_same_v<decltype(&MinecraftScreenController::getSceneId),SettingsScene>);
static_assert(std::is_same_v<decltype(&ISceneStack::getTopSceneShared),TopScene>);
static_assert(std::is_base_of_v<std::enable_shared_from_this<MinecraftScreenController>,SettingsScreenController>);
static_assert(static_cast<int>(SettingsTabIndex::GlobalResourcesTab)==24);

#include "resourcepacks/NativeSettingsPackScope.h"
#include "resourcepacks/WorldNativeCompatibilityGate.h"
#include "mc/client/game/MinecraftGame.h"
using WorldInGameFn=bool(MinecraftGame::*)()const;
static_assert(std::is_same_v<decltype(&MinecraftGame::$isInGame),WorldInGameFn>);
using ModelClientId=SubClientId(MinecraftScreenModel::*)()const;
using ClientGame=IMinecraftGame&(IClientInstance::*)()const;
using CurrentScenes=Bedrock::NotNullNonOwnerPtr<ISceneStack>(IClientInstance::*)();
static_assert(std::is_same_v<decltype(&MinecraftScreenModel::getSubClientId),ModelClientId>);
static_assert(std::is_same_v<decltype(&IClientInstance::getMinecraftGame_DEPRECATED),ClientGame>);
static_assert(std::is_same_v<decltype(static_cast<CurrentScenes>(&IClientInstance::getCurrentSceneStack)),CurrentScenes>);
// Compile the actual resolver against the pinned SDK, rather than substituting
// bool fixtures for native calls. Final product link validates its SDK binding.
std::shared_ptr<IClientInstance> probeSettingsClient(MinecraftScreenModel const& model,MinecraftGame& game,std::uintptr_t observed) {
    return praxis::companion::packs::nativeSettingsClient(model,game,observed);
}
std::shared_ptr<AbstractScene> probeSettingsCurrentScene(IClientInstance& client) {
    return client.getCurrentSceneStack()->getTopSceneShared();
}
