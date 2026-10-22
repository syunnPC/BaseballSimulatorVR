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
    Tests::Check(landed == expected, "Four concise scoreboard rows retain all seven core metrics and their units");
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

    const auto plot = BuildFieldPitchPlotLayout(state);
    const auto overhead = BuildFieldPitchPlotLayout(state, FieldPitchPlotView::Overhead);
    const double areaWidth = 11.8 * scale;
    Tests::Check(std::abs(board.width - 44 * scale) < 1e-10 &&
        std::abs(board.height - 17 * scale) < 1e-10 &&
        std::abs(overhead.left + overhead.width - plot.left - areaWidth) < 1e-10 &&
        std::abs(plot.height - 9.6 * scale) < 1e-10 && overhead.height == plot.height &&
        overhead.bottom == plot.bottom && overhead.width == plot.width &&
        plot.left + plot.width < overhead.left,
        "The catcher and overhead views share the original scoreboard drawing area side by side");
    const auto plateEnd = overhead.Project(state.pitchTrace.back());
    const auto releaseEnd = overhead.Project(state.pitchTrace.front());
    Tests::Check(plateEnd.y < releaseEnd.y && std::abs(plateEnd.y - overhead.bottom - .45 * scale) < 1e-10 &&
        std::abs(releaseEnd.y - overhead.bottom - overhead.height + .45 * scale) < 1e-10 &&
        overhead.centerXMeters == plot.centerXMeters &&
        std::abs(overhead.unitsPerMeter * (.18 + .35) - (overhead.width - .9 * scale)) < 1e-10 &&
        overhead.Project({.1, 1, -8}).x > overhead.Project({0, 1, -8}).x &&
        overhead.Project({.1, 2, -8}).y == overhead.Project({.1, -3, -8}).y,
        "From above, the flight runs from the pitcher at the top to the plate at the bottom with its sideways movement across the panel");
    const auto center = plot.Project({0, 1, 0});
    const auto pitchRight = plot.Project({.1, 1, -20});
    const auto pitchUp = plot.Project({0, 1.1, 100});
    Tests::Check(pitchRight.x > center.x && pitchUp.y > center.y &&
        std::abs(pitchRight.x - center.x - (pitchUp.y - center.y)) < 1e-10,
        "Catcher view maps positive simulation x right and uses one orthographic scale for both axes");
    Tests::Check(std::abs(plot.Project({.1, 1, -20}).x - plot.Project({.1, 1, 20}).x) < 1e-10,
        "Depth never changes the catcher-view movement coordinate");
    for (const auto& [dx, dy] : std::array{
        std::pair{.001, .0002}, std::pair{.0002, .001}, std::pair{.001, .001},
        std::pair{.001, 0.0}, std::pair{0.0, .001}})
    {
        auto tiny = state;
        tiny.pitchTrace = {{7, -4, -18}, {7 + .4 * dx, -4 + .4 * dy, -9}, {7 + dx, -4 + dy, 0}};
        const auto fitted = BuildFieldPitchPlotLayout(tiny);
        const auto first = fitted.Project(tiny.pitchTrace.front());
        const auto last = fitted.Project(tiny.pitchTrace.back());
        // The trace fills the panel inside its fixed margin along its limiting dimension.
        const double fractionX = std::abs(last.x - first.x) / (fitted.width - .9 * scale);
        const double fractionY = std::abs(last.y - first.y) / (fitted.height - .9 * scale);
        Tests::Check(fitted.hasVisibleMovement && (std::max)(fractionX, fractionY) > .999 &&
            std::abs(last.x - first.x - dx * fitted.unitsPerMeter) < 1e-10 &&
            std::abs(last.y - first.y - dy * fitted.unitsPerMeter) < 1e-10 &&
            std::abs((first.x + last.x) * .5 - fitted.left - fitted.width * .5) < 1e-10 &&
            std::abs((first.y + last.y) * .5 - fitted.bottom - fitted.height * .5) < 1e-10,
            "Even millimeter movement fills the limiting plot dimension while preserving XY aspect and centering the trace");
        const auto fittedOverhead = BuildFieldPitchPlotLayout(tiny, FieldPitchPlotView::Overhead);
        std::size_t movementTriangles = 0, markerTriangles = 0;
        EmitFieldScoreboardGeometry(tiny,
            [&](const auto& a, const auto& b, const auto& c, const float*)
            {
                const bool movement = std::abs(a.z - fitted.z) < 1e-8;
                const bool marker = std::abs(a.z - fitted.z - .02) < 1e-8;
                if (!movement && !marker) return;
                if (movement) ++movementTriangles;
                if (marker) ++markerTriangles;
                const auto& panel = a.x < fittedOverhead.left ? fitted : fittedOverhead;
                for (const auto& p : {a, b, c})
                    Tests::Check(p.x > panel.left && p.x < panel.left + panel.width &&
                        p.y > panel.bottom && p.y < panel.bottom + panel.height,
                        "Magnified trace strokes and the round endpoint stay strictly inside their own panel");
            }, [](auto&&...) {});
        Tests::Check(movementTriangles == 8 && markerTriangles == 64,
            "Both views draw their trace and endpoint without a scale-limiting zone outline");
        tiny.zoneWidthMeters = 100;
        tiny.zoneBottomMeters = -100;
        tiny.zoneTopMeters = 100;
        const auto unrelatedZone = BuildFieldPitchPlotLayout(tiny);
        Tests::Check(fitted.unitsPerMeter == unrelatedZone.unitsPerMeter &&
            fitted.centerXMeters == unrelatedZone.centerXMeters && fitted.centerYMeters == unrelatedZone.centerYMeters,
            "Physical strike-zone bounds cannot reduce movement-chart magnification");
    }
    auto straight = state;
    straight.pitchTrace = {{.08, .9, -18}, {.08, .9, -9}, {.08, .9, 0}};
    const auto straightPlot = BuildFieldPitchPlotLayout(straight);
    Tests::Check(!straightPlot.hasVisibleMovement && std::isfinite(straightPlot.unitsPerMeter) &&
        std::abs(straightPlot.Project(straight.pitchTrace.front()).x -
            straightPlot.Project(straight.pitchTrace.back()).x) < 1e-10 &&
        std::abs(straightPlot.Project(straight.pitchTrace.front()).y -
            straightPlot.Project(straight.pitchTrace.back()).y) < 1e-10,
        "A zero-change trajectory remains one point with a finite physical-ball-diameter reference scale");
    auto roundoff = straight;
    roundoff.pitchTrace[1].x += 1e-12;
    roundoff.pitchTrace[1].y -= 1e-12;
    const auto roundoffPlot = BuildFieldPitchPlotLayout(roundoff);
    Tests::Check(!roundoffPlot.hasVisibleMovement &&
        roundoffPlot.unitsPerMeter == straightPlot.unitsPerMeter,
        "Floating-point residuals cannot magnify an otherwise straight trajectory into false movement");
    const auto straightOverhead = BuildFieldPitchPlotLayout(straight, FieldPitchPlotView::Overhead);
    std::size_t straightSegments = 0, straightEndpoint = 0, overheadSegments = 0;
    EmitFieldScoreboardGeometry(straight,
        [&](const auto& a, const auto& b, const auto& c, const float* color)
        {
            if (a.x >= straightOverhead.left)
            {
                // From above the same pitch is a straight run down the middle to the plate.
                if (std::abs(a.z - straightPlot.z) < 1e-8 && color[0] == .24F && color[1] == .91F)
                {
                    ++overheadSegments;
                    for (const auto& p : {a, b, c})
                        Tests::Check(std::abs(p.x - straightOverhead.left - straightOverhead.width * .5) <
                            .07 * scale + 1e-10, "A pitch with no sideways movement runs straight down the overhead view");
                }
                return;
            }
            if (std::abs(a.z - straightPlot.z) < 1e-8 && color[0] == .24F && color[1] == .91F)
                ++straightSegments;
            if (std::abs(a.z - straightPlot.z - .02) > 1e-8) return;
            ++straightEndpoint;
            const auto expected = straightPlot.Project(straight.pitchTrace.back());
            const double radius = straightPlot.endpointRadius;
            Tests::Check(std::hypot(a.x - expected.x, a.y - expected.y) < 1e-10 &&
                std::abs(std::hypot(b.x - a.x, b.y - a.y) - radius) < 1e-10 &&
                std::abs(std::hypot(c.x - a.x, c.y - a.y) - radius) < 1e-10,
                "A collapsed trajectory renders a small circular endpoint marker at its actual endpoint");
        }, [](auto&&...) {});
    Tests::Check(straightSegments == 0 && straightEndpoint == 32 && overheadSegments == 4,
        "A straight ray emits a single endpoint disc without a false diagonal segment in the catcher view");
    state.pitchTrace.push_back({-2.4, 3.1, 0});
    const auto largePlot = BuildFieldPitchPlotLayout(state);
    const auto largeOverhead = BuildFieldPitchPlotLayout(state, FieldPitchPlotView::Overhead);
    for (const auto* panel : {&largePlot, &largeOverhead})
        for (const auto& point : state.pitchTrace)
        {
            const auto p = panel->Project(point);
            const double radius = panel->endpointRadius;
            Tests::Check(p.x - radius > panel->left && p.x + radius < panel->left + panel->width &&
                p.y - radius > panel->bottom && p.y + radius < panel->bottom + panel->height,
                "The complete endpoint marker and extreme movement coordinates fit within each view");
        }
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
                    "The four core rows retain readable glyph heights and ample text width");
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
        "The production board retains four core metrics and emits analysis and diagram labels");
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
