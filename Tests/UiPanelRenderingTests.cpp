#include "GpuTestContext.hpp"
#include "../BaseballSimulatorVR/Common/UiTextAtlasPipeline.hpp"

#include <d3d12sdklayers.h>
#include <cmath>
#include <fstream>

namespace
{
    namespace Common = BaseballSimulatorVR::Common;
    namespace Ui = Common::UiText;
    using Tests::Check;

    struct Bounds { double left, top, right, bottom; };

    Bounds ProjectCell(const Ui::PanelLayout& panel, const Ui::PanelLocalRect& cell,
                       const Common::ViewProjectionState& view, LONG width, LONG height)
    {
        Bounds bounds{static_cast<double>(width), static_cast<double>(height), 0, 0};
        for (const double x : {cell.left, cell.right})
            for (const double y : {cell.top, cell.bottom})
            {
                Common::ScreenPoint projected;
                Check(Common::TryProjectWorldToScreen(panel.center + panel.rightAxis*x + panel.upAxis*y +
                    panel.forwardAxis*.024, view, width, height, &projected, false), "Project oriented test cell");
                bounds.left = (std::min)(bounds.left, projected.x);
                bounds.top = (std::min)(bounds.top, projected.y);
                bounds.right = (std::max)(bounds.right, projected.x);
                bounds.bottom = (std::max)(bounds.bottom, projected.y);
            }
        return bounds;
    }

    bool Contains(const Bounds& bounds, double x, double y)
    {
        return x >= bounds.left-2 && x <= bounds.right+2 && y >= bounds.top-2 && y <= bounds.bottom+2;
    }

