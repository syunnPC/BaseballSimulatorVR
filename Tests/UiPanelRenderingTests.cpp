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
    field.statistics.zoneCall=Common::PitchZoneCall::Strike;
    field.statistics.hasPitchBreak=true;
    field.statistics.pitchHorizontalBreakMeters=-.18;
    field.statistics.pitchVerticalBreakMeters=-.90;
    field.statistics.pitchTrace={{0,1.9,0},{-.01,1.85,4},{-.04,1.7,8},{-.09,1.4,12},{-.18,1.0,18.44}};
    field.statistics.hasBattedMetrics=true;
    field.statistics.battedLaunchSpeedMetersPerSecond=45;
    field.statistics.battedLaunchAngleDegrees=28;
    field.statistics.battedDistanceMeters=95;
    field.statistics.battedHasGroundContact=true;
    field.statistics.battedMaxHeightMeters=24;
    field.statistics.hasBatContactPosition=true;
    field.statistics.batContactFraction=.82;
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

    view.position={0,12,-60};
    view.orientation={};
    view.orientationW=1;
    vertices.clear();
    context.groups.clear();
    atlas.BeginFrame();
    Ui::EmitProjectedFieldPresentation(field,view,gpu.width,gpu.height,context,
        [](const auto&,const auto&,const auto&,const float*) {});
    Ui::SortAtlasTrianglesBackToFront(vertices);
    const auto detailPixels=draw(pipeline);
    SavePreview(detailPixels,gpu.width,gpu.height,L"UiBoardDetailRegression.bmp");
    std::size_t greenInk=0, endpointInk=0;
    for (const auto& pixel : detailPixels)
    {
        if (pixel[1]>210 && pixel[0]<90 && pixel[2]<130) ++greenInk;
        if (pixel[0]>240 && pixel[1]>150 && pixel[1]<195 && pixel[2]<85) ++endpointInk;
    }
    Check(greenInk>20 && endpointInk>=8,
        "The production GPU scoreboard preserves the green sweet spot, contact marker and pitch endpoint marker");

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

    {
        namespace Physics = BaseballSimulatorVR::Physics;
        view = Common::ViewProjectionState{};
        view.isValid = true;
        std::vector<Ui::AtlasVertex> allBodies, allShells;
        std::array<Tests::GpuTestContext::Pixel, 3> tintedCenters{};
        const auto drawBat = [&](const std::vector<Ui::AtlasVertex>& body,
                                 const std::vector<Ui::AtlasVertex>& shell)
        {
            gpu.Begin();
            Check(pipeline.UploadAndDraw(gpu.commands.Get(), atlas, body, gpu.width, gpu.height),
                  "Draw the material body below the sweet-spot shell");
            if (!shell.empty())
                Check(laggingPipeline.UploadAndDraw(gpu.commands.Get(), atlas, shell, gpu.width, gpu.height),
                      "Draw the translucent bat shell through the production alpha shader");
            return gpu.ReadPixels();
        };
        for (std::size_t index = 0; index < 3; ++index)
        {
            const auto material = static_cast<Physics::BatMaterial>(index);
            const double y = .18 - .18 * index;
            const double depth = Common::GetPitchDistanceMeters() +
                                 Common::DebugWorldLayout::BatterToPlateDistanceMeters - 1.;
            Common::BatOverlayState bat{{-.42, y, depth}, {.42, y, depth}, .035, material};
            std::vector<Ui::AtlasVertex> body, shell;
            Common::EmitProjectedSolidBatOverlay(view, gpu.width, gpu.height, bat,
                [&](const auto& p0, const auto& p1, const auto& p2, const float* color)
                {
                    auto& bucket = color[3] < .999F ? shell : body;
                    Ui::AppendUntexturedTriangle(bucket, p0, p1, p2, color, gpu.width, gpu.height);
                });
            Check(!body.empty() && !shell.empty(), "A material bat emits both its body and a separate translucent shell");
            Ui::SortAtlasTrianglesBackToFront(body);
            Ui::SortAtlasTrianglesBackToFront(shell);
            const auto barePixels = drawBat(body, {});
            const auto overlaidPixels = drawBat(body, shell);
            const auto& profile = Physics::GetBatMaterialProfile(material);
            const double sweetRatio = .5 * (profile.sweetSpotStartRatio + profile.sweetSpotEndRatio);
            Common::ScreenPoint sweetCenter{}, bodyCenter{};
            Check(Common::TryProjectWorldToScreen({-.42 + .84 * sweetRatio, y, -1.},
                      view, gpu.width, gpu.height, &sweetCenter) &&
                  Common::TryProjectWorldToScreen({-.20, y, -1.}, view, gpu.width, gpu.height, &bodyCenter),
                  "Project interior samples of the shell and unmarked material body");
            const auto pixelIndex = [&](const Common::ScreenPoint& point)
            {
                return static_cast<std::size_t>(std::lround(point.y)) * gpu.width +
                       static_cast<std::size_t>(std::lround(point.x));
            };
            const auto center = pixelIndex(sweetCenter);
            const auto& tint = Common::DebugWorldStyle::BatSweetSpotColor;
            Check(tint[3] > 0.F && tint[3] < .5F, "The green cylinder remains a faint translucent overlay");
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const double expected = barePixels[center][channel] * (1. - tint[3]) +
                                        255. * tint[channel] * tint[3];
                Check(std::abs(overlaidPixels[center][channel] - expected) < 3.,
                      "Actual GPU pixels blend one green shell layer over the visible material color");
            }
            Check(overlaidPixels[pixelIndex(bodyCenter)] == barePixels[pixelIndex(bodyCenter)],
                  "The shell leaves the material appearance outside the sweet zone unchanged");
            tintedCenters[index] = overlaidPixels[center];
            allBodies.insert(allBodies.end(), body.begin(), body.end());
            allShells.insert(allShells.end(), shell.begin(), shell.end());
        }
        Check(tintedCenters[0] != tintedCenters[1] && tintedCenters[1] != tintedCenters[2] &&
                  tintedCenters[0] != tintedCenters[2],
              "Wood, metal and urethane remain distinguishable through the green shell");
        Ui::SortAtlasTrianglesBackToFront(allShells);
        SavePreview(drawBat(allBodies, allShells), gpu.width, gpu.height, L"BatMaterialRegression.bmp");
    }
    {
        namespace Physics = BaseballSimulatorVR::Physics;
        view = Common::ViewProjectionState{};
        view.isValid = true;
        const double offset = Common::GetPitchDistanceMeters() +
                              Common::DebugWorldLayout::BatterToPlateDistanceMeters;
        const auto drawTrajectory = [&](const std::vector<Physics::Vector3>& worldPath)
        {
            std::vector<Physics::Vector3> simulationPath = worldPath;
            for (auto& point : simulationPath) point.z += offset;
            vertices.clear();
            Common::EmitProjectedBallAndTrajectoryMeshOverlay(view, gpu.width, gpu.height,
                simulationPath, {}, [&](const auto& a, const auto& b, const auto& c, const float* color)
                {
                    Ui::AppendUntexturedTriangle(vertices, a, b, c, color, gpu.width, gpu.height);
                }, 1.F, 0.F, false, Common::TrajectoryColorMode::Uniform);
            Ui::SortAtlasTrianglesBackToFront(vertices);
            return draw(pipeline);
        };
        for (double eye : {0., -.032, .032})
        {
            view.position.x = eye;
            const auto pixels = drawTrajectory({{0.,0.,-2.}, {0.,0.,-3.}, {0.,0.,-4.}});
            std::size_t trajectoryInk = 0;
            for (const auto& pixel : pixels) if (pixel[2] > 20) ++trajectoryInk;
            Check(trajectoryInk > 20, "Actual GPU trajectory pixels remain visible end-on and in both XR eye positions");
        }
        view.position = {};
        const auto clipped = drawTrajectory({{-.1,0.,-.01}, {.1,0.,-.2}});
        std::size_t clippedTrajectoryInk = 0;
        for (const auto& pixel : clipped) if (pixel[2] > 20) ++clippedTrajectoryInk;
        Check(clippedTrajectoryInk > 100, "A near-plane crossing retains its visible trajectory in actual GPU pixels");
        const auto before = drawTrajectory({{-.4,0.,-2.}, {0.,0.,-2.}});
        const auto after = drawTrajectory({{-.4,0.,-2.}, {0.,0.,-2.}, {.4,1.,-3.}});
        bool prefixUnchanged = true;
        std::size_t prefixInk = 0;
        for (UINT y = 0; y < gpu.height; ++y)
        for (UINT x = 0; x < gpu.width / 2 - 30; ++x)
        {
            const auto index = static_cast<std::size_t>(y) * gpu.width + x;
            prefixUnchanged &= before[index] == after[index];
            if (before[index][2] > 20) ++prefixInk;
        }
        Check(prefixUnchanged && prefixInk > 20,
              "Extending a curved trajectory leaves all earlier visible GPU pixels unchanged");
    }
    {
        view = Common::ViewProjectionState{};
        view.isValid = true;
        view.position = {0, 1.1, 0};
        view.fovUpRadians = .45;
        view.fovDownRadians = -.45;
        view.fovRightRadians = std::atan(std::tan(.45) * gpu.width / gpu.height);
        view.fovLeftRadians = -view.fovRightRadians;
        constexpr std::array<double, 3> heights{1.8, 1.2, .1};
        constexpr std::array<double, 2> times{-.12, 0};
        constexpr std::array<const wchar_t*, 3> forms{L"Overhand", L"Sidearm", L"Submarine"};
        for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 3; ++column)
        {
            vertices.clear();
            Common::PitcherPresentationState pitcher;
            pitcher.visible = true;
            pitcher.secondsFromRelease = times[row];
            pitcher.releasePositionWorld = {-.2, heights[column] + .18, -3.5};
            pitcher.moundHeightMeters = .18;
            std::size_t triangles = 0;
            Common::EmitProjectedPitcherMesh(pitcher, view, gpu.width, gpu.height,
                [&](auto a, auto b, auto c, const float* color)
                {
                    ++triangles;
                    Ui::AppendUntexturedTriangle(vertices, a, b, c, color, gpu.width, gpu.height);
                });
            Check(triangles > 300, "Every delivery form produces visible projected pitcher surfaces before and at release");
            Ui::SortAtlasTrianglesBackToFront(vertices);
            const auto deliveryPixels = draw(pipeline);
            const std::wstring filename = std::wstring{L"Pitcher"} + forms[column] +
                (row == 0 ? L"WindupRegression.bmp" : L"ReleaseRegression.bmp");
            SavePreview(deliveryPixels, gpu.width, gpu.height, filename.c_str());
            std::size_t uniformInk = 0;
            for (const auto& pixel : deliveryPixels)
            {
                if (pixel[0] > 80 && pixel[1] > 80 && pixel[2] > 80) ++uniformInk;
            }
            Check(uniformInk > 1000, "All three full-size pitcher forms survive the production GPU shader and depth ordering");
        }
    }
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
