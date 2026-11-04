#include "map/MapCore.h"
#include "map/chiyan/TerrainPalette.h"
#include <bit>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace praxis::companion::map {
namespace {
bool validKey(TileKey const &);
bool validSample(Sample const &s) {
    return s.height >= -2048 && s.height <= 2047 &&
           std::find(s.biome.begin(), s.biome.end(), char{}) != s.biome.end();
}
} // namespace
std::size_t TileHash::operator()(TileKey const &k) const noexcept {
    std::size_t h = 0;
    for (int v : {k.x, k.z, k.dimension, k.caveTop, k.caveBottom, int(k.layer)})
        h ^= std::hash<int>{}(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}
Point Transform::project(Point p) const noexcept {
    auto const x = p.x - worldCenter.x, z = p.z - worldCenter.z, c = std::cos(angle), s = std::sin(angle);
    return {screenCenter.x + (c * x - s * z) * pixelsPerBlock,
            screenCenter.z + (s * x + c * z) * pixelsPerBlock};
}
Point Transform::unproject(Point p) const noexcept {
    if (!std::isfinite(pixelsPerBlock) || pixelsPerBlock <= 0)
        return worldCenter;
    auto const x = (p.x - screenCenter.x) / pixelsPerBlock, z = (p.z - screenCenter.z) / pixelsPerBlock,
               c = std::cos(angle), s = std::sin(angle);
    return {worldCenter.x + c * x + s * z, worldCenter.z - s * x + c * z};
}
Point Transform::projectPixel(Point p, bool snap) const noexcept {
    auto q = project(p);
    if (snap && pixelsPerBlock < 1) {
        q.x = std::round(q.x);
        q.z = std::round(q.z);
    }
    return q;
}
Transform Transform::zoomAt(Point pointer, double scale) const noexcept {
    if (!std::isfinite(scale) || scale <= 0 || scale > 128 ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.z))
        return *this;
    auto before = unproject(pointer);
    auto result = *this;
    result.pixelsPerBlock = scale;
    auto after = result.unproject(pointer);
    result.worldCenter.x = std::clamp(worldCenter.x + before.x - after.x,
                                    -double(coordinateLimit), double(coordinateLimit));
    result.worldCenter.z = std::clamp(worldCenter.z + before.z - after.z,
                                    -double(coordinateLimit), double(coordinateLimit));
    return result;
}
bool Settings::valid() const noexcept {
    return radius >= 16 && radius <= 256 && pixelSize >= 96 && pixelSize <= 384 && columnsPerTick >= 1 &&
           columnsPerTick <= 128 && readsPerTick >= 1 && readsPerTick <= 4096 && maxTiles >= 16 &&
           maxTiles <= 1024 && std::isfinite(zoom) && zoom >= 0.25 && zoom <= 8 &&
           std::isfinite(scanMilliseconds) && scanMilliseconds > 0 && scanMilliseconds <= 4 &&
           caveDepth >= 4 && caveDepth <= 64 && refreshTicks >= 20 && refreshTicks <= 1200 &&
           radarMilliseconds >= 100 && radarMilliseconds <= 2000 &&
           int(anchor) <= int(Anchor::BottomLeft) && offsetX >= -4096 && offsetX <= 4096 &&
           offsetY >= -4096 && offsetY <= 4096 &&
           std::all_of(actionKeys.begin(), actionKeys.end(), [](int key) { return key >= 0 && key <= 255; });
}
Cache::Cache(std::size_t n, std::size_t bytes)
    : maxTiles_(std::clamp<std::size_t>(n, 1, 1024)), maxBytes_(bytes) {}
