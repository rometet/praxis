#include "map/MapRuntime.h"
#include "gui/JapaneseText.h"
#include "map/MapCore.h"
#include "map/MapDataSource.h"
#include "map/MapPersistence.h"
#include "map/MapRenderer.h"
#include "map/MapGraphics.h"
#include "map/TerrainBaker.h"
#ifndef PRAXIS_MAP_HEADLESS_TEST
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/dimension/Dimension.h"
#endif
#include <atomic>
#include <cstdio>
#include <deque>
#include <imgui.h>
#include <memory>
#include <mutex>

namespace praxis::companion {
namespace {
using namespace map;
std::mutex mapMutex;
std::unique_ptr<Persistence> persistence;
Cache cache;
Scanner scanner;
std::unique_ptr<TerrainBaker> baker;
std::array<View,2> bakeViews{};
std::size_t bakeViewCount{};
std::uint64_t bakeRevision{}, terrainPublications{};
bool bakeCaveShade{};
std::chrono::steady_clock::time_point nextBake{};
std::atomic_bool optionsLoadDone{};
bool pendingMapToggle{};
CaveBand caveBand;
SettingsHistory settingsHistory;
DeathTracker deaths;
std::optional<Waypoint> pendingDeath;
Waypoints waypoints;
std::atomic<std::shared_ptr<Settings const>> options{std::make_shared<Settings>()};
std::atomic<std::shared_ptr<Snapshot const>> publication;
Session activeSession;
std::uint64_t tick{}, savedWaypoints{}, pendingWaypointRevision{}, savedTerrain{}, optionsRevision{1},
    savedOptions{};
bool terrainLoadDone{}, waypointsLoadDone{}, waypointLoadQueued{}, terrainLoadQueued{}, waypointsBlocked{},
    terrainBlocked{}, optionsBlocked{}, waypointWriteFailed{}, stopped{true};
std::vector<Tile> loadedTiles;
std::size_t loadedCursor{};
std::string ioStatus = "Not initialized";
double worldX{}, worldZ{};
double worldZoom{1};
bool worldOpen{}, worldCentered{true};
std::chrono::steady_clock::time_point nextRadar{}, nextSave{};
std::chrono::steady_clock::time_point sessionStarted{};
double firstAppearance{-1};
std::atomic<double> renderMilliseconds{};
std::atomic<bool> actionDispatcherAvailable{};
std::vector<RadarPoint> radar;
std::uint32_t radarVisited{};
double radarTime{};
struct Command {
    Session session;
    enum Action { Add, Edit, Remove } action{};
    Waypoint waypoint;
};
std::deque<Command> commands;
std::deque<IoJob> deferredWrites;
// Admission pauses on the first retired-world save failure. At most 32
// admitted IO jobs + 8 deferred retirements + one active snapshot can fail.
std::deque<IoJob> failedWrites;
bool retryInFlight{};
Session retrySession;
std::uint64_t retryRevision{};
std::chrono::steady_clock::time_point nextRetry{};
void retryFailed() {
    if (!persistence || failedWrites.empty() || retryInFlight || std::chrono::steady_clock::now() < nextRetry)
        return;
    auto const &job = failedWrites.front();
    if (persistence->enqueue(job)) {
        retryInFlight = true;
        retrySession = job.session;
        retryRevision = job.revision;
        nextRetry = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }
}
void retainFailed(IoResult &result) {
    if (!std::holds_alternative<std::vector<Waypoint>>(result.payload))
        return;
    for (auto &job : failedWrites)
        if (job.session.world == result.session.world) {
            if (result.revision > job.revision)
                job = {result.session, FileKind::Waypoints, result.revision, true, std::move(result.payload)};
            return;
        }
    failedWrites.push_back(
        {result.session, FileKind::Waypoints, result.revision, true, std::move(result.payload)});
}
void flushDeferred() {
    while (persistence && !deferredWrites.empty()) {
        if (!persistence->enqueue(deferredWrites.front()))
            break;
        deferredWrites.pop_front();
    }
}

bool queueWaypoints() {
    if (!persistence || !activeSession.persistent || !waypointsLoadDone || waypointsBlocked)
        return false;
    auto revision = waypoints.revision();
    if (revision == savedWaypoints || revision == pendingWaypointRevision)
        return true;
    if (!persistence->enqueue({activeSession, FileKind::Waypoints, revision, true, waypoints.items()}))
        return false;
    pendingWaypointRevision = revision;
    return true;
}
bool retireLocked() {
    if (waypointWriteFailed && activeSession.valid() && waypoints.revision() != savedWaypoints) {
        publication.store({}, std::memory_order_release);
        scanner.cancel();
        ioStatus = "Waypoint save and recovery failed: owned edits retained; Map paused";
        return false;
    }
    if (activeSession.valid() && activeSession.persistent && waypointsLoadDone &&
        waypoints.revision() != savedWaypoints && !queueWaypoints()) {
        if (deferredWrites.size() >= 8) {
            publication.store({}, std::memory_order_release);
            scanner.cancel();
            ioStatus = "Persistence saturated: previous-world waypoints retained; Map paused";
            return false;
        }
        deferredWrites.push_back(
            {activeSession, FileKind::Waypoints, waypoints.revision(), true, waypoints.items()});
    }
    activeSession = {};
    scanner.cancel();
    cache.begin({});
    waypoints.clear();
    commands.clear();
    loadedTiles.clear();
    loadedCursor = 0;
    radar.clear();
    if(baker)baker->cancel();
    bakeViewCount=0;nextBake={};

    caveBand.reset();
    deaths.clear();
    pendingDeath.reset();
    publication.store({}, std::memory_order_release);
    worldCentered = true;
    return true;
}
void completions() {
    if (!persistence)
        return;
    for (auto &result : persistence->take(activeSession)) {
        if(result.kind==FileKind::Options&&!result.write)optionsLoadDone.store(true);
        bool const retryResult = result.write && result.kind == FileKind::Waypoints && retryInFlight &&
                                 result.session == retrySession && result.revision == retryRevision;
        if (retryResult) {
            retryInFlight = false;
            if ((result.okay || result.recovered) && !failedWrites.empty() &&
                failedWrites.front().session == retrySession &&
                failedWrites.front().revision == retryRevision)
                failedWrites.pop_front();
        }
        if (result.kind != FileKind::Options && result.session != activeSession) {
            if (!result.okay && !result.recovered)
                retainFailed(result);
            ioStatus = result.okay ? "Previous-world save completed" : "Previous world: " + result.error;
            continue;
        }
        if (!result.okay) {
            ioStatus = result.error;
            if (result.kind == FileKind::Waypoints) {
                if (result.write) {
                    waypointWriteFailed = !result.recovered;
                    if (!result.recovered)
                        retainFailed(result);
                    if (result.recovered)
                        savedWaypoints = std::max(savedWaypoints, result.revision);
                    pendingWaypointRevision = 0;
                } else
                    savedWaypoints = waypoints.revision(); // No edits were admitted before a failed load.
                waypointsBlocked = true;
                waypointsLoadDone = true;
            }
            if (result.kind == FileKind::Terrain) {
                terrainBlocked = true;
                terrainLoadDone = true;
            }
            if (result.kind == FileKind::Options)
                optionsBlocked = true;
            continue;
        }
        if (result.write) {
            if (result.kind == FileKind::Waypoints) {
                savedWaypoints = std::max(savedWaypoints, result.revision);
                waypointWriteFailed = false;
                waypointsBlocked = false;
                if (pendingWaypointRevision == result.revision)
                    pendingWaypointRevision = 0;
            }
            if (result.kind == FileKind::Terrain)
                savedTerrain = std::max(savedTerrain, result.revision);
            if (result.kind == FileKind::Options)
                savedOptions = std::max(savedOptions, result.revision);
            ioStatus = "Saved";
            continue;
        }
        if (result.kind == FileKind::Waypoints) {
            if (auto items = std::get_if<std::vector<Waypoint>>(&result.payload))
                waypoints.replace(std::move(*items));
            waypointsLoadDone = true;
            savedWaypoints = waypoints.revision();
        }
        if (result.kind == FileKind::Terrain) {
            if (auto tiles = std::get_if<std::vector<Tile>>(&result.payload)) {
                loadedTiles = std::move(*tiles);
                loadedCursor = 0;
            }
            terrainLoadDone = true;
        }
        if (result.kind == FileKind::Options && !result.missing && optionsRevision == 1) {
            if (auto setting = std::get_if<Settings>(&result.payload))
                options.store(std::make_shared<Settings const>(*setting));
            savedOptions = result.optionsMigrated ? 0 : optionsRevision;
        }
        ioStatus = result.missing ? "New world / no saved data" : "Loaded";
    }
    // Restoring cache data is incremental too; no huge synchronous publication.
    for (int count = 0; count < 8 && loadedCursor < loadedTiles.size(); ++count)
        cache.restore(activeSession, loadedTiles[loadedCursor++]);
    if (loadedCursor == loadedTiles.size()) {
        loadedTiles.clear();
        loadedCursor = 0;
    }
}
bool queueCommand(Command cmd) {
    std::lock_guard lock(mapMutex);
    if (stopped || commands.size() >= 64 || cmd.session != activeSession ||
        !waypointsLoadDone || waypointsBlocked)
        return false;
    commands.push_back(std::move(cmd));
    return true;
}
void applyOptions(Settings const &setting, bool remember = true) {
    if (!setting.valid())
        return;
    std::lock_guard lock(mapMutex);
    auto old = options.load();
    if (*old == setting)
        return;
    if (remember)
        settingsHistory.remember(*old);
    options.store(std::make_shared<Settings const>(setting));
    ++optionsRevision;
    if (persistence && !optionsBlocked)
        persistence->enqueue({{}, FileKind::Options, optionsRevision, true, setting});
}
} // namespace
bool initializeMap(std::filesystem::path const &root) noexcept {
    try {
        std::lock_guard lock(mapMutex);
        if (root.empty() || !root.is_absolute())
            return false;
        if (persistence)
            return true;
        auto newPersistence=std::make_unique<Persistence>(root);
        auto newBaker=std::make_unique<TerrainBaker>();
        persistence=std::move(newPersistence);
        baker=std::move(newBaker);
        optionsLoadDone.store(false);
        stopped = false;
        ioStatus = "Loading settings";
        persistence->enqueue({{}, FileKind::Options, 0, false, {}});
        return true;
    } catch (...) {
        return false;
    }
}
#ifndef PRAXIS_MAP_HEADLESS_TEST
void updateMap(LocalPlayer &player, std::uint64_t generation) noexcept {
    try {
        std::lock_guard lock(mapMutex);
        if (stopped)
            return;
        flushDeferred();
        completions();
        retryFailed();
        if(optionsLoadDone.load()&&pendingMapToggle) {
            pendingMapToggle=false;
            auto next=std::make_shared<Settings>(*options.load());next->enabled=!next->enabled;
            options.store(next);++optionsRevision;
            if(persistence&&!optionsBlocked)persistence->enqueue({{},FileKind::Options,optionsRevision,true,*next});
        }
        if (!failedWrites.empty() || retryInFlight) {
            publication.store({});
            ioStatus = "Unsaved previous-world waypoints retained: retrying; Map paused";
            return;
        }
        // A same-world rejoin cannot load older data before a deferred save.
        // Accepted IO jobs are FIFO; retain retirement order at admission too.
        if (!deferredWrites.empty()) {
            publication.store({});
            return;
        }
        auto setting = options.load(std::memory_order_acquire);
        if (!setting->enabled) {
            if (activeSession.valid())
                retireLocked();
            return;
        }
        auto session = captureSession(player, generation + 1);
        if (!session.valid()) {
            retireLocked();
            return;
        }
        if (session != activeSession) {
            if (!retireLocked())
                return;
            if (!deferredWrites.empty())
                return;
            activeSession = session;
            cache.begin(session);
            tick = 0;
            savedWaypoints = pendingWaypointRevision = savedTerrain = 0;
            waypointsLoadDone = terrainLoadDone = !session.persistent;
            waypointLoadQueued = terrainLoadQueued = false;
            waypointsBlocked = terrainBlocked = waypointWriteFailed = false;
            ioStatus = session.persistent ? "Loading world" : "Session only: stable world ID unavailable";
            nextSave = {};
            nextRadar = {};
            sessionStarted = std::chrono::steady_clock::now();
            firstAppearance = -1;
        }
        cache.setTileLimit(std::size_t(setting->maxTiles));
        if (persistence && session.persistent) {
            if (!waypointsLoadDone && !waypointLoadQueued)
                waypointLoadQueued = persistence->enqueue({session, FileKind::Waypoints, 0, false, {}});
            if (!terrainLoadDone && !terrainLoadQueued)
                terrainLoadQueued = persistence->enqueue({session, FileKind::Terrain, 0, false, {}});
        }
        ++tick;
        while (!commands.empty()) {
            auto cmd = std::move(commands.front());
            commands.pop_front();
            if (cmd.session != activeSession || !waypointsLoadDone || waypointsBlocked)
                continue;
            if (cmd.action == Command::Add)
                waypoints.add(std::move(cmd.waypoint));
            if (cmd.action == Command::Edit)
                waypoints.edit(std::move(cmd.waypoint));
            if (cmd.action == Command::Remove)
                waypoints.remove(cmd.waypoint.id);
        }
        auto position = player.getPosition();
        if (!coordinate(position.x) || !coordinate(position.z) || !std::isfinite(position.y))
            return;
        int x = int(std::floor(position.x)), z = int(std::floor(position.z));
        auto [top, bottom] = caveBand.select(position.y, setting->caveDepth);
        auto death = deaths.observe(activeSession, position.x, position.y, position.z, player.isAlive());
        if (setting->lastDeath && death)
            pendingDeath = std::move(death);
        if (pendingDeath && waypointsLoadDone && !waypointsBlocked) {
            if (!waypoints.edit(*pendingDeath))
                waypoints.add(*pendingDeath);
            pendingDeath.reset();
        }
        if (worldCentered) {
            worldX = position.x;
            worldZ = position.z;
        }
        View mini{x,
                  z,
                  std::clamp(int(setting->radius / setting->zoom), 16, 256),
                  setting->cave ? Layer::Cave : Layer::Surface,
                  top,
                  bottom};
        std::array<View, 2> views{mini,
                                  View{int(std::floor(worldX)), int(std::floor(worldZ)),
                                       std::clamp(int(256 / worldZoom), 16, 256), mini.layer, top, bottom}};
        std::size_t viewCount = worldOpen ? 2 : 1;
        scanner.request(activeSession, {views.data(), viewCount});
        auto client=ll::service::getClientInstance();
        if(!client||client->getLocalPlayer()!=&player)return;
        auto* region=client->getRegion();
        if(!region)return;
        NativeDataSource source(*region);
        auto stats = scanner.tick(cache, source, *setting, tick);
        auto now = std::chrono::steady_clock::now();
        if (setting->radar && setting->radarCategories[0] && now >= nextRadar) {
            auto start = now;
            radar = captureRadar(player, std::min(setting->radius, 128), setting->radarNames, radarVisited);
            radarTime =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            nextRadar = now + std::chrono::milliseconds(setting->radarMilliseconds);
        }
        if (!setting->radar || !setting->radarCategories[0])
            radar.clear();
        if (persistence && now >= nextSave) {
            queueWaypoints();
            if (activeSession.persistent && terrainLoadDone && !terrainBlocked &&
                cache.revision() != savedTerrain)
                persistence->enqueue(
                    {activeSession, FileKind::Terrain, cache.revision(), true, cache.exportTiles(128)});
            if (!optionsBlocked && optionsRevision != savedOptions)
                persistence->enqueue({{}, FileKind::Options, optionsRevision, true, *setting});
            nextSave = now + std::chrono::seconds(3);
        }
        auto start = std::chrono::steady_clock::now();
        if(baker&&now>=nextBake&&(bakeRevision!=cache.revision()||bakeViewCount!=viewCount||
            !std::equal(views.begin(),views.begin()+std::ptrdiff_t(viewCount),bakeViews.begin())||
            bakeCaveShade!=setting->caveShade)) {
            TerrainBake job{activeSession,views,viewCount,cache.revision(),cache.exportTiles(std::size_t(setting->maxTiles))};
            job.caveShade=setting->caveShade;
            baker->enqueue(std::move(job));
            bakeViews=views;bakeViewCount=viewCount;bakeRevision=cache.revision();bakeCaveShade=setting->caveShade;
            nextBake=now+std::chrono::milliseconds(80);
        }
        auto baked=baker?baker->take(activeSession,mini):nullptr;
        auto terrain=baked?baked->cells:std::make_shared<std::vector<Cell> const>();
        auto yaw = std::isfinite(player.getRotation().y) ? player.getRotation().y : 0;
        auto previous = publication.load(std::memory_order_acquire);
        if (previous && previous->session == activeSession && previous->cells == terrain &&
            previous->waypointRevision == waypoints.revision() && previous->radar == radar &&
            previous->waypointsEditable == (waypointsLoadDone && !waypointsBlocked) &&
            previous->playerX == position.x && previous->playerY == position.y &&
            previous->playerZ == position.z && previous->yaw == yaw && previous->layer == mini.layer &&
            (mini.layer == Layer::Surface || (previous->caveTop == top && previous->caveBottom == bottom)) &&
            previous->persistenceStatus == ioStatus)
            return; // Diagnostics describe the last publication; unchanged stationary ticks allocate nothing here.
        auto snapshot = std::make_shared<Snapshot>();
        snapshot->session = activeSession;
        snapshot->cells = std::move(terrain);
        if(baked)snapshot->terrainImages=baked->images;
        if(!previous||previous->cells!=snapshot->cells)++terrainPublications;
        snapshot->waypoints = waypoints.items();
        snapshot->radar = radar;
        snapshot->playerX = position.x;
        snapshot->playerY = position.y;
        snapshot->playerZ = position.z;
        snapshot->yaw = yaw;
        snapshot->waypointRevision = waypoints.revision();
        snapshot->waypointsEditable = waypointsLoadDone && !waypointsBlocked;
        snapshot->revision = cache.revision();
        snapshot->layer = mini.layer;
        snapshot->caveTop = top;
        snapshot->caveBottom = bottom;
        snapshot->demandBuilds = scanner.builds();
        snapshot->terrainPublications = terrainPublications;
        snapshot->cacheTiles = cache.size();
        snapshot->cacheBytes = cache.bytes();
        snapshot->reads = stats.reads;
        snapshot->columns = stats.columns;
        snapshot->scanMilliseconds = stats.milliseconds;
        snapshot->radarVisited = radarVisited;
        snapshot->radarMilliseconds = radarTime;
        snapshot->persistenceStatus = ioStatus;
        snapshot->publicationMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (firstAppearance < 0 && std::any_of(snapshot->cells->begin(), snapshot->cells->end(),
                                             [](auto const &c) { return c.sample.known; }))
            firstAppearance =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sessionStarted)
                    .count();
        snapshot->firstAppearanceMilliseconds = firstAppearance;
        snapshot->renderMilliseconds = renderMilliseconds.load();
        publication.store(std::move(snapshot), std::memory_order_release);
    } catch (std::exception const &e) {
        std::lock_guard lock(mapMutex);
        ioStatus = e.what();
        publication.store({});
    } catch (...) {
        std::lock_guard lock(mapMutex);
        ioStatus = "Map tick failed";
        publication.store({});
    }
}
#endif
void retireMap() noexcept {
    try {
        std::lock_guard lock(mapMutex);
        retireLocked();
    } catch (...) {
        publication.store({});
    }
}
void shutdownMap() noexcept {
    std::unique_ptr<Persistence> old;
    std::unique_ptr<TerrainBaker> oldBaker;
    {
        std::lock_guard lock(mapMutex);
        stopped = true;
        actionDispatcherAvailable.store(false);
        // Only shutdown may wait on disk after native/render admission stops.
        if (persistence) {
            persistence->flush();
            completions();
            while (!deferredWrites.empty()) {
                flushDeferred();
                persistence->flush();
                completions();
            }
            // One final worker retry; persistent storage failures do not cause
            // an infinite shutdown loop. Owned recovery data remains retained.
            nextRetry = {};
            retryFailed();
            persistence->flush();
            completions();
        }
        queueWaypoints();
        if (persistence && !optionsBlocked)
            persistence->enqueue({{}, FileKind::Options, optionsRevision, true, *options.load()});
        if (persistence) {
            persistence->flush();
            completions(); // Consume final failures before retiring their owned snapshots.
        }
        retireLocked();
        old = std::move(persistence);
        oldBaker=std::move(baker);
    }
    if (old)
        old->stopAndJoin();
}
void toggleMapEnabled() noexcept {
    try {
        std::lock_guard lock(mapMutex);
        if(stopped)return;
        if(!optionsLoadDone.load()){pendingMapToggle=!pendingMapToggle;return;}
        auto next=std::make_shared<Settings>(*options.load());next->enabled=!next->enabled;
        options.store(next);++optionsRevision;
        if(persistence&&!optionsBlocked)persistence->enqueue({{},FileKind::Options,optionsRevision,true,*next});
    } catch(...) {}
}
bool mapHudNeeded() noexcept {
    auto s = publication.load(std::memory_order_acquire);
    auto setting = options.load();
    return setting->enabled && setting->minimap && bool(s);
}
std::array<int, mapActionCount> mapActionBindings() noexcept { return options.load()->actionKeys; }
bool setMapActionBinding(MapAction action,int key) noexcept {
    try {
        auto index=static_cast<unsigned>(action);
        if(index>=mapActionCount||key<0||key>255||key==0x79||key==0x7A)return false;
        auto value=*options.load();value.actionKeys[index]=key;applyOptions(value);return true;
    }catch(...){return false;}
}
void setMapActionDispatcherAvailable(bool available) noexcept { actionDispatcherAvailable.store(available); }
bool dispatchMapAction(MapAction action) noexcept {
    try {
        if (!actionDispatcherAvailable.load()) return false;
        { std::lock_guard lock(mapMutex); if (stopped) return false; }
        if (std::size_t(action) >= mapActionCount)
            return false;
        if (action == MapAction::CenterOnPlayer) {
            std::lock_guard lock(mapMutex);
            if (stopped || !activeSession.valid()) return false;
            worldCentered = true;
            return true;
        }
        if (action == MapAction::AddWaypoint) {
            auto s = publication.load(std::memory_order_acquire);
            if (!s) return false;
            Waypoint point{std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()),
                           "Waypoint", s->playerX, s->playerY, s->playerZ, s->session.dimension};
            if (!validWaypoint(point)) return false;
            return queueCommand({s->session, Command::Add, std::move(point)});
        }
        applyOptions(actionSettings(*options.load(), action));
        return true;
    } catch (...) { return false; }
}
void drawMapHud(void *context) noexcept {
    if (!context || context != ImGui::GetCurrentContext())
        return;
    auto s = publication.load(std::memory_order_acquire);
    auto setting = options.load();
    if (!setting->enabled || !setting->minimap || !s)
        return;
    double half = setting->pixelSize / 2.;
    auto size = ImGui::GetIO().DisplaySize;
    auto center = minimapCenter(*setting, {size.x, size.y});
    double x = center.x, y = center.z;
    Canvas canvas{{{s->playerX, s->playerZ},
                   {x, y},
                   half * setting->zoom / setting->radius,
                   mapAngle(s->yaw, setting->northLocked)},
                  half,
                  setting->circular};
    configureCanvas(canvas, *setting);
    canvas.viewMask = 1;
    auto start = std::chrono::steady_clock::now();
    auto *draw = ImGui::GetForegroundDrawList();
    drawMapCanvas(*draw, *s, canvas);
    char text[160];
    std::string label=setting->cave?"洞窟地図":"ミニマップ";
    if(setting->coordinates) {
        std::snprintf(text,sizeof(text),"  X %.0f  Y %.0f  Z %.0f",s->playerX,s->playerY,s->playerZ);
        label+=text;
    }
    drawMapLabel(*draw,label.c_str(),float(x-half),float(y+half+6),size.x,size.y);
    renderMilliseconds.store(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
void drawMapSettings() noexcept {
    if (!ImGui::GetCurrentContext())
        return;
    if(persistence&&!optionsLoadDone.load()) {
        ImGui::TextUnformatted("地図設定を読込み中");
        return;
    }
    auto old = options.load();
    auto setting = *old;
    bool changed = false;
    ImGui::PushID("PraxisMap");
    ImGui::TextWrapped("初期は無効。Mで地図のON/OFF。広域地図を開くと、左ドラッグで移動・ホイールで拡大縮小。右クリックで地点の下書きを作り、下の保存で確定します。未読込みの地形は表示できません。");
    changed |= ImGui::Checkbox("地図を有効に", &setting.enabled);
    changed |= ImGui::Checkbox("ミニマップを表示", &setting.minimap);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("円形", &setting.circular);
    changed |= ImGui::Checkbox("北を上に固定", &setting.northLocked);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("洞窟地図", &setting.cave);
    changed |= ImGui::Checkbox("レーダー", &setting.radar);
    changed |= ImGui::Checkbox("プレイヤー", &setting.radarCategories[0]);
    ImGui::BeginDisabled();
    ImGui::Checkbox("敵対Mob (未対応)", &setting.radarCategories[1]);
    ImGui::Checkbox("友好Mob (未対応)", &setting.radarCategories[2]);
    ImGui::Checkbox("アイテム (未対応)", &setting.radarCategories[3]);
    ImGui::Checkbox("その他 (未対応)", &setting.radarCategories[4]);
    ImGui::EndDisabled();
    changed |= ImGui::Checkbox("名前を表示 (最大16件)", &setting.radarNames);
    changed |= ImGui::Checkbox("遠い印を薄くする", &setting.distanceRadar);
    ImGui::TextWrapped("レーダー: 上三角は上、点は同じ高さ、下三角は下 (差4ブロック)。");
    changed |= ImGui::Checkbox("座標", &setting.coordinates);
    changed |= ImGui::Checkbox("地点の印", &setting.waypointMarkers);
    changed |= ImGui::Checkbox("最後の死亡地点を記録", &setting.lastDeath);
    changed |= ImGui::Checkbox("低解像度の画素を揃える", &setting.snapPixels);
    changed |= ImGui::Checkbox("洞窟の深さで陰影", &setting.caveShade);
    int anchor = int(setting.anchor);
    if (ImGui::Combo("ミニマップの位置", &anchor, "右上\0左上\0右下\0左下\0")) {
        setting.anchor = Anchor(anchor);
        changed = true;
    }
    changed |= ImGui::SliderInt("横のずれ", &setting.offsetX, -4096, 4096);
    changed |= ImGui::SliderInt("縦のずれ", &setting.offsetY, -4096, 4096);
    if (ImGui::TreeNode("地図の操作キー")) {
        char const *labels[] = {"ミニマップ切替", "拡大", "縮小", "洞窟切替", "自分を中心に", "地点を追加"};
        bool available = actionDispatcherAvailable.load();
        if (!available) ImGui::TextUnformatted("この表示側では操作キーを使えません。");
        ImGui::TextWrapped("キー番号を指定します。0は未設定。「キー割当」画面ならキーを押して指定できます。");
        ImGui::BeginDisabled(!available);
        for (std::size_t i = 0; i < mapActionCount; ++i)
            changed |= ImGui::SliderInt(labels[i], &setting.actionKeys[i], 0, 255);
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    changed |= ImGui::SliderInt("表示半径", &setting.radius, 16, 256);
    changed |= ImGui::SliderInt("表示サイズ", &setting.pixelSize, 96, 384);
    float zoom = float(setting.zoom);
    if (ImGui::SliderFloat("倍率", &zoom, 0.25f, 8, "%.2fx")) {
        setting.zoom = zoom;
        changed = true;
    }
    changed |= ImGui::SliderInt("洞窟の深さ", &setting.caveDepth, 4, 64);
    if (ImGui::Button("地図を初期化")) {
        setting = Settings{};
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("設定を1つ戻す")) {
        std::optional<Settings> undo;
        { std::lock_guard lock(mapMutex); undo = settingsHistory.undo(); }
        if (undo) { setting = *undo; applyOptions(setting, false); }
        changed = false;
    }
    if (changed)
        applyOptions(setting);
    auto s = publication.load(std::memory_order_acquire);
    if (!s) {
        ImGui::TextUnformatted("地図を有効にしてワールドに入ると、読込み済みの地形を記録します。");
        ImGui::PopID();
        return;
    }
    ImGui::Text("キャッシュ %zu枚 / %.2f MiB / 走査 %.3f ms / %u取得・%u列", s->cacheTiles,
                s->cacheBytes / (1024. * 1024.), s->scanMilliseconds, s->reads, s->columns);
    ImGui::Text("更新 %.3f ms / 描画 %.3f ms / 初回 %.1f ms", s->publicationMilliseconds,
                s->renderMilliseconds, s->firstAppearanceMilliseconds);
    ImGui::Text("レーダー %.3f ms / %u確認", s->radarMilliseconds, s->radarVisited);
    ImGui::Text("要求の更新 %llu / 地形の更新 %llu", s->demandBuilds, s->terrainPublications);
    ImGui::TextWrapped("%s",storageStatusJa(s->persistenceStatus).c_str());
    bool open;
    {
        std::lock_guard lock(mapMutex);
        open = worldOpen;
    }
    if (ImGui::Checkbox("広域地図", &open)) {
        std::lock_guard lock(mapMutex);
        worldOpen = open;
    }
    static Waypoint draft{1, "Waypoint", 0, 64, 0, 0};
    static char name[97] = "Waypoint";
    static std::uint64_t editId{};
    static Session editingSession;
    if (editingSession != s->session) {
        editingSession = s->session;
        editId = 0;
        draft = {1, "Waypoint", s->playerX, s->playerY, s->playerZ, s->session.dimension};
        std::snprintf(name, sizeof(name), "%s", draft.name.c_str());
    }
    if (open) {
        double cx, cz, wz;
        {
            std::lock_guard lock(mapMutex);
            cx = worldX;
            cz = worldZ;
            wz = worldZoom;
        }
        if (ImGui::Button("自分を中心に")) {
            std::lock_guard lock(mapMutex);
            worldCentered = true;
        }
        float worldZoomValue = float(wz);
        if (ImGui::SliderFloat("広域地図の倍率", &worldZoomValue, 1.f, 8.f, "%.2fx")) {
            std::lock_guard lock(mapMutex);
            worldZoom = worldZoomValue;
        }
        auto available = ImGui::GetContentRegionAvail();
        float side = std::clamp(std::min(available.x, 420.f), 96.f, 600.f);
        auto start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("WorldMapCanvas", {side, side}, ImGuiButtonFlags_MouseButtonLeft);
        Canvas canvas{
            {{cx, cz}, {start.x + side / 2, start.y + side / 2}, side / 512. * wz, 0}, side / 2, false};
        configureCanvas(canvas, setting);
        canvas.viewMask = 2;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            auto delta = ImGui::GetIO().MouseDelta;
            if (!std::isfinite(delta.x) || !std::isfinite(delta.y))
                delta = {};
            std::lock_guard lock(mapMutex);
            worldCentered = false;
            worldX = std::clamp(worldX - std::clamp(double(delta.x), -double(side), double(side)) /
                                   canvas.transform.pixelsPerBlock, -double(coordinateLimit),
                                double(coordinateLimit));
            worldZ = std::clamp(worldZ - std::clamp(double(delta.y), -double(side), double(side)) /
                                   canvas.transform.pixelsPerBlock, -double(coordinateLimit),
                                double(coordinateLimit));
            canvas.transform.worldCenter = {worldX, worldZ};
        }
        if (ImGui::IsItemHovered()) {
            auto mouse = ImGui::GetIO().MousePos;
            auto wheel = ImGui::GetIO().MouseWheel;
            if (std::isfinite(wheel) && wheel != 0) {
                double zoom = std::clamp(wz * std::pow(1.2, std::clamp(double(wheel), -4., 4.)), 1., 8.);
                canvas.transform = canvas.transform.zoomAt({mouse.x, mouse.y}, side / 512. * zoom);
                std::lock_guard lock(mapMutex);
                worldZoom = zoom;
                worldX = canvas.transform.worldCenter.x;
                worldZ = canvas.transform.worldCenter.z;
                worldCentered = false;
            }
            auto p = canvas.transform.unproject({mouse.x, mouse.y});
            auto nearest = sampleAt(*s->cells, p, s->layer, s->caveTop, s->caveBottom, 2);
            Waypoint const *hover = nullptr;
            double distance = 64;
            // Picking needs one minimum, not a sorted allocation of every waypoint.
            for (std::size_t index = 0; index < std::min(s->waypoints.size(), maxWaypoints); ++index) {
                auto const &w = s->waypoints[index];
                if (!w.visible || w.dimension != s->session.dimension) continue;
                auto marker = canvas.transform.projectPixel({w.x, w.z}, canvas.snap);
                double dx = marker.x - mouse.x, dz = marker.z - mouse.y;
                if (dx * dx + dz * dz < distance && setting.waypointMarkers) {
                    hover = &w;
                    distance = dx * dx + dz * dz;
                }
            }
            if (hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                draft = *hover;
                editId = hover->id;
                std::snprintf(name, sizeof(name), "%s", hover->name.c_str());
            }
            if (nearest && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                draft = {1, "Waypoint", p.x, double(nearest->sample.height + 1), p.z, s->session.dimension};
                editId = 0;
                std::snprintf(name, sizeof(name), "%s", draft.name.c_str());
            }
            ImGui::BeginTooltip();
            ImGui::Text("X %.0f Z %.0f", p.x, p.z);
            if (nearest)
                ImGui::Text("Y %d / %s", int(nearest->sample.height), readableBiome(nearest->sample.biome.data()).c_str());
            else
                ImGui::TextUnformatted("未走査 / 未読込み");
            if (hover)
                ImGui::Text("%s (クリックで編集)", hover->name.c_str());
            else if (nearest)
                ImGui::TextUnformatted("右クリックで、この読込み済み地点を追加する下書きを作ります。");
            ImGui::EndTooltip();
        }
        drawMapCanvas(*ImGui::GetWindowDrawList(), *s, canvas);
    }
    ImGui::SeparatorText("地点一覧");
    if (!s->waypointsEditable)
        ImGui::TextUnformatted("地点を読込み中、または保存エラー後の保護中です。");
    ImGui::BeginDisabled(!s->waypointsEditable);
    ImGui::InputText("名前", name, sizeof(name));
    ImGui::InputDouble("X", &draft.x);
    ImGui::InputDouble("Y", &draft.y);
    ImGui::InputDouble("Z", &draft.z);
    ImGui::InputInt("次元", &draft.dimension);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("0: 地上 / 1: ネザー / 2: ジ・エンド。地点を編集してから「地点を保存」で確定します。");
    ImGui::Checkbox("地図に表示", &draft.visible);
    float color[4];
    auto rgba = ImGui::ColorConvertU32ToFloat4(draft.color);
    color[0] = rgba.x;
    color[1] = rgba.y;
    color[2] = rgba.z;
    color[3] = rgba.w;
    if (ImGui::ColorEdit4("色", color))
        draft.color = ImGui::ColorConvertFloat4ToU32({color[0], color[1], color[2], color[3]});
    if (ImGui::Button("自分の位置を使用")) {
        draft.x = s->playerX;
        draft.y = s->playerY;
        draft.z = s->playerZ;
        draft.dimension = s->session.dimension;
    }
    ImGui::SameLine();
    if (ImGui::Button(editId ? "地点を保存" : "地点を追加")) {
        draft.name = name;
        draft.id =
            editId ? editId : std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
        if (validWaypoint(draft)) {
            queueCommand({s->session, editId ? Command::Edit : Command::Add, draft});
            editId = 0;
        }
    }
    static bool sameDimension = true;
    ImGui::Checkbox("今の次元だけ", &sameDimension);
    auto order = orderedWaypoints(s->waypoints, s->session.dimension, {s->playerX, s->playerZ}, false);
    if (!sameDimension) {
        order.clear();
        for (std::size_t i = 0; i < s->waypoints.size(); ++i)
            order.push_back(i);
        std::sort(order.begin(), order.end(), [&](auto a, auto b) {
            auto const &wa = s->waypoints[a], &wb = s->waypoints[b];
            bool da = wa.dimension == s->session.dimension, db = wb.dimension == s->session.dimension;
            if (da != db) return da;
            return std::hypot(wa.x - s->playerX, wa.z - s->playerZ) <
                   std::hypot(wb.x - s->playerX, wb.z - s->playerZ);
        });
    }
    ImGuiListClipper clipper;
    clipper.Begin(int(order.size()), ImGui::GetFrameHeightWithSpacing());
    while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        auto const &w = s->waypoints[order[row]];
        ImGui::PushID(reinterpret_cast<void *>(std::uintptr_t(w.id)));
        char distance[64];
        if (w.dimension == s->session.dimension)
            std::snprintf(distance,sizeof(distance),"%.0fブロック",std::hypot(w.x-s->playerX,w.z-s->playerZ));
        else
            std::snprintf(distance,sizeof(distance),"別の次元");
        ImGui::Text("%s: %.0f %.0f %.0f [次元%d] %s", w.name.c_str(), w.x, w.y, w.z, w.dimension, distance);
        ImGui::SameLine();
        bool visible = w.visible;
        if (ImGui::Checkbox("表示", &visible)) {
            auto next = w;
            next.visible = visible;
            queueCommand({s->session, Command::Edit, next});
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("中心へ") && w.dimension == s->session.dimension) {
            std::lock_guard lock(mapMutex);
            worldOpen = true;
            worldCentered = false;
            worldX = w.x;
            worldZ = w.z;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("編集")) {
            draft = w;
            editId = w.id;
            std::snprintf(name, sizeof(name), "%s", w.name.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("削除"))
            queueCommand({s->session, Command::Remove, w});
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}
} // namespace praxis::companion
