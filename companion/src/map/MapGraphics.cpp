#include "map/MapGraphics.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <mutex>
namespace praxis::companion::map {
namespace {
using Microsoft::WRL::ComPtr;
std::mutex graphicsMutex;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11SamplerState> pointSampler;
struct Texture {std::shared_ptr<TerrainImage const> image;ComPtr<ID3D11ShaderResourceView> view;};
std::array<Texture,2> textures;
std::array<ComPtr<ID3D11ShaderResourceView>,8> frameViews;
std::size_t frameViewCount{};
Session submittedSession;
std::chrono::steady_clock::time_point firstSubmitted{};
void pointSampling(ImDrawList const*,ImDrawCmd const*) {
    // The host's frame lock covers callback execution and graphics teardown.
    if(context&&pointSampler){auto* sampler=pointSampler.Get();context->PSSetSamplers(0,1,&sampler);}
}
}
void clearMapGraphics() noexcept {
    std::lock_guard lock(graphicsMutex);textures={};frameViews={};frameViewCount=0;pointSampler.Reset();context.Reset();device.Reset();
    submittedSession={};firstSubmitted={};
}
std::optional<std::chrono::steady_clock::time_point> firstMapSubmission(Session const& session) noexcept {
    std::lock_guard lock(graphicsMutex);
    if(submittedSession!=session||firstSubmitted==std::chrono::steady_clock::time_point{})return {};
    return firstSubmitted;
}
void setMapGraphics(ID3D11Device* d,ID3D11DeviceContext* c) noexcept {
    std::lock_guard lock(graphicsMutex);
    frameViews={};frameViewCount=0;
    if(device.Get()==d&&context.Get()==c)return;
    textures={};pointSampler.Reset();context.Reset();device.Reset();
    if(!d||!c)return;
    ComPtr<ID3D11Device> owner;c->GetDevice(&owner);
    if(owner.Get()!=d)return;
    device=d;context=c;
    D3D11_SAMPLER_DESC desc{};desc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxLOD=D3D11_FLOAT32_MAX;desc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
    if(FAILED(device->CreateSamplerState(&desc,&pointSampler))){context.Reset();device.Reset();}
}
bool drawTerrainTexture(ImDrawList& draw,Snapshot const& s,Canvas const& c) {
    std::lock_guard lock(graphicsMutex);
    if(!device||!context||!pointSampler)return false;
    int chosen=-1;double best=1e30;
    double radius=c.halfSize/c.transform.pixelsPerBlock;
    for(int i=0;i<2;++i)if((c.viewMask==3||(c.viewMask&(1u<<i)))&&s.terrainImages[i]) {
        auto const& image=s.terrainImages[i];
        auto dx=image->x-c.transform.worldCenter.x,dz=image->z-c.transform.worldCenter.z;
        double score=std::hypot(dx,dz)+std::abs(image->radius-radius);
        if(score<best){best=score;chosen=i;}
    }
    if(chosen<0)return false;
    auto const& image=s.terrainImages[chosen];auto& texture=textures[chosen];
    if(image->side<1||image->side>513||image->rgba.size()!=std::size_t(image->side)*image->side)return false;
    if(texture.image!=image) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=unsigned(image->side);desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{image->rgba.data(),unsigned(image->side*4),0};
        ComPtr<ID3D11Texture2D> created;ComPtr<ID3D11ShaderResourceView> view;
        if(FAILED(device->CreateTexture2D(&desc,&data,&created))||FAILED(device->CreateShaderResourceView(created.Get(),nullptr,&view)))return false;
        texture={image,std::move(view)};
    }
    // clip the actual image footprint against the circular canvas. This keeps
    // world texture origin and markers aligned and never clamps/stretch edges.
    double loX=image->x-image->radius-.5,loZ=image->z-image->radius-.5;
    double hiX=loX+image->side,hiZ=loZ+image->side;
    std::array<Point,40> a{},b{};
    a[0]=c.transform.project({loX,loZ});a[1]=c.transform.project({hiX,loZ});
    a[2]=c.transform.project({hiX,hiZ});a[3]=c.transform.project({loX,hiZ});int count=4;
    if(c.circular)for(int edge=0;edge<32&&count;++edge) {
        double angle=(2*edge+1)*3.14159265358979323846/32;
        Point n{std::cos(angle),std::sin(angle)};double limit=c.halfSize*std::cos(3.14159265358979323846/32);
        auto side=[&](Point p){return n.x*(p.x-c.transform.screenCenter.x)+n.z*(p.z-c.transform.screenCenter.z)-limit;};
        int out=0;
        for(int i=0;i<count;++i){auto p=a[i],q=a[(i+1)%count];auto dp=side(p),dq=side(q);bool ip=dp<=0,iq=dq<=0;
            if(ip)b[out++]=p;
            if(ip!=iq){double t=dp/(dp-dq);b[out++]={p.x+t*(q.x-p.x),p.z+t*(q.z-p.z)};}}
        count=out;a=b;
    }
    if(count<3)return true;
    if(frameViewCount>=frameViews.size())return false;
    if(image->known&&s.session.valid()&&submittedSession!=s.session) {
        submittedSession=s.session;firstSubmitted=std::chrono::steady_clock::now();
    }
    // UI and HUD may load different immutable snapshots in one host frame.
    // Keep every submitted SRV alive until the host admits the next frame.
    frameViews[frameViewCount++]=texture.view;
    auto uv=[&](Point p){auto w=c.transform.unproject(p);return ImVec2{float((w.x-loX)/image->side),float((w.z-loZ)/image->side)};};
    draw.AddCallback(pointSampling,nullptr);
    draw.PushTextureID(reinterpret_cast<ImTextureID>(texture.view.Get()));
    draw.PrimReserve((count-2)*3,count);auto base=draw._VtxCurrentIdx;
    for(int i=0;i<count;++i)draw.PrimWriteVtx({float(a[i].x),float(a[i].z)},uv(a[i]),0xffffffffu);
    for(int i=1;i<count-1;++i){draw.PrimWriteIdx(ImDrawIdx(base));draw.PrimWriteIdx(ImDrawIdx(base+i));draw.PrimWriteIdx(ImDrawIdx(base+i+1));}
    draw.PopTextureID();
    draw.AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    return true;
}
}