void Cache::setTileLimit(std::size_t n) {
    maxTiles_ = std::clamp<std::size_t>(n, 1, 1024);
    while (entries_.size() > maxTiles_) {
        auto oldest = std::min_element(entries_.begin(), entries_.end(), [](auto const &a, auto const &b) {
            return a.second.use < b.second.use;
        });
        entries_.erase(oldest);
        ++knownStateRevision_;
        ++revision_;
    }
}
void Cache::begin(Session const &s) {
    if (session_ != s) {
        entries_.clear();
        ++knownStateRevision_;
        session_ = s;
        ++revision_;
        clock_ = 0;
    }
}
std::size_t Cache::bytes() const noexcept {
    return entries_.size() * (sizeof(Entry) + sizeof(TileKey) + 128) + session_.world.capacity();
}
bool Cache::admit(TileKey k) {
    if (entries_.contains(k))
        return true;
    auto const cost = sizeof(Entry) + sizeof(TileKey) + 128;
    if (cost + session_.world.capacity() > maxBytes_)
        return false;
    while (entries_.size() >= maxTiles_ || bytes() + cost > maxBytes_) {
        if (entries_.empty())
            return false;
        auto oldest = std::min_element(entries_.begin(), entries_.end(), [](auto const &a, auto const &b) {
            return a.second.use < b.second.use;
        });
        entries_.erase(oldest);
        ++knownStateRevision_;
        ++revision_;
    }
    Entry e;
    e.tile.key = k;
    e.use = ++clock_;
    entries_.emplace(k, std::move(e));
    return true;
}
bool Cache::apply(Session const &s, TileKey k, int x, int z, Sample value, std::uint64_t tick) {
    value.biome.back() = 0;
    if (!s.valid() || s != session_ || !validKey(k) || !validSample(value) || k.dimension != s.dimension ||
        tileKey(x, z, k.dimension, k.layer, k.caveTop, k.caveBottom) != k || !admit(k))
        return false;
    auto &e = entries_.at(k);
    auto i = cellIndex(x, z);
    if(e.tile.samples[i].known&&!value.known)++knownStateRevision_;
    value.biome.back() = 0;
    if (e.tile.samples[i] != value) {
        e.tile.samples[i] = value;
        e.tile.revision = ++revision_;
    }
    e.tile.sampledAt[i] = tick;
    e.use = ++clock_;
    return true;
}
std::optional<Sample> Cache::find(TileKey k, int x, int z, std::uint64_t) {
    auto it = entries_.find(k);
    if (it == entries_.end())
        return {};
    it->second.use = ++clock_;
    auto value = it->second.tile.samples[cellIndex(x, z)];
    return value.known ? std::optional<Sample>{value} : std::nullopt;
}
bool Cache::fresh(TileKey k, int x, int z, std::uint64_t tick, std::uint64_t ttl) const {
    auto it = entries_.find(k);
    if (it == entries_.end())
        return false;
    auto i = cellIndex(x, z), at = it->second.tile.sampledAt[i];
    return it->second.tile.samples[i].known && tick >= at && tick - at < ttl;
}
bool Cache::known(TileKey k,int x,int z) const noexcept {
    auto it=entries_.find(k);
    return it!=entries_.end()&&it->second.tile.samples[cellIndex(x,z)].known;
}
bool Cache::restore(Session const &s, Tile const &t) {
    if (s != session_ || !s.valid() || !validKey(t.key) || t.key.dimension != s.dimension ||
        !std::all_of(t.samples.begin(), t.samples.end(), validSample) || !admit(t.key))
        return false;
    auto &e = entries_.at(t.key);
    // Historical disk data cannot overwrite a newer live sample in this session.
    bool changed = false;
    for (std::size_t i = 0; i < t.samples.size(); ++i)
        if (!e.tile.samples[i].known && t.samples[i].known) {
            e.tile.samples[i] = t.samples[i];
            e.tile.sampledAt[i] = 0;
            changed = true;
        }
    e.use = ++clock_;
    if (changed)
        e.tile.revision = ++revision_;
    return true;
}
std::vector<Tile> Cache::exportTiles(std::size_t n) const {
    n = std::min(n, entries_.size());
    std::vector<Entry const *> recent;
    recent.reserve(entries_.size());
    for (auto const &[_, e] : entries_)
        recent.push_back(&e);
    std::partial_sort(recent.begin(), recent.begin() + std::ptrdiff_t(n), recent.end(),
                      [](auto a, auto b) { return a->use > b->use; });
    std::vector<Tile> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(recent[i]->tile);
    return out;
}
namespace {
int viewStride(View const &) { return 1; }
bool validView(View const &v) {
    return coordinate(v.x) && coordinate(v.z) && v.radius >= 1 && v.radius <= 256 &&
           (v.layer == Layer::Surface || (v.layer == Layer::Cave && v.bottom >= -2048 && v.top <= 2047 &&
                                          v.top >= v.bottom && std::int64_t(v.top) - v.bottom <= 64));
}
bool validKey(TileKey const &k) {
    return std::abs(std::int64_t(k.x)) <= coordinateLimit / 16 + 1 &&
           std::abs(std::int64_t(k.z)) <= coordinateLimit / 16 + 1 && k.dimension >= -128 &&
           k.dimension <= 128 &&
           ((k.layer == Layer::Surface && !k.caveTop && !k.caveBottom) ||
            (k.layer == Layer::Cave && k.caveBottom >= -2048 && k.caveTop <= 2047 &&
             k.caveTop >= k.caveBottom && std::int64_t(k.caveTop) - k.caveBottom <= 64));
}
std::vector<std::pair<int, int>> coordinates(View v, std::size_t max) {
    std::vector<std::pair<int, int>> out;
    if (!validView(v))
        return out;
    int const stride = viewStride(v);
    int const r = (v.radius + stride - 1) / stride;
    int const anchorX = floorDiv(v.x, stride) * stride, anchorZ = floorDiv(v.z, stride) * stride;
    out.reserve(std::min<std::size_t>(max, std::size_t((r * 2 + 1) * (r * 2 + 1))));
    auto emit = [&](int x, int z) {
        if (out.size() < max) {
            auto px = std::clamp(anchorX + x * stride, -coordinateLimit, coordinateLimit),
                 pz = std::clamp(anchorZ + z * stride, -coordinateLimit, coordinateLimit);
            out.emplace_back(px, pz);
        }
    };
    emit(0, 0);
    for (int ring = 1; ring <= r && out.size() < max; ++ring) {
        for (int x = -ring; x <= ring; ++x) {
            emit(x, -ring);
            emit(x, ring);
        }
        for (int z = -ring + 1; z < ring; ++z) {
            emit(-ring, z);
            emit(ring, z);
        }
    }
    return out;
}
std::vector<View> canonicalViews(std::span<View const> views) {
    std::vector<View> out;
    out.reserve(2);
    for (auto v : views.first(std::min<std::size_t>(2, views.size()))) {
        if (!validView(v))
            continue;
        int stride = viewStride(v);
        v.x = std::clamp(floorDiv(v.x, stride) * stride, -coordinateLimit, coordinateLimit);
        v.z = std::clamp(floorDiv(v.z, stride) * stride, -coordinateLimit, coordinateLimit);
        if (v.layer == Layer::Surface)
            v.top = v.bottom = 0;
        out.push_back(v);
    }
    return out;
}
} // namespace
std::vector<Cell> Cache::view(std::span<View const> views, std::size_t n) {
    n = std::min(n, maxCells);
    std::vector<Cell> out;
    auto plan = canonicalViews(views);
    if (plan.empty()) return out;
    std::size_t demand = 0;
    for (auto const &v : plan) demand += std::size_t(2 * v.radius + 1) * (2 * v.radius + 1);
    auto append = [&](int x, int z, TileKey key, std::uint8_t mask) {
        auto s = find(key,x,z,0);
        if (!s) return;
        auto north = find(tileKey(x,z-1,session_.dimension,key.layer,key.caveTop,key.caveBottom),x,z-1,0);
        auto west = find(tileKey(x-1,z,session_.dimension,key.layer,key.caveTop,key.caveBottom),x-1,z,0);
        auto color = s->color;
        auto shaded = chiyan::shadeTerrain({float(color&255)/255,float((color>>8)&255)/255,float((color>>16)&255)/255,1},
            s->height,north?north->height:s->height,west?west->height:s->height);
        s->color = packMapColor(shaded.r,shaded.g,shaded.b,shaded.a).value_or(color);
        out.push_back({x,z,1,*s,key,mask,{1,1}});
    };
    if (n >= demand) {
        // Full worker views visit resident samples once, including overlapping
        // MiniMap/WorldMap cells. Unknown pixels stay absent. No coordinate list
        // or hash lookup for every unscanned position in a 513x513 view.
        out.reserve(std::min(demand, entries_.size() * tileSide * tileSide));
        for (auto const &[key, entry] : entries_) {
            if (key.dimension != session_.dimension) continue;
            int tileX = key.x * tileSide, tileZ = key.z * tileSide;
            std::uint8_t eligible = 0;
            for (std::size_t i = 0; i < plan.size(); ++i) {
                auto const &v = plan[i];
                if (key.layer == v.layer && key.caveTop == v.top && key.caveBottom == v.bottom &&
                    tileX <= v.x + v.radius && tileX + tileSide - 1 >= v.x - v.radius &&
                    tileZ <= v.z + v.radius && tileZ + tileSide - 1 >= v.z - v.radius) eligible |= std::uint8_t(1u << i);
            }
            if (!eligible) continue;
            for (int iz = 0; iz < tileSide; ++iz) for (int ix = 0; ix < tileSide; ++ix) {
                if (!entry.tile.samples[std::size_t(iz * tileSide + ix)].known) continue;
                int x = key.x * tileSide + ix, z = key.z * tileSide + iz;
                if (!coordinate(x) || !coordinate(z)) continue;
                std::uint8_t mask = 0;
                for (std::size_t i = 0; i < plan.size(); ++i) {
                    auto const &v = plan[i];
                    if ((eligible & (1u << i)) &&
                        std::abs(x-v.x) <= v.radius && std::abs(z-v.z) <= v.radius) mask |= std::uint8_t(1u << i);
                }
                if (mask) append(x,z,key,mask);
            }
        }
    } else {
        // Explicitly capped callers retain the original centre-first limit.
        std::uint8_t mask = 0;
        for (auto const &v : plan) {
            mask = mask ? 2 : 1;
            for (auto [x, z] : coordinates(v, n - out.size()))
                append(x,z,tileKey(x,z,session_.dimension,v.layer,v.top,v.bottom),mask);
        }
    }
    std::sort(out.begin(),out.end(),[](Cell const &a,Cell const &b) {
        if (a.key.layer != b.key.layer) return a.key.layer < b.key.layer;
        if (a.key.caveTop != b.key.caveTop) return a.key.caveTop < b.key.caveTop;
        if (a.key.caveBottom != b.key.caveBottom) return a.key.caveBottom < b.key.caveBottom;
        return a.z < b.z || (a.z == b.z && a.x < b.x);
    });
    auto end = out.begin();
    for (auto it=out.begin();it!=out.end();++it) {
        if (end!=out.begin()&&(end-1)->x==it->x&&(end-1)->z==it->z&&(end-1)->key==it->key)
            (end-1)->viewMask |= it->viewMask;
        else { if(end!=it)*end=*it; ++end; }
    }
    out.erase(end,out.end());
    return out;
}
Scanner::Request Scanner::requestAt(std::size_t index) const noexcept {
    for (std::size_t i = 0; i < viewCount_; ++i) {
        auto const &v = views_[i];
        auto size = std::size_t(2 * v.radius + 1) * (2 * v.radius + 1);
        if (index >= size) { index -= size; continue; }
        int ring = (int(std::sqrt(double(index))) + 1) / 2;
        int x = 0, z = 0;
        if (ring) {
            auto first = std::size_t(2 * ring - 1) * (2 * ring - 1);
            chiyan::GetRingCoords(ring, int(index - first), x, z);
        }
        return {std::clamp(v.x + x, -coordinateLimit, coordinateLimit),
                std::clamp(v.z + z, -coordinateLimit, coordinateLimit),
                0, v.top, v.bottom, v.layer, 0, false, 0, {}};
    }
    return {};
}
void Scanner::request(Session const &s, std::span<View const> views) {
    if (!s.valid()) { cancel(); return; }
    auto plan=canonicalViews(views);
    if(session_==s&&plan.size()==viewCount_&&std::equal(plan.begin(),plan.end(),views_.begin()))return;
    views=plan;
    ++builds_;
    auto previous = current_;
    auto previousMissing=missingCurrent_;
    bool sameSession = s == session_ && demand_;
    // Reuse only the complete known squares inside the previous prefix.
    // Their intersection after recentering is still known; eviction is
    // checked against Cache's loss-only revision before any bulk skip.
    std::array<std::size_t,2> knownCounts{};
    if(sameSession&&plan.size()==viewCount_) {
        if(viewCount_==1)knownCounts[0]=verifiedPrefix_;
        else {
            auto a=std::size_t(2*views_[0].radius+1)*(2*views_[0].radius+1);
            auto b=std::size_t(2*views_[1].radius+1)*(2*views_[1].radius+1);
            auto interleaved=std::min(verifiedPrefix_,2*std::min(a,b));
            knownCounts={(interleaved+1)/2,interleaved/2};
            if(verifiedPrefix_>interleaved)knownCounts[a>b?0:1]+=verifiedPrefix_-interleaved;
        }
        for(std::size_t i=0;i<viewCount_;++i) {
            auto const& before=views_[i];auto const& after=plan[i];
            if(!knownCounts[i]||before.radius!=after.radius||before.layer!=after.layer||
               before.top!=after.top||before.bottom!=after.bottom){knownCounts[i]=0;continue;}
            int side=int(std::sqrt(double(knownCounts[i])));if(!(side&1))--side;
            int radius=(side-1)/2-std::max(std::abs(before.x-after.x),std::abs(before.z-after.z));
            knownCounts[i]=radius<0?0:std::size_t(2*radius+1)*(2*radius+1);
        }
    }
    views_ = {}; viewCount_ = 0; demand_ = 0;
    for (auto const &v : views) {
        if (!validView(v)) continue;
        if (viewCount_ == views_.size()) break;
        views_[viewCount_++] = v;
        demand_ += std::size_t(2 * v.radius + 1) * (2 * v.radius + 1);
    }
    cursor_ = 0;
    bool retained = false;
    std::size_t base = 0;
    if (sameSession) for (std::size_t i = 0; i < viewCount_; ++i) {
        auto const &v = views_[i];
        int x = previous.x - v.x, z = previous.z - v.z;
        int ring = std::max(std::abs(x), std::abs(z));
        if (previous.layer == v.layer && ring <= v.radius &&
            (v.layer == Layer::Surface || (previous.top == v.top && previous.bottom == v.bottom))) {
            int offset = 0;
            if (ring) {
                if (z == -ring && x < ring) offset = x + ring;
                else if (x == ring && z < ring) offset = 2 * ring + z + ring;
                else if (z == ring && x > -ring) offset = 4 * ring + ring - x;
                else offset = 6 * ring + ring - z;
            }
            cursor_ = base + (ring ? std::size_t(2 * ring - 1) * (2 * ring - 1) + offset : 0);
            retained = true;
            break;
        }
        base += std::size_t(2 * v.radius + 1) * (2 * v.radius + 1);
    }
    current_ = retained && previous.phase ? previous : requestAt(cursor_);
    session_ = s;
    verifiedPrefix_=knownCounts[0];
    if(viewCount_==2) {
        auto shared=std::min(std::size_t(2*views_[0].radius+1)*(2*views_[0].radius+1),
                             std::size_t(2*views_[1].radius+1)*(2*views_[1].radius+1));
        auto common=std::min(knownCounts[0],knownCounts[1]);
        verifiedPrefix_=common==shared?knownCounts[0]+knownCounts[1]:
            2*common+(knownCounts[0]>knownCounts[1]?1:0);
    }
    // Recenter first fills missing pixels near BOTH requested centres. The
    // independent refresh cursor/partial column above retains world-space
    // progress, so continuous movement cannot starve terrain refresh.
    missingCursor_=missingVisited_=0;missingRetryAt_=0;
    missingActive_=demand_!=0;missingCurrent_=missingAt(0);useMissing_=missingResumed_=false;
    if(sameSession&&previousMissing.phase)for(std::size_t i=0;i<viewCount_;++i) {
        auto const& v=views_[i];
        if(previousMissing.layer==v.layer&&std::abs(previousMissing.x-v.x)<=v.radius&&
           std::abs(previousMissing.z-v.z)<=v.radius&&
           (v.layer==Layer::Surface||(previousMissing.top==v.top&&previousMissing.bottom==v.bottom))) {
            missingCurrent_=previousMissing;useMissing_=missingResumed_=true;break;
        }
    }
}
Scanner::Request Scanner::missingAt(std::size_t index) const noexcept {
    if(viewCount_!=2)return requestAt(index);
    auto a=std::size_t(2*views_[0].radius+1)*(2*views_[0].radius+1);
    auto b=std::size_t(2*views_[1].radius+1)*(2*views_[1].radius+1);
    auto shared=std::min(a,b);
    // Round-robin ring positions: World Map's centre does not wait for the
    // entire MiniMap square to finish.
    if(index<2*shared)return requestAt(index%2?a+index/2:index/2);
    return requestAt(a>b?shared+index-2*shared:a+shared+index-2*shared);
}
void Scanner::cancel() noexcept {
    views_ = {}; viewCount_ = demand_ = cursor_ = 0;
    current_ = {}; session_ = {};
    missingCurrent_={};missingCursor_=missingVisited_=0;missingRetryAt_=schedulingColumn_=0;
    missingActive_=useMissing_=missingResumed_=false;
    verifiedPrefix_=0;knownStateRevision_=0;
}
ScanStats Scanner::tick(Cache &cache, DataSource &source, Settings const &settings, std::uint64_t tick) {
    ScanStats stats;
    auto start = std::chrono::steady_clock::now();
    if (!settings.valid() || session_ != cache.session() || !demand_)
        return stats;
    std::size_t visited = 0;
    if(!missingActive_&&tick>=missingRetryAt_) {
        missingActive_=true;missingCursor_=missingVisited_=0;missingCurrent_=missingAt(0);
    }
    while (stats.reads < unsigned(settings.readsPerTick) &&
           stats.columns < unsigned(settings.columnsPerTick) && visited < demand_) {
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >=
            settings.scanMilliseconds)
            {stats.stop=ScanStats::Time;break;}
        // Select only between columns. Seven missing columns share one
        // refresh reservation; all native reads/time/columns use ONE budget.
        if(!current_.phase&&!missingCurrent_.phase)
            useMissing_=missingActive_&&(schedulingColumn_%8!=7);
        if(!missingActive_)useMissing_=false;
        if(knownStateRevision_!=cache.knownStateRevision()) {
            verifiedPrefix_=0;knownStateRevision_=cache.knownStateRevision();
        }
        if(useMissing_&&!missingCurrent_.phase&&missingCursor_<verifiedPrefix_) {
            // Preserve each refresh reservation and the original logical
            // skipped/visited accounting. At most seven proven-known cells.
            auto count=std::min({verifiedPrefix_-missingCursor_,
                std::size_t(7-schedulingColumn_%8),demand_-visited,demand_-missingVisited_});
            stats.skipped+=std::uint32_t(count);visited+=count;schedulingColumn_+=count;
            stats.prefixSkipped+=std::uint32_t(count);
            missingCursor_=(missingCursor_+count)%demand_;missingVisited_+=count;
            missingCurrent_=missingAt(missingCursor_);
            if(missingVisited_>=demand_){missingActive_=false;missingRetryAt_=tick+20;}
            continue;
        }
        auto &r = useMissing_?missingCurrent_:current_;
        auto key = tileKey(r.x, r.z, session_.dimension, r.layer, r.top, r.bottom);
        auto advance = [&](bool known=false) {
            ++schedulingColumn_; // Include unavailable/fresh attempts in fairness.
            r.phase = 0;
            r.airSeen = false;
            r.sample = {};
            r.waterColor=0;r.waterBottom=0;r.verticalReads=0;
            if(useMissing_) {
                if(missingResumed_)missingResumed_=false;
                else {
                    if(known&&missingCursor_==verifiedPrefix_)++verifiedPrefix_;
                    missingCursor_=(missingCursor_+1)%demand_;++missingVisited_;
                }
                missingCurrent_=missingAt(missingCursor_);
                if(missingVisited_>=demand_) {
                    missingActive_=false;missingRetryAt_=tick+20;
                }
            } else {
                cursor_ = (cursor_ + 1) % demand_;
                current_ = requestAt(cursor_);
            }
            ++visited;
        };
        if (r.phase == 0 && (useMissing_?cache.known(key,r.x,r.z):cache.fresh(key, r.x, r.z, tick, settings.refreshTicks))) {
            ++stats.skipped;
            advance(useMissing_);
            continue;
        }
        if (r.phase == 0) {
            if(useMissing_)++stats.missingAttempts;else ++stats.refreshAttempts;
            if (r.layer == Layer::Surface) {
                auto cost=std::max(1u,source.queryCost(DataSource::Query::Height));
                if(cost>unsigned(settings.readsPerTick)-stats.reads){stats.stop=ScanStats::Reads;break;}
                stats.reads+=cost;
                auto height = source.surfaceColumn(r.x, r.z);
                if (!height || height->top < -2048 || height->top > 2047 || height->bottom < -2048 || height->bottom > height->top) {
                    ++stats.unavailable;
                    advance();
                    continue;
                }
                r.y = height->overlay.value_or(height->top);
                if(r.y<height->top||r.y>2047) { ++stats.unavailable;advance();continue; }
                r.bottom = height->bottom;
            } else
                r.y = r.top;
            r.phase = 1;
        } else if (r.phase == 1) {
            if(r.layer==Layer::Cave&&r.verticalReads>=64){advance();continue;}
            auto cost=std::max(1u,source.queryCost(DataSource::Query::Voxel));
            if(cost>unsigned(settings.readsPerTick)-stats.reads){stats.stop=ScanStats::Reads;break;}
            stats.reads+=cost;++r.verticalReads;
            auto voxel = source.voxel(r.x, r.y, r.z);
            if (!voxel) {
                ++stats.unavailable;
                advance();
                continue;
            }
            if (voxel->air || (r.layer==Layer::Cave&&voxel->cavePassable)) {
                ++stats.air;
                r.airSeen = true;
            } else if (!voxel->mapVisible) ++stats.noColor;
            if(voxel->water&&r.layer==Layer::Cave)r.waterColor=voxel->color;
            if (!voxel->air && voxel->mapVisible && (r.layer == Layer::Surface || (r.airSeen&&!voxel->cavePassable))) {
                r.sample.color = voxel->color;
                if(r.layer==Layer::Cave&&r.waterColor) {
                    auto channel=[&](int shift){return (((voxel->color>>shift)&255)*2+((r.waterColor>>shift)&255))/3;};
                    r.sample.color=0xff000000u|channel(0)|(channel(8)<<8)|(channel(16)<<16);
                }
                r.sample.height = std::int16_t(r.y);
                r.sample.known = true;
                r.phase = voxel->water?3:2;
                if(voxel->water) {
                    r.waterColor=voxel->color;r.waterBottom=std::max(r.bottom,r.y-64);r.y=std::max(r.bottom,r.y-1);
                }
            } else if (--r.y < r.bottom) {
                ++stats.exhausted;
                advance();
            }
        } else if(r.phase==3) {
            auto cost=std::max(1u,source.queryCost(DataSource::Query::Voxel));
            if(cost>unsigned(settings.readsPerTick)-stats.reads){stats.stop=ScanStats::Reads;break;}
            stats.reads+=cost;
            auto voxel=source.voxel(r.x,r.y,r.z);
            if(!voxel) { ++stats.unavailable;advance();continue; }
            if((!voxel->air&&voxel->mapVisible&&!voxel->water&&!voxel->waterPassable)||r.y<=r.waterBottom) {
                auto unpack=[](std::uint32_t c){return chiyan::Color{float(c&255)/255,float((c>>8)&255)/255,float((c>>16)&255)/255,1};};
                auto floor=(!voxel->air&&voxel->mapVisible&&!voxel->water)?unpack(voxel->color):chiyan::Color{.08f,.08f,.08f,1};
                auto blended=chiyan::BlendWaterOverFloor(floor,unpack(r.waterColor));
                r.sample.color=packMapColor(blended.r,blended.g,blended.b,blended.a).value_or(r.waterColor);
                r.sample.height=std::int16_t(r.y);r.phase=2;
            } else --r.y;
        } else {
            auto cost=std::max(1u,source.queryCost(DataSource::Query::Biome));
            if(cost>unsigned(settings.readsPerTick)-stats.reads){stats.stop=ScanStats::Reads;break;}
            stats.reads+=cost;
            r.sample.biome = source.biome(r.x, r.y, r.z);
            bool cached=cache.apply(session_, key, r.x, r.z, r.sample, tick);
            source.completed(r.x,r.z,r.sample);
            if(r.sample.color==0xff000000u)++stats.black;
            ++stats.columns;
            if(useMissing_)++stats.missing;else ++stats.refreshed;
            advance(cached);
        }
    }
    stats.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stats.visited=std::uint32_t(visited);
    if(stats.stop!=ScanStats::Time&&stats.stop!=ScanStats::Reads)
        stats.stop=stats.columns>=unsigned(settings.columnsPerTick)?ScanStats::Columns:
            stats.reads>=unsigned(settings.readsPerTick)?ScanStats::Reads:ScanStats::Sweep;
    return stats;
}
std::shared_ptr<std::vector<Cell> const> TerrainPublication::get(Cache &cache, std::span<View const> views) {
    auto plan = canonicalViews(views);
    if (!cells_ || session_ != cache.session() || revision_ != cache.revision() || views_ != plan) {
        cells_ = std::make_shared<std::vector<Cell> const>(cache.view(plan));
        session_ = cache.session();
        revision_ = cache.revision();
        views_ = std::move(plan);
        ++builds_;
    }
    return cells_;
}
void TerrainPublication::clear() noexcept {
    cells_.reset();
    views_.clear();
    session_ = {};
}
std::pair<int, int> CaveBand::select(double y, int depth) noexcept {
    depth = std::clamp(depth, 4, 64);
    y = std::isfinite(y) ? std::clamp(y, -2048., 2047.) : 0;
    if (top_ == 2048 || depth_ != depth || y > top_ - 1 || y < top_ - 5)
        top_ = std::clamp(floorDiv(int(std::floor(y)), 4) * 4 + 4, -2048 + depth, 2047);
    depth_ = depth;
    return {top_, top_ - depth};
}
void SettingsHistory::remember(Settings const &s) {
    if (!s.valid() || (!history_.empty() && history_.back() == s))
        return;
    if (history_.size() == 16)
        history_.erase(history_.begin());
    history_.push_back(s);
}
std::optional<Settings> SettingsHistory::undo() {
    if (history_.empty())
        return {};
    auto s = history_.back();
    history_.pop_back();
    return s;
}
std::optional<Waypoint> DeathTracker::observe(Session const &s, double x, double y, double z, bool alive) {
    if (s != session_) {
        session_ = s;
        wasAlive_ = false;
    }
    if (!s.valid() || !coordinate(x) || !coordinate(z) || !std::isfinite(y) || std::abs(y) > 2048)
        return {};
    if (alive)
        last_ = {std::numeric_limits<std::uint64_t>::max() - std::uint64_t(s.dimension + 128),
                 "Last death", x, y, z, s.dimension, 0xff5555ffu};
    bool edge = wasAlive_ && !alive;
    wasAlive_ = alive;
    return edge ? std::optional{last_} : std::nullopt;
}
Point minimapCenter(Settings const &s, Point display) noexcept {
    double half = s.pixelSize / 2., text = (s.coordinates ? 60 : 0) + (s.biomeText ? 20 : 0);
    bool left = s.anchor == Anchor::TopLeft || s.anchor == Anchor::BottomLeft;
    bool bottom = s.anchor == Anchor::BottomLeft || s.anchor == Anchor::BottomRight;
    return {std::clamp(left ? half + s.offsetX : display.x - half - s.offsetX,
                       half, std::max(half, display.x - half)),
            std::clamp(bottom ? display.z - half - text - s.offsetY : half + s.offsetY,
                       half, std::max(half, display.z - half - text))};
}
Cell const *sampleAt(std::span<Cell const> cells, Point p, Layer layer, int top, int bottom, std::uint8_t mask) noexcept {
    Cell const *best = nullptr;
    for (auto const &c : cells.first(std::min(cells.size(), maxCells))) {
        double half = c.strideFor(mask) / 2.;
        if (!(c.viewMask & mask) || !c.sample.known || std::abs(p.x - c.x) > half || std::abs(p.z - c.z) > half)
            continue;
        if (c.key.layer != layer || (layer == Layer::Cave &&
            (c.key.caveTop != top || c.key.caveBottom != bottom)))
            continue;
        if (!best || c.strideFor(mask) < best->strideFor(mask))
            best = &c;
    }
    return best;
}
Settings actionSettings(Settings const &old, MapAction action) noexcept {
    auto s = old;
    switch (action) {
    case MapAction::ToggleMinimap:
        if (!s.enabled) s.enabled = s.minimap = true;
        else s.minimap = !s.minimap;
        break;
    case MapAction::ZoomIn: s.zoom = std::min(8., s.zoom * 1.25); break;
    case MapAction::ZoomOut: s.zoom = std::max(.25, s.zoom / 1.25); break;
    case MapAction::ToggleCave: s.cave = !s.cave; break;
    default: break;
    }
    return s;
}
std::string readableBiome(std::string_view raw) {
    if (auto colon = raw.find(':'); colon != std::string_view::npos)
        raw.remove_prefix(colon + 1);
    if (raw.empty() || raw == "unknown")
        return "Unknown biome";
    std::string out(raw.substr(0, 63));
    bool start = true;
    for (auto &c : out) {
        if (c == '_') { c = ' '; start = true; }
        else if (start) { if (c >= 'a' && c <= 'z') c -= 'a' - 'A'; start = false; }
    }
    return out;
}
std::vector<std::size_t> orderedWaypoints(std::span<Waypoint const> w, int dimension, Point p, bool visible) {
    std::vector<std::size_t> out;
    out.reserve(std::min(w.size(), maxWaypoints));
    for (std::size_t i = 0; i < std::min(w.size(), maxWaypoints); ++i)
        if (w[i].dimension == dimension && (!visible || w[i].visible))
            out.push_back(i);
    auto distance = [&](std::size_t i) { auto dx = w[i].x - p.x, dz = w[i].z - p.z; return dx*dx + dz*dz; };
    std::sort(out.begin(), out.end(), [&](auto a, auto b) {
        auto da = distance(a), db = distance(b);
        return da == db ? w[a].id < w[b].id : da < db;
    });
    return out;
}
std::optional<std::uint32_t> mapColor(float r, float g, float b, float a) noexcept {
    for (float f : {r, g, b, a})
        if (!std::isfinite(f) || f < 0 || f > 1)
            return {};
    if (a <= 0)
        return {};
    auto byte = [](float f) { return std::uint32_t(std::lround(f * 255)); };
    return 0xff000000u | byte(r) | (byte(g) << 8) | (byte(b) << 16);
}
std::optional<std::uint32_t> ColorCache::find(std::uint64_t id) const {
    auto it = values_.find(id);
    return it == values_.end() ? std::nullopt : std::optional{it->second};
}
void ColorCache::store(std::uint64_t id, std::uint32_t value) {
    if (values_.size() >= 4096 && !values_.contains(id))
        values_.erase(values_.begin());
    values_[id] = value;
}
bool validWaypoint(Waypoint const &w) noexcept {
    if (!w.id || w.name.empty() || w.name.size() > 96 || !coordinate(w.x) || !coordinate(w.z) ||
        !std::isfinite(w.y) || std::abs(w.y) > 2048 || w.dimension < -128 || w.dimension > 128 ||
        (w.color >> 24) == 0)
        return false;
    for (unsigned char c : w.name)
        if (c < 32 || c == 127)
            return false;
    return true;
}
std::optional<std::string> localWorldIdentity(std::string_view path, std::string_view id, bool local) {
    if (!local || path.empty() || id.empty() || path.size() > maxWorldBytes || id.size() > maxWorldBytes)
        return {};
    for (auto text : {path, id})
        for (unsigned char c : text)
            if (c < 32 || c == 127)
                return {};
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    // Do lexical normalization only: no disk I/O in a Minecraft callback.
    auto storage = std::filesystem::u8path(normalized).lexically_normal();
    if (!storage.is_absolute())
        return {};
    auto utf8 = storage.generic_u8string();
    normalized.assign(reinterpret_cast<char const *>(utf8.data()), utf8.size());
    while (normalized.size() > 3 && normalized.back() == '/')
        normalized.pop_back();
    std::string key = "local-v2:" + std::to_string(normalized.size()) + ":" + normalized + ":" +
                      std::to_string(id.size()) + ":" + std::string(id);
    return key.size() <= maxWorldBytes ? std::optional<std::string>{std::move(key)} : std::nullopt;
}
bool Waypoints::add(Waypoint w) {
    if (!validWaypoint(w) || items_.size() >= maxWaypoints ||
        std::any_of(items_.begin(), items_.end(), [&](auto const &x) { return x.id == w.id; }))
        return false;
    w.name = std::string(w.name.data(), w.name.size());
    items_.push_back(std::move(w));
    ++revision_;
    return true;
}
bool Waypoints::edit(Waypoint w) {
    if (!validWaypoint(w))
        return false;
    w.name = std::string(w.name.data(), w.name.size());
    for (auto &old : items_)
        if (old.id == w.id) {
            old = std::move(w);
            ++revision_;
            return true;
        }
    return false;
}
bool Waypoints::remove(std::uint64_t id) {
    auto n = std::erase_if(items_, [&](auto const &w) { return w.id == id; });
    revision_ += n;
    return n != 0;
}
bool Waypoints::replace(std::vector<Waypoint> values) {
    Waypoints candidate;
    for (auto const &v : values)
        if (!candidate.add(v))
            return false;
    items_ = std::move(candidate.items_);
    ++revision_;
    return true;
}
void Waypoints::clear() {
    items_.clear();
    ++revision_;
}
namespace {
std::uint64_t checksum(std::span<std::uint8_t const> bytes) {
    std::uint64_t h = 14695981039346656037ull;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
struct Writer {
    Bytes data;
    void u(std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i)
            data.push_back(std::uint8_t(v >> (i * 8)));
    }
    void text(std::string_view s) {
        u(s.size(), 2);
        data.insert(data.end(), s.begin(), s.end());
    }
    void real(double v) { u(std::bit_cast<std::uint64_t>(v), 8); }
    void seal() { u(checksum(data), 8); }
};
struct Reader {
    std::span<std::uint8_t const> data;
    std::size_t at{};
    bool okay{true};
    std::uint64_t u(int n) {
        if (!okay || at + std::size_t(n) > data.size()) {
            okay = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= std::uint64_t(data[at++]) << (i * 8);
        return v;
    }
    std::string text(std::size_t max) {
        auto n = u(2);
        if (!okay || n > max || at + n > data.size()) {
            okay = false;
            return {};
        }
        std::string s(reinterpret_cast<char const *>(data.data() + at), std::size_t(n));
        at += n;
        return s;
    }
    double real() { return std::bit_cast<double>(u(8)); }
    bool header(std::uint32_t magic, std::string const &world, int version = 1) {
        return u(4) == magic && u(2) == unsigned(version) && text(maxWorldBytes) == world && okay;
    }
};
std::optional<Reader> reader(std::span<std::uint8_t const> data, std::size_t max) {
    if (data.size() < 8 || data.size() > max)
        return {};
    Reader tail{data.subspan(data.size() - 8)};
    if (checksum(data.first(data.size() - 8)) != tail.u(8))
        return {};
    return Reader{data.first(data.size() - 8)};
}
void header(Writer &w, std::uint32_t magic, std::string const &world, int version = 1) {
    w.u(magic, 4);
    w.u(version, 2);
    w.text(world);
}
} // namespace
std::string worldFilename(std::string const &world) {
    std::ostringstream s;
    s << std::hex << std::setfill('0') << std::setw(16)
      << checksum({reinterpret_cast<std::uint8_t const *>(world.data()), world.size()});
    return s.str();
}
Bytes encodeWaypoints(std::string const &world, std::span<Waypoint const> values) {
    if (world.empty() || world.size() > maxWorldBytes || values.size() > maxWaypoints)
        return {};
    Waypoints check;
    for (auto const &v : values)
        if (!check.add(v))
            return {};
    Writer w;
    header(w, 0x31575050u, world, 2);
    w.u(values.size(), 2);
    for (auto const &v : values) {
        w.u(v.id, 8);
        w.text(v.name);
        w.real(v.x);
        w.real(v.y);
        w.real(v.z);
        w.u(std::uint32_t(v.dimension), 4);
        w.u(v.color, 4);
        w.u(v.visible, 1);
    }
    w.seal();
    return w.data;
}
std::optional<std::vector<Waypoint>> decodeWaypoints(std::span<std::uint8_t const> data,
                                                     std::string const &world) {
    auto p = reader(data, 1024 * 1024);
    if (!p || data.size() < 6)
        return {};
    int version = data[4] | (int(data[5]) << 8);
    if ((version != 1 && version != 2) || !p->header(0x31575050u, world, version))
        return {};
    auto &r = *p;
    auto n = r.u(2);
    if (n > maxWaypoints)
        return {};
    Waypoints values;
    for (std::size_t i = 0; i < n && r.okay; ++i) {
        Waypoint v;
        v.id = r.u(8);
        v.name = r.text(96);
        v.x = r.real();
        v.y = r.real();
        v.z = r.real();
        v.dimension = int(std::int32_t(r.u(4)));
        v.color = std::uint32_t(r.u(4));
        if (version >= 2) {
            auto flag = r.u(1);
            if (flag > 1)
                return {};
            v.visible = flag != 0;
        }
        if (!r.okay || !values.add(std::move(v)))
            return {};
    }
    if (!r.okay || r.at != r.data.size())
        return {};
    return values.items();
}
Bytes encodeTiles(std::string const &world, std::span<Tile const> tiles) {
    if (world.empty() || world.size() > maxWorldBytes || tiles.size() > 512)
        return {};
    Writer w;
    header(w, 0x32435050u, world);
    w.u(tiles.size(), 2);
    std::unordered_set<TileKey, TileHash> unique;
    for (auto const &t : tiles) {
        auto k = t.key;
        if (!validKey(k) || !unique.insert(k).second)
            return {};
        for (int v : {k.x, k.z, k.dimension, k.caveTop, k.caveBottom})
            w.u(std::uint32_t(v), 4);
        w.u(int(k.layer), 1);
        for (auto const &s : t.samples) {
            if (s.height < -2048 || s.height > 2047 ||
                std::find(s.biome.begin(), s.biome.end(), char{}) == s.biome.end())
                return {};
            w.u(s.known, 1);
            w.u(s.color, 4);
            w.u(std::uint16_t(s.height), 2);
            w.text(std::string_view(s.biome.data(),
                                    std::find(s.biome.begin(), s.biome.end(), char{}) - s.biome.begin()));
        }
    }
    w.seal();
    return w.data;
}
std::optional<std::vector<Tile>> decodeTiles(std::span<std::uint8_t const> data, std::string const &world,
                                            std::size_t tileLimit, bool allowLegacy) {
    auto p = reader(data, 12 * 1024 * 1024);
    std::uint32_t magic = data.size()>=4 ? std::uint32_t(data[0])|(std::uint32_t(data[1])<<8)|
        (std::uint32_t(data[2])<<16)|(std::uint32_t(data[3])<<24) : 0;
    if ((magic!=0x32435050u&&!(allowLegacy&&magic==0x31435050u))||!p||!p->header(magic,world))
        return {};
    auto &r = *p;
    auto n = r.u(2);
    if (n > std::min<std::size_t>(512, tileLimit))
        return {};
    std::vector<Tile> tiles;
    tiles.reserve(std::size_t(n));
    std::unordered_set<TileKey, TileHash> unique;
    for (std::size_t i = 0; i < n && r.okay; ++i) {
        Tile t;
        t.key.x = int(std::int32_t(r.u(4)));
        t.key.z = int(std::int32_t(r.u(4)));
        t.key.dimension = int(std::int32_t(r.u(4)));
        t.key.caveTop = int(std::int32_t(r.u(4)));
        t.key.caveBottom = int(std::int32_t(r.u(4)));
        t.key.layer = Layer(r.u(1));
        if (!validKey(t.key) || !unique.insert(t.key).second)
            return {};
        for (auto &s : t.samples) {
            auto known = r.u(1);
            if (known > 1)
                return {};
            s.known = known != 0;
            s.color = std::uint32_t(r.u(4));
            s.height = std::int16_t(r.u(2));
            auto biome = r.text(63);
            std::copy(biome.begin(), biome.end(), s.biome.begin());
            if (s.height < -2048 || s.height > 2047)
                return {};
        }
        tiles.push_back(std::move(t));
    }
    if (!r.okay || r.at != r.data.size())
        return {};
    return tiles;
}
TileKey regionKey(TileKey k) noexcept {
    k.x = floorDiv(k.x, 4);
    k.z = floorDiv(k.z, 4);
    return k;
}
bool validRegionKey(TileKey const &k) noexcept {
    return validKey(k) && std::abs(std::int64_t(k.x)) <= coordinateLimit / tileSide / 4 + 1 &&
           std::abs(std::int64_t(k.z)) <= coordinateLimit / tileSide / 4 + 1;
}
std::vector<Tile> mergeRegion(std::span<Tile const> old, std::span<Tile const> incoming) {
    if (old.size() > maxRegionTiles || incoming.empty() || incoming.size() > maxRegionTiles)
        return {};
    auto region = regionKey(incoming.front().key);
    for (auto tiles : {old, incoming})
        for (auto const &tile : tiles)
            if (!validKey(tile.key) || regionKey(tile.key) != region ||
                !std::all_of(tile.samples.begin(), tile.samples.end(), validSample))
                return {};
    std::vector<Tile> result(old.begin(), old.end());
    for (auto const &tile : incoming) {
        auto found = std::find_if(result.begin(), result.end(), [&](auto const &t) { return t.key == tile.key; });
        if (found == result.end())
            result.push_back(tile);
        else
            for (std::size_t i = 0; i < tile.samples.size(); ++i)
                if (tile.samples[i].known)
                    found->samples[i] = tile.samples[i];
    }
    return result.size() <= maxRegionTiles ? result : std::vector<Tile>{};
}
Bytes encodeRegion(std::string const &world, TileKey region, std::span<Tile const> tiles) {
    if (!validRegionKey(region) || tiles.empty() || tiles.size() > maxRegionTiles)
        return {};
    for (auto const &t : tiles)
        if (regionKey(t.key) != region)
            return {};
    auto payload = encodeTiles(world, tiles);
    if (payload.empty())
        return {};
    Writer w;
    header(w, 0x32525050u, world);
    for (int v : {region.x, region.z, region.dimension, region.caveTop, region.caveBottom})
        w.u(std::uint32_t(v), 4);
    w.u(int(region.layer), 1);
    w.u(payload.size(), 4);
    w.data.insert(w.data.end(), payload.begin(), payload.end());
    w.seal();
    return w.data.size() <= maxRegionFileBytes ? w.data : Bytes{};
}
std::optional<std::vector<Tile>> decodeRegion(std::span<std::uint8_t const> data,
                                              std::string const &world, TileKey region) {
    auto p = reader(data, maxRegionFileBytes);
    if (!validRegionKey(region) || !p || !p->header(0x32525050u, world))
        return {};
    auto &r = *p;
    TileKey stored;
    stored.x = int(std::int32_t(r.u(4)));
    stored.z = int(std::int32_t(r.u(4)));
    stored.dimension = int(std::int32_t(r.u(4)));
    stored.caveTop = int(std::int32_t(r.u(4)));
    stored.caveBottom = int(std::int32_t(r.u(4)));
    stored.layer = Layer(r.u(1));
    auto count = r.u(4);
    if (!r.okay || stored != region || count != r.data.size() - r.at)
        return {};
    auto tiles = decodeTiles(r.data.subspan(r.at), world, maxRegionTiles, true);
    if (!tiles || tiles->empty() || tiles->size() > maxRegionTiles)
        return {};
    for (auto const &t : *tiles)
        if (regionKey(t.key) != region)
            return {};
    return tiles;
}
Bytes encodeSettings(Settings const &s) {
    if (!s.valid())
        return {};
    Writer w;
    w.u(0x31535050u, 4);
    w.u(3, 2);
    for (bool b : {s.enabled, s.circular, s.northLocked, s.radar, s.cave})
        w.u(b, 1);
    for (int n : {s.radius, s.pixelSize, s.columnsPerTick, s.readsPerTick, s.maxTiles, s.caveDepth,
                  s.refreshTicks, s.radarMilliseconds})
        w.u(n, 4);
    w.real(s.zoom);
    w.real(s.scanMilliseconds);
    for (bool b : {s.minimap, s.coordinates, s.biomeText, s.radarNames, s.distanceRadar,
                   s.snapPixels, s.waypointMarkers, s.lastDeath, s.caveShade})
        w.u(b, 1);
    for (bool b : s.radarCategories)
        w.u(b, 1);
    w.u(int(s.anchor), 1);
    w.u(std::uint32_t(s.offsetX), 4);
    w.u(std::uint32_t(s.offsetY), 4);
    for (int key : s.actionKeys)
        w.u(key, 1);
    w.seal();
    return w.data;
}
std::optional<Settings> decodeSettings(std::span<std::uint8_t const> data) {
    auto p = reader(data, 256);
    if (!p || p->u(4) != 0x31535050u)
        return {};
    auto &r = *p;
    auto version = r.u(2);
    if (version != 1 && version != 2 && version != 3)
        return {};
    Settings s;
    bool *flags[] = {&s.enabled, &s.circular, &s.northLocked, &s.radar, &s.cave};
    for (auto f : flags) {
        auto n = r.u(1);
        if (n > 1)
            return {};
        *f = n != 0;
    }
    int *fields[] = {&s.radius,   &s.pixelSize, &s.columnsPerTick, &s.readsPerTick,
                     &s.maxTiles, &s.caveDepth, &s.refreshTicks,   &s.radarMilliseconds};
    for (auto f : fields)
        *f = int(std::int32_t(r.u(4)));
    s.zoom = r.real();
    s.scanMilliseconds = r.real();
    bool const legacy = r.data.size() == 59;
    bool const extended = r.data.size() == 88;
    if ((!legacy && !extended) || (version == 3 && !extended) || (version == 1 && !legacy)) return {};
    if (extended) {
        bool *extra[] = {&s.minimap, &s.coordinates, &s.biomeText, &s.radarNames, &s.distanceRadar,
                         &s.snapPixels, &s.waypointMarkers, &s.lastDeath, &s.caveShade};
        for (auto flag : extra) {
            auto value = r.u(1);
            if (value > 1)
                return {};
            *flag = value != 0;
        }
        for (auto &flag : s.radarCategories) {
            auto value = r.u(1);
            if (value > 1)
                return {};
            flag = value != 0;
        }
        s.anchor = Anchor(r.u(1));
        s.offsetX = int(std::int32_t(r.u(4)));
        s.offsetY = int(std::int32_t(r.u(4)));
        for (auto &key : s.actionKeys)
            key = int(r.u(1));
    }
    if (!r.okay || r.at != r.data.size() || !s.valid())
        return {};
    return s;
}
} // namespace praxis::companion::map
