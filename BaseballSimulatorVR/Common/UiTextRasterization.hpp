#pragma once

#include "../Framework.hpp"
#include "../Physics/Vector3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <list>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace BaseballSimulatorVR::Common::UiText
{
    struct CoverageRun final
    {
        int x{}, y{}, width{}, height{1};
        std::uint8_t coverage{};
    };

    [[nodiscard]] inline float CoverageAlpha(const std::uint8_t coverage, const float opacity=1.0F) noexcept
    {
        return static_cast<float>(coverage) * (1.0F/255.0F) * std::clamp(opacity,0.0F,1.0F);
    }

    // Coalesce equal-coverage spans, including identical spans on adjacent rows.
    // Every nonzero sample is retained, including very faint edge coverage.
    [[nodiscard]] inline std::vector<CoverageRun> BuildCoverageRuns(
        const std::span<const std::uint8_t> coverage,const int width,const int height)
    {
        if (width<0 || height<0 || coverage.size()!=static_cast<std::size_t>(width)*static_cast<std::size_t>(height))
            throw std::invalid_argument("Text coverage dimensions must match the bitmap");
        std::vector<CoverageRun> runs;
        std::map<std::tuple<int,int,std::uint8_t>,std::size_t> previousRow;
        for (int y=0;y<height;++y) {
            std::map<std::tuple<int,int,std::uint8_t>,std::size_t> currentRow;
            for (int x=0;x<width;) {
                const auto value=coverage[static_cast<std::size_t>(y)*width+x];
                if (value==0) { ++x; continue; }
                const int start=x++;
                while (x<width && coverage[static_cast<std::size_t>(y)*width+x]==value) ++x;
                const auto key=std::tuple{start,x-start,value};
                const auto previous=previousRow.find(key);
                std::size_t index;
                if (previous!=previousRow.end()) {
                    index=previous->second;
                    ++runs[index].height;
                } else {
                    index=runs.size();
                    runs.push_back({start,y,x-start,1,value});
                }
                currentRow.emplace(key,index);
            }
            previousRow=std::move(currentRow);
        }
        return runs;
    }

    [[nodiscard]] inline int SelectRasterPixelHeight(const double projectedTextHeightPixels) noexcept
    {
        // Rasterize close to the final display density. Buckets avoid cache
        // churn from subpixel head movement while preserving detailed glyphs.
        constexpr std::array<int,7> sizes{16,24,32,48,64,96,128};
        const double requested=std::isfinite(projectedTextHeightPixels)
            ? std::clamp(projectedTextHeightPixels,16.0,128.0) : 16.0;
        for (const int size : sizes) if (size>=requested) return size;
        return sizes.back();
    }

    struct Glyph final
    {
        int advance{}, lineHeight{};
        std::vector<CoverageRun> runs{};
    };

    class GlyphCache final
    {
    public:
        static constexpr std::size_t DefaultCapacity=1024;
        explicit GlyphCache(const std::size_t capacity=DefaultCapacity) : m_capacity(capacity)
        {
            if (capacity==0) throw std::invalid_argument("Glyph cache capacity must be positive");
            m_dc=CreateCompatibleDC(nullptr);
            if (!m_dc) throw std::runtime_error("CreateCompatibleDC failed for UI text");
        }
        ~GlyphCache()
        {
            for (const auto& [height,font] : m_fonts) { (void)height; DeleteObject(font); }
            if (m_dc) DeleteDC(m_dc);
        }
        GlyphCache(const GlyphCache&)=delete;
        GlyphCache& operator=(const GlyphCache&)=delete;
        [[nodiscard]] std::size_t Size() const noexcept { return m_entries.size(); }
        [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] std::size_t RasterizationCount() const noexcept { return m_rasterizationCount; }
        [[nodiscard]] bool Contains(const std::uint32_t character,const int requestedHeight) const
        {
            return m_lookup.contains(Key(character,SelectRasterPixelHeight(requestedHeight)));
        }
        [[nodiscard]] const Glyph& Get(const std::uint32_t character,const int requestedHeight)
        {
            const int height=SelectRasterPixelHeight(requestedHeight);
            const auto key=Key(character,height);
            if (const auto found=m_lookup.find(key);found!=m_lookup.end()) {
                m_entries.splice(m_entries.begin(),m_entries,found->second);
                return found->second->second;
            }
            Glyph glyph=Rasterize(character,height);
            if (m_entries.size()==m_capacity) {
                m_lookup.erase(m_entries.back().first);
                m_entries.pop_back();
            }
            m_entries.emplace_front(key,std::move(glyph));
            m_lookup.emplace(key,m_entries.begin());
            return m_entries.front().second;
        }
    private:
        using KeyType=std::uint64_t;
        using Entry=std::pair<KeyType,Glyph>;
        [[nodiscard]] static KeyType Key(const std::uint32_t character,const int height) noexcept
        {
            return (static_cast<KeyType>(height)<<32U)|static_cast<std::uint32_t>(character);
        }
        [[nodiscard]] Glyph Rasterize(const std::uint32_t character,const int height)
        {
            auto font=m_fonts.find(height);
            if (font==m_fonts.end()) {
                HFONT created=CreateFontW(-height,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                    DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
                    DEFAULT_PITCH|FF_DONTCARE,L"Yu Gothic UI");
                if (!created) throw std::runtime_error("CreateFontW failed for UI text");
                font=m_fonts.emplace(height,created).first;
            }
            const HGDIOBJ oldFont=SelectObject(m_dc,font->second);
            if (!oldFont || oldFont==HGDI_ERROR) throw std::runtime_error("SelectObject failed for UI font");
            struct RestoreFont final { HDC dc; HGDIOBJ font; ~RestoreFont() { SelectObject(dc,font); } } restore{m_dc,oldFont};
            TEXTMETRICW textMetrics{};
            if (!GetTextMetricsW(m_dc,&textMetrics)) throw std::runtime_error("GetTextMetricsW failed for UI font");
            if (character>0xFFFFU) {
                // GDI's outline API addresses BMP characters. Let TextOutW
                // shape a complete UTF-16 pair for supplementary characters.
                const std::uint32_t value=character-0x10000U;
                const wchar_t pair[]{static_cast<wchar_t>(0xD800U+(value>>10U)),static_cast<wchar_t>(0xDC00U+(value&0x3FFU))};
                SIZE extent{};
                if(!GetTextExtentPoint32W(m_dc,pair,2,&extent)) throw std::runtime_error("Measure UTF-16 UI character failed");
                const int bitmapWidth=(std::max)(1,static_cast<int>(extent.cx)+4),bitmapHeight=textMetrics.tmHeight+4;
                BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth=bitmapWidth;info.bmiHeader.biHeight=-bitmapHeight;
                info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                void* bits{};
                const HBITMAP bitmap=CreateDIBSection(m_dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
                if(!bitmap || !bits) throw std::runtime_error("Create UTF-16 UI bitmap failed");
                const HGDIOBJ oldBitmap=SelectObject(m_dc,bitmap);
                struct RestoreBitmap final { HDC dc;HGDIOBJ previous;HBITMAP bitmap;~RestoreBitmap(){SelectObject(dc,previous);DeleteObject(bitmap);} } restoreBitmap{m_dc,oldBitmap,bitmap};
                auto* pixels=static_cast<std::uint32_t*>(bits);
                std::fill_n(pixels,static_cast<std::size_t>(bitmapWidth)*bitmapHeight,0U);
                SetBkMode(m_dc,TRANSPARENT);SetTextColor(m_dc,RGB(255,255,255));
                if(!TextOutW(m_dc,2,2,pair,2)) throw std::runtime_error("Render UTF-16 UI character failed");
                GdiFlush();
                std::vector<std::uint8_t> coverage(static_cast<std::size_t>(bitmapWidth)*bitmapHeight);
                for(std::size_t i=0;i<coverage.size();++i) {
                    const auto pixel=pixels[i];
                    coverage[i]=static_cast<std::uint8_t>(((pixel&255U)+2*((pixel>>8U)&255U)+((pixel>>16U)&255U))/4U);
                }
                Glyph glyph{};glyph.advance=extent.cx;glyph.lineHeight=textMetrics.tmHeight;
                glyph.runs=BuildCoverageRuns(coverage,bitmapWidth,bitmapHeight);
                for(auto& run:glyph.runs){run.x-=2;run.y-=2;}
                ++m_rasterizationCount;
                return glyph;
            }
            MAT2 transform{};transform.eM11.value=1;transform.eM22.value=1;
            GLYPHMETRICS metrics{};
            const DWORD bytes=GetGlyphOutlineW(m_dc,character,GGO_GRAY8_BITMAP,&metrics,0,nullptr,&transform);
            if (bytes==GDI_ERROR) throw std::runtime_error("GetGlyphOutlineW failed for UI character");
            Glyph glyph{};glyph.advance=metrics.gmCellIncX;glyph.lineHeight=textMetrics.tmHeight;
            if (bytes>0) {
                std::vector<std::uint8_t> bitmap(bytes);
                if (GetGlyphOutlineW(m_dc,character,GGO_GRAY8_BITMAP,&metrics,bytes,bitmap.data(),&transform)==GDI_ERROR)
                    throw std::runtime_error("GetGlyphOutlineW bitmap read failed");
                const auto width=static_cast<int>(metrics.gmBlackBoxX), bitmapHeight=static_cast<int>(metrics.gmBlackBoxY);
                const DWORD stride=(metrics.gmBlackBoxX+3U)&~3U;
                std::vector<std::uint8_t> coverage(static_cast<std::size_t>(width)*bitmapHeight);
                for (int y=0;y<bitmapHeight;++y) for (int x=0;x<width;++x)
                    coverage[static_cast<std::size_t>(y)*width+x]=static_cast<std::uint8_t>(
                        (static_cast<unsigned int>(bitmap[static_cast<std::size_t>(y)*stride+x])*255U+32U)/64U);
                glyph.runs=BuildCoverageRuns(coverage,width,bitmapHeight);
                for (auto& run : glyph.runs) { run.x+=metrics.gmptGlyphOrigin.x;run.y+=textMetrics.tmAscent-metrics.gmptGlyphOrigin.y; }
            }
            ++m_rasterizationCount;
            return glyph;
        }
        HDC m_dc{};
        std::size_t m_capacity,m_rasterizationCount{};
        std::map<int,HFONT> m_fonts{};
        std::list<Entry> m_entries{};
        std::unordered_map<KeyType,std::list<Entry>::iterator> m_lookup{};
    };

    struct RasterizedText final
    {
        int width{},height{};
        std::vector<CoverageRun> runs{};
    };

    [[nodiscard]] inline GlyphCache& SharedGlyphCache()
    {
        static GlyphCache cache;
        return cache;
    }

    [[nodiscard]] inline std::vector<std::uint32_t> DecodeUtf16(const std::wstring_view text)
    {
        std::vector<std::uint32_t> scalars;
        for(std::size_t i=0;i<text.size();++i) {
            std::uint32_t value=static_cast<std::uint16_t>(text[i]);
            if(value>=0xD800U && value<=0xDBFFU) {
                if(i+1<text.size()) {
                    const std::uint32_t low=static_cast<std::uint16_t>(text[i+1]);
                    if(low>=0xDC00U && low<=0xDFFFU) {value=0x10000U+((value-0xD800U)<<10U)+(low-0xDC00U);++i;}
                    else value=0xFFFDU;
                } else value=0xFFFDU;
            } else if(value>=0xDC00U && value<=0xDFFFU) value=0xFFFDU;
            if(value=='\r') {
                if(i+1<text.size() && text[i+1]=='\n') ++i;
                value='\n';
            }
            if((value<32U && value!='\n' && value!='\t') || value==127U) value=0xFFFDU;
            scalars.push_back(value);
        }
        return scalars;
    }

    [[nodiscard]] inline RasterizedText Layout(const std::span<const std::uint32_t> text,
        const int height,const bool includeRuns,GlyphCache& cache)
    {
        RasterizedText result{};
        if(text.empty()) return result;
        const auto& space=cache.Get(' ',height);
        const int lineHeight=space.lineHeight,tabWidth=(std::max)(1,space.advance*4);
        int x=0,y=0;
        for(const auto character:text) {
            if(character=='\n') {result.width=(std::max)(result.width,x);x=0;y+=lineHeight;continue;}
            if(character=='\t') {x=(x/tabWidth+1)*tabWidth;continue;}
            const auto& glyph=cache.Get(character,height);
            if(includeRuns) for(auto run:glyph.runs){run.x+=x;run.y+=y;result.runs.push_back(run);}
            x+=glyph.advance;
        }
        result.width=(std::max)(result.width,x);
        result.height=y+lineHeight;
        return result;
    }

    [[nodiscard]] inline RasterizedText Rasterize(const std::wstring_view text,const int height,GlyphCache& cache=SharedGlyphCache())
    {
        return Layout(DecodeUtf16(text),height,true,cache);
    }

    [[nodiscard]] inline RasterizedText RasterizeToFit(const std::wstring_view text,
        const double projectedWidthPixels,const double projectedHeightPixels,
        const int minimumPixelHeight=16,GlyphCache& cache=SharedGlyphCache())
    {
        const auto scalars=DecodeUtf16(text);
        const auto extent=Layout(scalars,32,false,cache);
        if (extent.width==0 || extent.height==0) return {};
        const double fittedHeight=(std::min)(projectedHeightPixels,
            projectedWidthPixels*static_cast<double>(extent.height)/extent.width);
        const int density=SelectRasterPixelHeight((std::max)(static_cast<double>(minimumPixelHeight),fittedHeight*32.0/extent.height));
        return Layout(scalars,density,true,cache);
    }

    // Uses the same coverage layout for panel cells, free text rectangles and
    // the stadium board. The callback maps local coordinates into world space.
    template <typename TWorldPoint,typename TTriangle>
    void EmitTextMesh(const RasterizedText& text,const double left,const double top,const double pixelScale,
        const std::array<float,4>& color,TWorldPoint&& worldPoint,TTriangle&& triangle)
    {
        for (const auto& run : text.runs) {
            const double x=left+run.x*pixelScale,y=top-run.y*pixelScale;
            const auto a=worldPoint(x,y),b=worldPoint(x+run.width*pixelScale,y);
            const auto c=worldPoint(x+run.width*pixelScale,y-run.height*pixelScale),d=worldPoint(x,y-run.height*pixelScale);
            const std::array<float,4> shaded{color[0],color[1],color[2],CoverageAlpha(run.coverage,color[3])};
            triangle(a,b,c,shaded.data());triangle(a,c,d,shaded.data());
        }
    }
}
