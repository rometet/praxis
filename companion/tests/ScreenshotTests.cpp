#include "core/ScreenshotPolicy.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion::screenshot;
int main() {
    unsigned checks=0;auto require=[&](bool value,char const* label){++checks;if(!value)throw std::runtime_error(label);};
    try {
        // Keybind gate: F1 menu and F3 debug stay reserved, 0 and junk rejected.
        require(!validScreenshotKey(0),"zero key rejected");
        require(!validScreenshotKey(menuKeyVk),"F1 menu key reserved");
        require(!validScreenshotKey(debugKeyVk),"F3 debug key reserved");
        require(validScreenshotKey(0x2C),"PrintScreen allowed");
        require(validScreenshotKey(0x74),"F5 allowed");
        require(validScreenshotKey(0x7B),"F12 allowed");
        require(!validScreenshotKey(0xFFFFFFFF),"all-ones rejected");
        // Frame validation.
        require(!validScreenshotFrame(0,10),"zero width rejected");
        require(!validScreenshotFrame(10,0),"zero height rejected");
        require(validScreenshotFrame(1920,1080),"full hd ok");
        require(!validScreenshotFrame(16384,16385),"over max pixels rejected");
        require(validScreenshotFrame(16384,16384),"max pixels boundary ok");
        // Stride and DIB size arithmetic.
        require(screenshotRowStride(4)==16,"32bpp stride");
        require(screenshotDibSize(4,3)==40+16*3,"dib size header+pixels");
        require(screenshotDibSize(1920,1080)==40+1920ull*4*1080,"full hd dib size");
        // Packer: bad input clears output and returns false.
        ScreenshotFrame bad; bad.width=2; bad.height=2; bad.bgra={0,0,0,0,0,0,0,0,0,0,0,0}; // missing a row
        std::vector<std::uint8_t> dib{1,2,3};
        require(!packClipboardDib(bad,dib),"truncated frame rejected");
        require(dib.empty(),"failed pack clears output");
        ScreenshotFrame empty; empty.width=0; empty.height=5;
        require(!packClipboardDib(empty,dib),"empty frame rejected");
        // Packer: 2x2 known pattern, verify BGRA pass-through and bottom-up flip.
        ScreenshotFrame frame; frame.width=2; frame.height=2;
        // top row:    (0,0)=10,20,30,40  (1,0)=50,60,70,80
        // bottom row: (0,1)=90,100,110,120 (1,1)=130,140,150,160
        frame.bgra={10,20,30,40, 50,60,70,80, 90,100,110,120, 130,140,150,160};
        require(packClipboardDib(frame,dib),"valid frame packs");
        require(dib.size()==40+2*2*4,"packed payload size");
        auto u32=[&](std::size_t at){return static_cast<std::uint32_t>(dib[at])|(static_cast<std::uint32_t>(dib[at+1])<<8)|(static_cast<std::uint32_t>(dib[at+2])<<16)|(static_cast<std::uint32_t>(dib[at+3])<<24);};
        auto u16=[&](std::size_t at){return static_cast<std::uint16_t>(dib[at])|(static_cast<std::uint16_t>(dib[at+1])<<8);};
        require(u32(0)==40,"biSize 40");
        require(u32(4)==2,"biWidth 2");
        require(u32(8)==2,"biHeight positive (bottom-up)");
        require(u16(12)==1,"biPlanes 1");
        require(u16(14)==32,"biBitCount 32");
        require(u32(16)==0,"BI_RGB compression 0");
        require(u32(20)==2*2*4,"biSizeImage w*h*4");
        require(u32(32)==0,"biClrUsed 0");
        // Bottom-up: first pixel row of the DIB is the LAST source row.
        require(dib[40+0]==90&&dib[40+1]==100&&dib[40+2]==110&&dib[40+3]==120,"row flip first stored row");
        require(dib[40+4]==130&&dib[40+5]==140&&dib[40+6]==150,"second bottom pixel");
        require(dib[40+8]==10&&dib[40+9]==20&&dib[40+10]==30,"row flip last stored row");
        require(dib[40+12]==50&&dib[40+13]==60&&dib[40+14]==70,"final bottom pixel");
        // 1-row frame: flip is identity, no out-of-bounds.
        ScreenshotFrame single; single.width=1; single.height=1; single.bgra={9,8,7,6};
        require(packClipboardDib(single,dib),"1x1 packs");
        require(dib.size()==44&&dib[40]==9&&dib[41]==8&&dib[42]==7&&dib[43]==6,"1x1 pixel intact");
        // Width 3 (stride 12) still exact, no stride padding surprises at 32bpp.
        ScreenshotFrame three; three.width=3; three.height=1;
        three.bgra={1,2,3,4,5,6,7,8,9,10,11,12};
        require(packClipboardDib(three,dib)&&dib.size()==40+12,"3x1 packs");
    } catch (std::exception const& e) {
        std::cerr<<"FAIL: "<<e.what()<<"\n";
        return 1;
    }
    std::cout<<"ScreenshotPolicy: "<<checks<<" checks PASS\n";
    return 0;
}
