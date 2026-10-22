#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/FieldPresentationOverlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

void RunFieldStatisticsCompactTests()
{
    using namespace BaseballSimulatorVR::Common;
    FieldStatisticsState state;
    state.visible = true;
    state.pitchName = L"フォーシーム";
    state.pitchSpeedMetersPerSecond = 43.0;
    state.pitchSpinRpm = 2450.0;
    state.hasBattedMetrics = true;
    state.battedMetricsLive = true;
    state.battedHasGroundContact = true;
    state.battedLaunchSpeedMetersPerSecond = 50.0;
    state.battedLaunchAngleDegrees = 28.5;
    state.battedDistanceMeters = 115.7;
    state.battedMaxHeightMeters = 31.2;

    const std::vector<std::wstring> expected{
        L"球種  フォーシーム",
        L"球速  154.8 km/h    回転  2450 rpm",
        L"初速  180.0 km/h    角度  28.5°",
        L"飛距離  115.7 m    最高点  31.2 m"
    };
    const auto landed = BuildFieldStatisticsLines(state);
    Tests::Check(landed == expected, "Four concise scoreboard rows show all seven core metrics with their units");
    for (const auto& row : landed)
    {
        for (std::wstring_view removed : std::array<std::wstring_view, 10>{
            L"BASEBALL", L"LIVE STATS", L"ZONE", L"校正済み", L"READY", L"LANDED",
            L"IN FLIGHT", L"RESULT", L"着地済み", L"打撃待機" })
        {
            Tests::Check(row.find(removed) == std::wstring::npos,
                "The fixed scoreboard contains no title, calibration diagnostics or redundant status description");
        }
    }
    state.battedMetricsLive = false;
    Tests::Check(BuildFieldStatisticsLines(state) == landed,
        "A completed result retains its numbers without adding a result status row");
    state.battedHasGroundContact = false;
    const auto flying = BuildFieldStatisticsLines(state);
    Tests::Check(flying.size() == 4 && flying[2] == expected[2] &&
        flying[3] == L"飛距離  -- m    最高点  31.2 m",
        "Before landing, distance uses a placeholder while launch and current height remain visible");

    state.pitchMetricsVisible = false;
    state.pitchName = L"秘密の次球種";
    const std::vector<std::wstring> placeholders{
        L"球種  --", L"球速  -- km/h    回転  -- rpm",
        L"初速  -- km/h    角度  --°", L"飛距離  -- m    最高点  -- m"
    };
    const auto hiddenPitch = BuildFieldStatisticsLines(state);
    Tests::Check(hiddenPitch[0] == placeholders[0] && hiddenPitch[1] == placeholders[1] &&
        hiddenPitch[2] == flying[2] && hiddenPitch[3] == flying[3],
        "Pitch concealment preserves current batted metrics, including tee batting");
    state.pitchMetricsVisible = true;
    state.hasBattedMetrics = false;
    const auto newBall = BuildFieldStatisticsLines(state);
    Tests::Check(newBall[0] == L"球種  秘密の次球種" && newBall[1] == expected[1] &&
        newBall[2] == placeholders[2] && newBall[3] == placeholders[3],
        "A new ball shows the allowed pitch values and clears stale launch and flight statistics");

    const auto board = BuildFieldScoreboardLayout();
    const double scale = board.unitsPerLayoutUnit;
    state.zoneCall = PitchZoneCall::Strike;
    state.hasPitchBreak = true;
    state.pitchHorizontalBreakMeters = -.123;
    state.pitchVerticalBreakMeters = .287;
    state.pitchTrace = {{-.35, 2.05, -18}, {-.10, 1.55, -8}, {.18, 1.10, 0}};
    const auto analysis = BuildFieldPitchAnalysisLines(state);
    Tests::Check(analysis[0] == L"判定  ストライク" &&
        analysis[1] == L"変化  横 -12.3 cm    縦 +28.7 cm",
        "Pitch analysis shows the call and explicitly signed break in centimeters");
    for (const auto& [call, expectedCall] : std::array{
        std::pair{PitchZoneCall::Pending, L"判定  --"},
        std::pair{PitchZoneCall::Ball, L"判定  ボール"},
        std::pair{PitchZoneCall::Contact, L"判定  打撃"}})
    {
        state.zoneCall = call;
        Tests::Check(BuildFieldPitchAnalysisLines(state)[0] == expectedCall,
            "Pending and completed zone outcomes have distinct Japanese labels");
    }
    state.pitchHorizontalBreakMeters = -.00001;
    Tests::Check(BuildFieldPitchAnalysisLines(state)[1].find(L"横 +0.0 cm") != std::wstring::npos,
        "Rounded zero break never displays misleading negative zero");

    // A pitch released 2 m up on the center line that tails right and sinks into the zone,
    // traced on over the plate as the flight analysis records it.
    state.plateFrontDepthMeters = 18.44;
    state.pitchTrace.clear();
    for (double depth = 0.0; depth < 18.44 + .47; depth += .3)
    {
        const double f = depth / 18.44;
        state.pitchTrace.push_back({.25 * f * f, 2.0 - .4 * f - .7 * f * f, depth});
    }
    const auto plot = BuildFieldPitchPlotLayout(state);
    const auto overhead = BuildFieldPitchPlotLayout(state, FieldPitchPlotView::Overhead);
    const double areaWidth = 11.8 * scale;
    Tests::Check(std::abs(board.width - 44 * scale) < 1e-10 &&
        std::abs(board.height - 17 * scale) < 1e-10 &&
        std::abs(overhead.left + overhead.width - plot.left - areaWidth) < 1e-10 &&
        std::abs(plot.height - 9.6 * scale) < 1e-10 && overhead.height == plot.height &&
        overhead.bottom == plot.bottom && overhead.width == plot.width &&
        plot.left + plot.width < overhead.left,
        "The catcher and overhead views sit side by side in the board's 11.8 by 9.6 unit drawing area");

    // From behind the plate: a pinhole view from about the umpire's eyes.
    const double plateFront = state.plateFrontDepthMeters, eyeHeight = plot.eye.y;
    const auto at = [&](const double x, const double y, const double depth) { return plot.Project({x, y, depth}); };
    Tests::Check(std::abs(plot.eye.z - plateFront - FieldPitchViewEyeBehindPlateMeters) < 1e-12 &&
        at(.1, 1, plateFront).x > at(0, 1, plateFront).x && at(0, 1.2, plateFront).y > at(0, 1, plateFront).y,
        "The catcher's view looks toward the pitcher with x to the right and height up");
    Tests::Check(std::abs((at(.2, eyeHeight, plateFront).x - at(0, eyeHeight, plateFront).x) -
            2 * (at(.2, eyeHeight, plateFront - 2).x - at(0, eyeHeight, plateFront - 2).x)) < 1e-10 &&
        std::abs((at(0, eyeHeight + .3, plateFront).y - at(0, eyeHeight, plateFront).y) -
            2 * (at(0, eyeHeight + .3, plateFront - 2).y - at(0, eyeHeight, plateFront - 2).y)) < 1e-10,
        "Twice as far from the eye looks half as big: the catcher's view is in perspective");
    const auto zone = BuildFieldPitchPlotZone(state);
    const auto zoneLow = plot.Project(zone[0]), zoneHigh = plot.Project(zone[2]);
    const auto release = plot.Project(state.pitchTrace.front());
    Tests::Check(release.x > zoneLow.x && release.x < zoneHigh.x && release.y > zoneLow.y && release.y < zoneHigh.y,
        "From behind the plate the pitcher's release is seen through the strike zone");
    const auto plate = BuildFieldPitchPlotPlate(plateFront);
    bool plateBelowZone = true;
    for (const auto& corner : plate) plateBelowZone = plateBelowZone && plot.Project(corner).y < zoneLow.y;
    Tests::Check(plateBelowZone && plot.Project(plate[3]).y < plot.Project(plate[0]).y &&
        plot.Project(plate[2]).x - plot.Project(plate[4]).x > plot.Project(plate[1]).x - plot.Project(plate[0]).x,
        "Home plate lies below the zone, its point nearest the catcher drawn lowest and widest apart");

    // From above: the pitcher at the top, home plate in its true shape at the bottom.
    const auto top = overhead.ProjectPlateCorner(plate[0]), right = overhead.ProjectPlateCorner(plate[1]);
    const auto apex = overhead.ProjectPlateCorner(plate[3]);
    Tests::Check(std::abs((right.x - top.x) - (top.y - apex.y)) < 1e-10 && right.y == top.y &&
        std::abs(overhead.ProjectPlateCorner(plate[2]).y - (top.y + apex.y) * .5) < 1e-10 &&
        std::abs(right.x - top.x - HomePlateWidthMeters * overhead.unitsPerMeter) < 1e-10,
        "Seen from above, home plate keeps its true pentagon shape at the path's sideways scale, point toward the catcher");
    Tests::Check(std::abs(top.y - overhead.Project({0, 1, plateFront}).y) < 1e-10 &&
        std::abs(top.x - overhead.Project({-HomePlateWidthMeters * .5, 1, plateFront}).x) < 1e-10,
        "The plate's front edge lies where the path reaches the plate front, so the crossing reads against the plate");
    Tests::Check(std::abs(overhead.Project(state.pitchTrace.front()).y - (overhead.bottom + overhead.height - .45 * scale)) < 1e-10 &&
        std::abs(apex.y - (overhead.bottom + .45 * scale)) < 1e-10 &&
        overhead.Project({.1, 1, 9}).x > overhead.Project({0, 1, 9}).x &&
        overhead.Project({.1, 2, 9}).y == overhead.Project({.1, -3, 9}).y,
        "The overhead view runs from the release at the top to the plate at the bottom, x to the right");
    const auto drop = [&](const double from, const double to)
    { return overhead.Project({0, 1, from}).y - overhead.Project({0, 1, to}).y; };
    Tests::Check(drop(0, 1) > 0 && std::abs(drop(0, 1) - drop(plateFront - 1, plateFront)) < 1e-10 &&
        std::abs(drop(0, 1) - drop(plateFront, plateFront + 1)) < 1e-10 &&
        overhead.depthUnitsPerMeter < overhead.unitsPerMeter,
        "The flight's depth is squeezed evenly all the way over the plate, so the path bends only where the ball did");
    // Everything each view draws stays in its own panel, and the path thickens as it nears the catcher.
    const auto checkPanels = [&](const FieldStatisticsState& input, const char* context)
    {
        const auto catcherView = BuildFieldPitchPlotLayout(input);
        const auto overheadView = BuildFieldPitchPlotLayout(input, FieldPitchPlotView::Overhead);
        std::size_t plateTriangles = 0, zoneTriangles = 0, pathTriangles = 0, endpointTriangles = 0;
        std::vector<double> catcherWidths;
        EmitFieldScoreboardGeometry(input,
            [&](const auto& a, const auto& b, const auto& c, const float*)
            {
                const double depth = a.z - board.centerZ;
                const bool plateLayer = std::abs(depth - .04) < 1e-8, zoneLayer = std::abs(depth - .05) < 1e-8;
                const bool pathLayer = std::abs(depth - .08) < 1e-8, endpointLayer = std::abs(depth - .10) < 1e-8;
                if (!plateLayer && !zoneLayer && !pathLayer && !endpointLayer) return;
                plateTriangles += plateLayer;
                zoneTriangles += zoneLayer;
                pathTriangles += pathLayer;
                endpointTriangles += endpointLayer;
                const auto& panel = a.x < overheadView.left ? catcherView : overheadView;
                if (pathLayer && &panel == &catcherView) catcherWidths.push_back(std::hypot(a.x - b.x, a.y - b.y));
                for (const auto& p : {a, b, c})
                    Tests::Check(p.x > panel.left && p.x < panel.left + panel.width &&
                        p.y > panel.bottom && p.y < panel.bottom + panel.height,
                        std::string("Home plate, the zone, the path and its end stay inside their own panel: ") + context);
            }, [](auto&&...) {});
        const std::size_t segments = input.pitchTrace.size() - 1;
        Tests::Check(plateTriangles == 6 && zoneTriangles == 8 && pathTriangles == 4 * segments &&
            endpointTriangles == 64,
            std::string("Each view draws home plate, the catcher's view the zone outline, both the path and its end: ") + context);
        // Each segment is two triangles; the first one's leading edge spans the stroke's width.
        Tests::Check(catcherWidths.size() >= 4 && catcherWidths[catcherWidths.size() - 2] > 2 * catcherWidths.front(),
            std::string("From behind the plate the ball's path thickens as it comes closer: ") + context);
    };
    checkPanels(state, "curving pitch");
    auto wild = state;
    for (auto& point : wild.pitchTrace) point.x -= 1.6 * point.z / 18.44;
    checkPanels(wild, "wild pitch");
    const auto wildOverhead = BuildFieldPitchPlotLayout(wild, FieldPitchPlotView::Overhead);
    Tests::Check(wildOverhead.unitsPerMeter < overhead.unitsPerMeter &&
        std::abs(wildOverhead.ProjectPlateCorner(plate[1]).x - wildOverhead.ProjectPlateCorner(plate[0]).x -
            (wildOverhead.ProjectPlateCorner(plate[0]).y - wildOverhead.ProjectPlateCorner(plate[3]).y)) < 1e-10,
        "A pitch far off the plate widens the overhead view so it stays in sight");
    auto hidden = state;
    hidden.pitchMetricsVisible = false;
    std::size_t hiddenPath = 0, hiddenReference = 0;
    EmitFieldScoreboardGeometry(hidden,
        [&](const auto& a, const auto&, const auto&, const float*)
        {
            const double depth = a.z - board.centerZ;
            if (std::abs(depth - .08) < 1e-8 || std::abs(depth - .10) < 1e-8) ++hiddenPath;
            if (std::abs(depth - .04) < 1e-8 || std::abs(depth - .05) < 1e-8) ++hiddenReference;
        }, [](auto&&...) {});
    Tests::Check(hiddenPath == 0 && hiddenReference == 14,
        "A concealed pitch leaves home plate and the zone with no path drawn");
    const auto largePlot = plot;
    const auto largeOverhead = overhead;
    std::vector<std::wstring> labels;
    struct TextBounds { double left, top, width, height; };
    std::vector<TextBounds> bounds;
    std::size_t triangles = 0;
    EmitFieldScoreboardGeometry(state,
        [&](const auto& a, const auto& b, const auto& c, const float*)
        {
            ++triangles;
            for (const auto& p : {a, b, c})
                Tests::Check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
                    p.x >= -board.width * .5 - .51 * scale &&
                    p.x <= board.width * .5 + .51 * scale && p.y >= 0 &&
                    p.y <= board.bottom + board.height + .51 * scale,
                    "Scoreboard frame, diagrams and supports emit finite in-bounds geometry");
        },
        [&](std::wstring_view label, double left, double top, double width, double height,
            const std::array<float, 4>&, double z)
        {
            labels.emplace_back(label);
            if (labels.size() <= 4)
                Tests::Check(std::abs(height - 1.65 * scale) < 1e-10 && width >= 27 * scale,
                    "The four core rows use readable glyph heights and ample text width");
            Tests::Check(top <= board.bottom + board.height && top - height >= board.bottom &&
                left >= -board.width * 0.5 && left + width <= board.width * 0.5 && z > board.centerZ,
                "Every label fits within the board face");
            for (const auto& other : bounds)
                Tests::Check(left + width <= other.left || other.left + other.width <= left ||
                    top <= other.top - other.height || other.top <= top - height,
                    "Metric, analysis, chart and bat labels never overlap");
            bounds.push_back({left, top, width, height});
        });
    Tests::Check(labels.size() >= 8 && std::equal(newBall.begin(), newBall.end(), labels.begin()) && triangles > 0,
        "The production board emits the four core metric rows first, then analysis and diagram labels");
    Tests::Check(std::find(labels.begin(), labels.end(), L"捕手目線") != labels.end() &&
        std::find(labels.begin(), labels.end(), L"真上から") != labels.end(),
        "The two movement diagrams name the catcher's and the overhead perspective in concise titles");

    std::size_t ballTriangles = 0;
    EmitFieldScoreboardGeometry(state,
        [&](const auto& a, const auto& b, const auto& c, const float*)
        {
            if (std::abs(a.z - board.centerZ - .10) > 1e-8) return;
            ++ballTriangles;
            const auto& panel = a.x < largeOverhead.left ? largePlot : largeOverhead;
            const auto expected = panel.Project(state.pitchTrace.back());
            const double radius = panel.endpointRadius;
            Tests::Check(std::abs(a.x - expected.x) < 1e-10 && std::abs(a.y - expected.y) < 1e-10 &&
                std::abs(std::hypot(b.x - a.x, b.y - a.y) - radius) < 1e-10 &&
                std::abs(std::hypot(c.x - a.x, c.y - a.y) - radius) < 1e-10,
                "Endpoint disc is centered on the actual trace end with a fixed display radius");
        }, [](auto&&...) {});
    Tests::Check(ballTriangles == 64 && std::abs(largePlot.endpointRadius - .18 * scale) < 1e-10 &&
        largeOverhead.endpointRadius == largePlot.endpointRadius,
        "A visible pitch trace emits one small round marker per view independent of auto-fit magnification");

    // Concealment must remove all stale pitch-dependent geometry, including auto-fit changes.
    const auto capture = [&](const FieldStatisticsState& input)
    {
        std::pair<std::vector<double>, std::vector<std::wstring>> result;
        EmitFieldScoreboardGeometry(input,
            [&](const auto& a, const auto& b, const auto& c, const float* color)
            {
                for (const auto& p : {a, b, c})
                    result.first.insert(result.first.end(), {p.x, p.y, p.z});
                for (int i = 0; i < 4; ++i) result.first.push_back(color[i]);
            },
            [&](std::wstring_view label, auto&&...) { result.second.emplace_back(label); });
        return result;
    };
    state.pitchMetricsVisible = false;
    auto clean = state;
    clean.pitchTrace.clear();
    clean.zoneCall = PitchZoneCall::Pending;
    clean.hasPitchBreak = false;
    clean.pitchHorizontalBreakMeters = clean.pitchVerticalBreakMeters = 0;
    Tests::Check(capture(state) == capture(clean),
        "Concealed stale trajectory, break and call cannot affect text or any diagram geometry");

    state.hasBattedMetrics = true;
    state.hasBatContactPosition = true;
    state.contactBatLengthMeters = .84;
    const auto& sweet = BaseballSimulatorVR::Physics::GetBatMaterialProfile(
        BaseballSimulatorVR::Physics::BatMaterial::Wood);
    for (const double fraction : {0.0, .5 * (sweet.sweetSpotStartRatio + sweet.sweetSpotEndRatio), 1.0})
    {
        state.batContactFraction = fraction;
        std::size_t markerTriangles = 0, endpointTriangles = 0, coreTriangles = 0;
        EmitFieldScoreboardGeometry(state,
            [&](const auto& a, const auto& b, const auto& c, const float* color)
            {
                if (std::abs(a.z - board.centerZ - .12) < 1e-8)
                {
                    ++markerTriangles;
                    const bool coreHit = fraction >= sweet.sweetSpotStartRatio && fraction <= sweet.sweetSpotEndRatio;
                    Tests::Check(coreHit ? color[1] > color[0] : color[0] > color[1],
                        "Contact marker is green inside the shared sweet spot and amber at either edge");
                    const double expectedX = plot.left + .4 * scale + (areaWidth - .8 * scale) * fraction;
                    if (markerTriangles > 2)
                        Tests::Check(std::abs(a.x - expectedX) < 1e-10,
                            "Bat contact marker uses the full handle-to-tip normalized coordinate");
                }
                if (std::abs(a.z - board.centerZ - .10) < 1e-8) ++endpointTriangles;
                if (std::abs(a.z - board.centerZ - .09) < 1e-8)
                {
                    ++coreTriangles;
                    for (const auto& p : {a, b, c})
                        Tests::Check(p.x >= plot.left + .4 * scale +
                            (areaWidth - .8 * scale) * sweet.sweetSpotStartRatio - 1e-10 &&
                            p.x <= plot.left + .4 * scale +
                            (areaWidth - .8 * scale) * sweet.sweetSpotEndRatio + 1e-10,
                            "Green core follows the physical bat material's normalized sweet zone");
                }
            }, [](auto&&...) {});
        Tests::Check(markerTriangles == 34 && endpointTriangles == 0 && coreTriangles == 2,
            "Tee batting draws its contact marker and shared sweet spot while concealed pitch endpoint stays absent");
    }
    state.hasBattedMetrics = false;
    clean = state;
    clean.hasBatContactPosition = false;
    Tests::Check(capture(state) == capture(clean),
        "Stale contact storage does not survive a new ball without batted metrics");
    state.visible = false;
    std::size_t hiddenTriangles = 0;
    std::size_t hiddenLabels = 0;
    EmitFieldScoreboardGeometry(state,
        [&](const auto&, const auto&, const auto&, const float*) { ++hiddenTriangles; },
        [&](auto&&...) { ++hiddenLabels; });
    Tests::Check(hiddenTriangles == 0 && hiddenLabels == 0,
        "A hidden scoreboard emits neither its compact text nor its background");
}
