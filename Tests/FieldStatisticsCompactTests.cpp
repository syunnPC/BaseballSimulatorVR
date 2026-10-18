#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/FieldPresentationOverlay.hpp"

#include <array>
#include <cmath>
#include <string>
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
    Tests::Check(BuildFieldStatisticsLines(state) == placeholders,
        "Preview-off conceals upcoming pitch values and even populated previous batted values");
    state.pitchMetricsVisible = true;
    state.hasBattedMetrics = false;
    const auto newBall = BuildFieldStatisticsLines(state);
    Tests::Check(newBall[0] == L"球種  秘密の次球種" && newBall[1] == expected[1] &&
        newBall[2] == placeholders[2] && newBall[3] == placeholders[3],
        "A new ball shows the allowed pitch values and clears stale launch and flight statistics");

    const auto board = BuildFieldScoreboardLayout();
    const double scale = board.width / 30.0;
    Tests::Check(std::abs(board.height - 12.0 * scale) < 1e-10,
        "The compact board removes the unused vertical space without narrowing its text area");
    std::vector<std::wstring> labels;
    std::size_t triangles = 0;
    EmitFieldScoreboardGeometry(state,
        [&](const auto&, const auto&, const auto&, const float*) { ++triangles; },
        [&](std::wstring_view label, double left, double top, double width, double height,
            const std::array<float, 4>&, double z)
        {
            labels.emplace_back(label);
            Tests::Check(std::abs(height - 1.65 * scale) < 1e-10 &&
                std::abs(width - 28.0 * scale) < 1e-10,
                "Compact scoreboard preserves the existing glyph height and available line width");
            Tests::Check(top <= board.bottom + board.height && top - height >= board.bottom &&
                left >= -board.width * 0.5 && left + width <= board.width * 0.5 && z > board.centerZ,
                "Every compact label fits within the board face at its unchanged readable size");
        });
    Tests::Check(labels == newBall && triangles > 0,
        "The production geometry emits exactly the four compact labels");
    state.visible = false;
    std::size_t hiddenTriangles = 0;
    std::size_t hiddenLabels = 0;
    EmitFieldScoreboardGeometry(state,
        [&](const auto&, const auto&, const auto&, const float*) { ++hiddenTriangles; },
        [&](auto&&...) { ++hiddenLabels; });
    Tests::Check(hiddenTriangles == 0 && hiddenLabels == 0,
        "A hidden scoreboard emits neither its compact text nor its background");
}
