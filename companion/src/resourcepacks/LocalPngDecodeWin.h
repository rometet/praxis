#pragma once
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
namespace praxis::companion::packs {
struct LocalPngDecodedRGBA {
    std::vector<std::uint8_t> pixels;
    unsigned width{},height{};
    bool valid() const noexcept {
        return width&&height&&width<=2048&&height<=2048
            &&pixels.size()==std::size_t(width)*height*4;
    }
};
// CPU-only. Windows Imaging Component decodes a local PNG held in memory;
// no Minecraft ResourcePackManager, texture atlas or GPU state is changed.
inline LocalPngDecodedRGBA decodeLocalPngWic(std::string const& png) noexcept {
    LocalPngDecodedRGBA out{};
    if(png.size()<33||png.size()>8*1024*1024)return out;
    auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool uninit=SUCCEEDED(init);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return out;
    struct Cleanup{bool active;~Cleanup(){if(active)CoUninitialize();}} cleanup{uninit};
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf()))))return {};
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,png.size());
    if(!memory)return {};
    auto* bytes=GlobalLock(memory);
    if(!bytes){GlobalFree(memory);return {};}
    std::memcpy(bytes,png.data(),png.size());
    GlobalUnlock(memory);
    Microsoft::WRL::ComPtr<IStream> stream;
    auto result=CreateStreamOnHGlobal(memory,TRUE,stream.GetAddressOf());
    if(FAILED(result)){GlobalFree(memory);return {};}
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,
            WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf())))return {};
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if(FAILED(decoder->GetFrame(0,frame.GetAddressOf())))return {};
    UINT width{},height{};
    if(FAILED(frame->GetSize(&width,&height))||width==0||height==0||width>2048||height>2048)return {};
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if(FAILED(factory->CreateFormatConverter(converter.GetAddressOf())))return {};
    if(FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
    out.width=width;out.height=height;
    out.pixels.resize(std::size_t(width)*height*4);
    UINT stride=width*4;
    if(FAILED(converter->CopyPixels(nullptr,stride,UINT(out.pixels.size()),out.pixels.data())))
        return {};
    return out.valid()?std::move(out):LocalPngDecodedRGBA{};
}
} // namespace praxis::companion::packs