    void SavePreview(const std::vector<Tests::GpuTestContext::Pixel>& pixels, UINT width, UINT height,
                     const wchar_t* filename=L"UiPanelRegression.bmp")
    {
        // An ignored GPU readback, useful for reviewing the real production shader output.
        const std::filesystem::path folder = std::filesystem::current_path()/"x64";
        std::filesystem::create_directories(folder);
        std::ofstream output(folder/filename, std::ios::binary);
        BITMAPFILEHEADER file{};
        file.bfType = 0x4D42;
        file.bfOffBits = sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + width*height*4;
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(width);
        info.biHeight = -static_cast<LONG>(height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        for (const auto& pixel : pixels)
        {
            const std::array<std::uint8_t,4> bgra{pixel[2],pixel[1],pixel[0],255};
            output.write(reinterpret_cast<const char*>(bgra.data()), bgra.size());
        }
        Check(output.good(), "Write GPU panel regression preview");
    }
}

void RunUiPanelRenderingTests()
{
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    Tests::GpuTestContext gpu(1024, 768);
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
    (void)gpu.device.As(&messages);
    Ui::GlyphAtlas atlas;
    Ui::TextLayoutCache layouts;
    Ui::AtlasPipeline pipeline, laggingPipeline;
    Check(pipeline.Initialize(gpu.device.Get(), gpu.ColorFormat, gpu.DepthFormat, 1), "Create production glyph shader");
    Check(laggingPipeline.Initialize(gpu.device.Get(), gpu.ColorFormat, gpu.DepthFormat, 1), "Create second frame glyph shader");
    std::vector<Ui::AtlasVertex> vertices;
    Ui::AtlasDrawContext context{atlas, layouts, vertices};
    Common::ViewProjectionState view;
    view.isValid = true;
    Ui::PanelLayout panel;
    panel.center = {0,0,-1};
    panel.rightAxis = {std::cos(.35),0,std::sin(.35)};
    panel.upAxis = {0,1,0};
    panel.forwardAxis = {-std::sin(.35),0,std::cos(.35)};
    panel.columns = 3;
    panel.rows = 2;
    std::array<std::wstring_view,6> labels{
        L"投球設定", L"球速 155.2", L"ストライク", L"打球統計", L"角度 24.5", L"反発係数"};
    std::array<Ui::PanelLocalRect,6> cells;
    std::array<Bounds,6> bounds;
    for (int i=0; i<6; ++i)
    {
        cells[i] = Ui::BuildPanelCellRect(-.57,.27,.36,.23,.025,3,i);
        const auto& cell = cells[i];
        Check(std::abs(cell.right-cell.left-.36)<1e-10 && std::abs(cell.top-cell.bottom-.23)<1e-10,
              "Cell rectangle preserves width and height (left/top/right/bottom order)");
        Check(std::abs(cell.left-(-.57+(i%3)*.385))<1e-10 &&
              std::abs(cell.top-(.27-(i/3)*.255))<1e-10, "Multirow panel uses the correct cell location");
        bounds[i] = ProjectCell(panel,cell,view,gpu.width,gpu.height);
    }
    const auto buildText = [&]
    {
        vertices.clear();
        atlas.BeginFrame();
        for (std::size_t i=0; i<labels.size(); ++i)
            Ui::AppendProjectedPanelText(panel,cells[i],labels[i],.9,.8,.5,.5,{1,1,1,1},view,
                gpu.width,gpu.height,context);
        Ui::SortAtlasTrianglesBackToFront(vertices);
    };
    buildText();
    Check(vertices.size()<=6*48 && !vertices.empty(), "Text emits at most one quad per visible glyph");
    float minimumW=100, maximumW=0;
    for (const auto& vertex : vertices)
    {
        for (const float value : vertex.position) Check(std::isfinite(value), "Projected glyph position is finite");
        minimumW=(std::min)(minimumW,vertex.position[3]);
        maximumW=(std::max)(maximumW,vertex.position[3]);
    }
    Check(maximumW-minimumW>.1F, "Tilted glyph quads retain perspective depth for UV interpolation");
    const auto draw = [&](Ui::AtlasPipeline& renderer)
    {
        gpu.Begin();
        Check(renderer.UploadAndDraw(gpu.commands.Get(),atlas,vertices,gpu.width,gpu.height), "Draw production atlas geometry");
        return gpu.ReadPixels();
    };
    const auto textPixels = draw(pipeline);
    std::array<std::size_t,6> ink{};
    std::size_t outside=0, antialiased=0;
    for (UINT y=0; y<gpu.height; ++y)
        for (UINT x=0; x<gpu.width; ++x)
        {
            const auto value=textPixels[static_cast<std::size_t>(y)*gpu.width+x][0];
            if (value==0) continue;
            bool inside=false;
            for (std::size_t i=0; i<bounds.size(); ++i)
                if (Contains(bounds[i],x,y)) { ++ink[i]; inside=true; }
            if (!inside) ++outside;
            if (value>8 && value<247) ++antialiased;
        }
    for (const auto count : ink) Check(count>100, "All six tilted cells contain readable rendered glyphs");
    Check(outside==0, "Projected text remains inside its own cell bounds");
    Check(antialiased>100, "GPU glyph sampling preserves grayscale antialiasing");
    Check(draw(laggingPipeline)==textPixels, "Independently in-flight atlas initially renders identically");
    const auto uploadCount=pipeline.UploadCount();
    buildText();
    Check(draw(pipeline)==textPixels && pipeline.UploadCount()==uploadCount,
          "Static text reuses the GPU atlas without a texture upload");

    // One frame adds glyphs and clears the shared dirty range before another
    // independently in-flight frame catches up. Both GPU textures must agree.
    labels[0]=L"山本由伸 新規";
    buildText();
    const auto updatedPixels=draw(pipeline);
    Check(updatedPixels!=textPixels, "A changed label updates the visible GPU output");
    Check(draw(laggingPipeline)==updatedPixels, "Lagging frame uploads new glyphs even after dirty range was committed");
    Check(laggingPipeline.Initialize(gpu.device.Get(),gpu.ColorFormat,gpu.DepthFormat,1),
          "Recreate an atlas pipeline after its submitted frame completes");
    Check(draw(laggingPipeline)==updatedPixels, "Recreated GPU texture uploads all glyphs despite an unchanged CPU generation");

    // Transparent geometry and text are sorted together: a near blue surface
    // must tint the far white glyph instead of being overwritten by a text pass.
    const float blue[]{0,0,1,.5F};
    const Common::ScreenPoint a{0,0,.00001F}, b{1023,0,.00001F}, c{1023,767,.00001F}, d{0,767,.00001F};
    Ui::AppendUntexturedTriangle(vertices,a,b,c,blue,gpu.width,gpu.height);
    Ui::AppendUntexturedTriangle(vertices,a,c,d,blue,gpu.width,gpu.height);
    Ui::SortAtlasTrianglesBackToFront(vertices);
    const auto layeredPixels=draw(pipeline);
    std::size_t tinted=0;
    for (std::size_t i=0; i<updatedPixels.size(); ++i)
        if (updatedPixels[i][0]>250)
        {
            const auto& pixel=layeredPixels[i];
            Check(pixel[0]>=125 && pixel[0]<=130 && pixel[1]==pixel[0] && pixel[2]>=250,
                  "Near transparent surface correctly covers far text");
            ++tinted;
        }
    Check(tinted>100, "Verify transparent layering across visible glyph interiors");

    // Hand-panel preview rendered by the production shader.
    buildText();
    const float background[]{.08F,.22F,.35F,.85F};
    for (const auto& cell : cells)
    {
        Ui::AppendProjectedPanelRect(panel,cell,{background[0],background[1],background[2],background[3]},
            0,view,gpu.width,gpu.height,context);
    }
    Ui::SortAtlasTrianglesBackToFront(vertices);
    const auto previewPixels=draw(pipeline);
    SavePreview(previewPixels,gpu.width,gpu.height);
    for (std::size_t i=0; i<updatedPixels.size(); ++i)
        if (updatedPixels[i][0]>250)
            Check(previewPixels[i][0]>245, "A tilted panel background never covers its own opaque glyph interiors");

    // The always-visible field board uses the same atlas path. A camera angle
    // makes its large background span the depths of multiple individual labels.
    Common::FieldPresentationState field;
    field.statistics.visible=true;
    field.statistics.pitchName=L"山本由伸・フォーシーム";
    field.statistics.pitchSpeedMetersPerSecond=43.1;
    field.statistics.pitchSpinRpm=2450;
    view.position={20,2,0};
    view.orientation={0,std::sin(.09),0};
    view.orientationW=std::cos(.09);
    vertices.clear();
    context.groups.clear();
    atlas.BeginFrame();
    Common::EmitFieldScoreboardGeometry(field.statistics,
        [](const auto&,const auto&,const auto&,const float*) {},
        [&](std::wstring_view label,double left,double top,double maxWidth,double maxHeight,
            const std::array<float,4>& color,double z)
        {
            Ui::AppendProjectedText(label,left,top,maxWidth,maxHeight,.5,0,color,
                [z](double x,double y) { return BaseballSimulatorVR::Physics::Vector3{x,y,z}; },
                view,gpu.width,gpu.height,context);
        });
    Ui::SortAtlasTrianglesBackToFront(vertices);
    const auto boardTextPixels=draw(pipeline);
    vertices.clear();
    Ui::EmitProjectedFieldPresentation(field,view,gpu.width,gpu.height,context,
        [](const auto&,const auto&,const auto&,const float*) {});
    Ui::SortAtlasTrianglesBackToFront(vertices);
    const auto boardPixels=draw(pipeline);
    SavePreview(boardPixels,gpu.width,gpu.height,L"UiBoardRegression.bmp");
    std::size_t boardInk=0;
    for (std::size_t i=0; i<boardPixels.size(); ++i)
        if (boardTextPixels[i][2]>250 && boardTextPixels[i][0]>220)
        {
            Check(boardPixels[i][0]+3>=boardTextPixels[i][0], "Angled statistics board retains its own visible text");
            ++boardInk;
        }
    Check(boardInk>20, "Production field board displays Japanese and numeric text through the GPU atlas");

    // A near plane crossing clips the glyph polygon and its UVs together. It
    // must retain the visible half instead of rejecting the complete letter.
    vertices.clear();
    view=Common::ViewProjectionState{};
    view.isValid=true;
    atlas.BeginFrame();
    const auto glyph=atlas.Get(L'M');
    const auto layout=layouts.Get(L"M",atlas);
    const double middleX=glyph.originX+glyph.width*.5;
    const double middleY=-(glyph.originY+glyph.height*.5);
    Ui::AppendProjectedText(L"M",0,0,layout.width,layout.height,0,0,{1,1,1,1},
        [&](double x,double y)
        {
            return BaseballSimulatorVR::Physics::Vector3{(x-middleX)*.0006,(y-middleY)*.0006,
                -Common::DebugWorldLayout::NearClipMeters-(x-middleX)*.001};
        },view,gpu.width,gpu.height,context);
    Check(!vertices.empty() && vertices.size()%3==0, "A partially near-clipped glyph keeps its visible polygon");
    const float middleU=(glyph.u0+glyph.u1)*.5F;
    for (const auto& vertex : vertices)
    {
        Check(vertex.position[3]>=Common::DebugWorldLayout::NearClipMeters,
              "Clipped glyph positions remain in front of the near plane");
        Check(vertex.uv[0]>=middleU-1e-5F && vertex.uv[0]<=glyph.u1+1e-5F &&
              vertex.uv[1]>=glyph.v0-1e-5F && vertex.uv[1]<=glyph.v1+1e-5F,
              "Near clipping interpolates UVs without stretching the hidden half");
    }
    const auto clippedPixels=draw(pipeline);
    std::size_t clippedInk=0;
    for (const auto& pixel : clippedPixels) if (pixel[0]>8) ++clippedInk;
    Check(clippedInk>100, "The surviving near-clipped glyph is visible in actual GPU pixels");
    vertices.clear();
    Ui::AppendProjectedText(L"M",0,0,layout.width,layout.height,0,0,{1,1,1,1},
        [](double x,double y) { return BaseballSimulatorVR::Physics::Vector3{x*.0006,y*.0006,-.01}; },
        view,gpu.width,gpu.height,context);
    Check(vertices.empty(), "A glyph entirely behind the near plane emits no triangles");
    if (messages)
        for (UINT64 i=0; i<messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i)
        {
            SIZE_T size=0;
            Check(SUCCEEDED(messages->GetMessage(i,nullptr,&size)), "Measure D3D12 validation message");
            std::vector<std::byte> storage(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(SUCCEEDED(messages->GetMessage(i,message,&size)), "Read D3D12 validation message");
            Check(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,
                  message->pDescription);
        }
}
