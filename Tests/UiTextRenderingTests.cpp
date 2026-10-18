#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/UiTextRasterization.hpp"
#include "../BaseballSimulatorVR/Common/TransparentGeometry.hpp"

#include <array>
#include <cmath>

void RunUiTextRenderingTests()
{
    using namespace BaseballSimulatorVR::Common;
    const std::array<std::uint8_t,12> source{0,1,128,255,0,1,128,255,0,1,64,255};
    const auto runs=UiText::BuildCoverageRuns(source,4,3);
    std::array<std::uint8_t,12> restored{};
    bool merged=false;
    for(const auto& run:runs) {
        merged|=run.height>1;
        for(int y=run.y;y<run.y+run.height;++y) for(int x=run.x;x<run.x+run.width;++x)
            restored[static_cast<std::size_t>(y)*4+x]=run.coverage;
    }
    Tests::Check(restored==source,"Coverage runs preserve every grayscale value, including faint edges");
    Tests::Check(merged,"Equal coverage spans merge vertically to limit text geometry");
    Tests::Check(std::abs(UiText::CoverageAlpha(128,.5F)-(128.0F/255*.5F))<1e-6F,
        "Text alpha combines exact coverage with panel text opacity");
    Tests::Check(UiText::CoverageAlpha(1)>0 && UiText::CoverageAlpha(255)==1,
        "Coverage retains edge antialiasing and fully opaque interiors");
    Tests::Check(UiText::SelectRasterPixelHeight(18)==24 && UiText::SelectRasterPixelHeight(70)==96 &&
        UiText::SelectRasterPixelHeight(1000)==128,"Projected text density selects bounded high-resolution font buckets");

    UiText::GlyphCache cache{4};
    (void)cache.Get(L'A',24);(void)cache.Get(L'B',24);(void)cache.Get(L'C',24);(void)cache.Get(L'D',24);
    const auto generated=cache.RasterizationCount();
    (void)cache.Get(L'A',24);
    Tests::Check(cache.RasterizationCount()==generated,"Frequently reused glyphs do not rasterize again");
    (void)cache.Get(L'E',24);
    Tests::Check(cache.Size()==cache.Capacity() && cache.Contains(L'A',24) && !cache.Contains(L'B',24),
        "Glyph LRU has a strict memory entry bound and evicts the least recently used character");
    for(int i=0;i<1000;++i) (void)cache.Get(static_cast<std::uint32_t>(L'0'+i%10),i%2?24:48);
    Tests::Check(cache.Size()==4,"Changing numeric labels and font density cannot grow the glyph cache");

    UiText::GlyphCache textCache{256};
    const auto japanese=UiText::Rasterize(L"球速 ±12.3° ↑↓→ 着地済み",32,textCache);
    const auto high=UiText::Rasterize(L"球速 ±12.3° ↑↓→ 着地済み",96,textCache);
    Tests::Check(japanese.width>0 && japanese.height>0 && japanese.runs.size()>100,
        "Japanese, degree, plus-minus and arrow glyphs produce a complete text raster");
    Tests::Check(high.height>japanese.height*2 && high.width>japanese.width*2,
        "High-density panel text uses a higher-resolution font instead of stretching a fixed bitmap");
    bool partial=false,opaque=false;
    for(const auto& run:japanese.runs){partial|=run.coverage>0 && run.coverage<255;opaque|=run.coverage==255;}
    Tests::Check(partial && opaque,"Native Japanese text preserves both grayscale edges and solid strokes");
    const auto small=UiText::RasterizeToFit(L"投球設定 / Fastball",150,20,16,textCache);
    const auto large=UiText::RasterizeToFit(L"投球設定 / Fastball",900,100,16,textCache);
    Tests::Check(large.height>small.height,"Font detail follows the projected label size");
    const auto multiline=UiText::Rasterize(L"球種\r\n速球\t144",32,textCache);
    Tests::Check(multiline.height==2*japanese.height && multiline.width>0,
        "Profile names with CRLF and tabs have explicit multiline layout");
    const auto decoded=UiText::DecodeUtf16(L"\xD83D\xDE00");
    Tests::Check(decoded.size()==1 && decoded[0]==0x1F600,
        "A supplementary character is decoded as one scalar rather than two broken glyphs");
    const auto emoji=UiText::Rasterize(L"速球 \xD83D\xDE00",32,textCache);
    Tests::Check(emoji.width>0 && !emoji.runs.empty(),"UTF-16 surrogate-pair profile names render without throwing");
    const auto repaired=UiText::DecodeUtf16(L"\xD800\x0001");
    Tests::Check(repaired.size()==2 && repaired[0]==0xFFFD && repaired[1]==0xFFFD,
        "Isolated surrogates and non-layout control characters have an explicit replacement character");
    std::size_t translucentTriangles=0;
    UiText::EmitTextMesh(japanese,0,0,.01,{1,1,1,.8F},
        [](double x,double y){return BaseballSimulatorVR::Physics::Vector3{x,y,-1};},
        [&](const auto&,const auto&,const auto&,const float* color){
            Tests::Check(color[3]>0 && color[3]<=.8F,"Mesh alpha retains the requested text opacity");
            if(color[3]<.79F) ++translucentTriangles;
        });
    Tests::Check(translucentTriangles>0,"Text edges enter the alpha blending render path");

    struct Vertex final {float position[3];int id;};
    std::vector<Vertex> vertices{{{0,0,.2F},1},{{0,0,.2F},1},{{0,0,.2F},1},
        {{0,0,.8F},2},{{0,0,.8F},2},{{0,0,.8F},2},
        {{0,0,.8F},3},{{0,0,.8F},3},{{0,0,.8F},3}};
    SortTransparentTrianglesBackToFront(vertices);
    Tests::Check(vertices[0].id==2 && vertices[3].id==3 && vertices[6].id==1,
        "Transparent text and panels blend back-to-front with stable coplanar ordering");
}
