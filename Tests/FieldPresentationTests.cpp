#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/FieldPresentationOverlay.hpp"

#include <cmath>

void RunFieldPresentationTests()
{
    using namespace BaseballSimulatorVR;
    Common::PitcherPresentationState pitcher{};
    pitcher.visible = true;
    pitcher.releasePositionWorld = {-.31, 1.98, -19.94};
    pitcher.moundHeightMeters = .18;
    pitcher.secondsFromRelease = 0;
    const auto release = Common::BuildPitcherPose(pitcher);
    Tests::Check(Physics::Length(release.heldBall - pitcher.releasePositionWorld) < 1e-10,
        "Pitcher ball center matches the physics release exactly");
    Tests::Check(release.leftFoot.y > pitcher.moundHeightMeters && release.rightFoot.y > pitcher.moundHeightMeters,
        "Pitcher feet stand on the mound rather than at world zero");
    Tests::Check(Physics::Length(release.rightHand - pitcher.releasePositionWorld) < 1e-10,
        "Default right-handed throwing hand retains its existing release position");
    for (const double scale : {.5, 1.0, 1.5}) {
        for (const double referenceTime : {-1.2, -.06, 0.0, .2, .6}) {
            auto right = pitcher;
            right.armSpeedScale = scale;
            right.secondsFromRelease = referenceTime == -1.2 ? -1.2 : referenceTime / scale;
            auto left = right;
            left.throwsLeftHanded = true;
            const auto r = Common::BuildPitcherPose(right);
            const auto l = Common::BuildPitcherPose(left);
            const auto mirror = [&](Physics::Vector3 p) { p.x = 2 * pitcher.releasePositionWorld.x - p.x; return p; };
            const std::array pairs{
                std::pair{r.pelvis, l.pelvis}, std::pair{r.chest, l.chest}, std::pair{r.head, l.head},
                std::pair{r.rightShoulder, l.leftShoulder}, std::pair{r.rightElbow, l.leftElbow},
                std::pair{r.rightHand, l.leftHand}, std::pair{r.leftShoulder, l.rightShoulder},
                std::pair{r.leftElbow, l.rightElbow}, std::pair{r.leftHand, l.rightHand},
                std::pair{r.rightHip, l.leftHip}, std::pair{r.rightKnee, l.leftKnee},
                std::pair{r.rightFoot, l.leftFoot}, std::pair{r.leftHip, l.rightHip},
                std::pair{r.leftKnee, l.rightKnee}, std::pair{r.leftFoot, l.rightFoot},
                std::pair{r.heldBall, l.heldBall}};
            for (const auto& [a, b] : pairs)
                Tests::Check(Physics::Length(mirror(a) - b) < 1e-10,
                    "Every left-handed joint mirrors its right-handed counterpart about the release plane");
            auto reference = pitcher;
            reference.secondsFromRelease = referenceTime;
            const auto referencePose = Common::BuildPitcherPose(reference);
            Tests::Check(Physics::Length(r.rightHand - referencePose.rightHand) < 1e-10,
                "Preset tempo scales the final arm swing and follow-through while keeping windup start fixed");
            if (referenceTime == 0) {
                Tests::Check(Physics::Length(l.leftHand - left.releasePositionWorld) < 1e-10 &&
                    Physics::Length(l.heldBall - left.releasePositionWorld) < 1e-10,
                    "Left throwing hand and ball meet the unchanged physical release at every tempo");
            }
            Common::IndexedWorldMesh rightMesh, leftMesh;
            Common::BuildPitcherWorldMesh(right, rightMesh);
            Common::BuildPitcherWorldMesh(left, leftMesh);
            Tests::Check(rightMesh.vertices.size() == leftMesh.vertices.size() &&
                rightMesh.triangles.size() == leftMesh.triangles.size(),
                "Mirroring preserves full pitcher, glove, ball and mound mesh topology");
            for (std::size_t i = 0; i < rightMesh.vertices.size(); ++i)
                Tests::Check(Physics::Length(mirror(rightMesh.vertices[i]) - leftMesh.vertices[i]) < 1e-10,
                    "All left-handed mesh vertices reflect about the release plane");
            for (std::size_t i = 0; i < rightMesh.triangles.size(); ++i) {
                const auto& a = rightMesh.triangles[i].indices;
                const auto& b = leftMesh.triangles[i].indices;
                Tests::Check(a[0] == b[0] && a[1] == b[2] && a[2] == b[1],
                    "Reflected triangles reverse winding to retain outward faces");
            }
        }
    }
    {
        Common::PitcherWorldMeshCache cache;
        (void)cache.Get(pitcher);
        const auto initialBuilds = cache.BuildCount();
        auto changed = pitcher;
        changed.throwsLeftHanded = true;
        (void)cache.Get(changed);
        Tests::Check(cache.BuildCount() == initialBuilds + 1,
            "Changing throwing hand invalidates the pitcher mesh cache at the same timestamp");
        changed.armSpeedScale = .5;
        (void)cache.Get(changed);
        Tests::Check(cache.BuildCount() == initialBuilds + 2,
            "Changing preset tempo invalidates the pitcher mesh cache");
        (void)cache.Get(changed);
        Tests::Check(cache.BuildCount() == initialBuilds + 2,
            "An unchanged hand and tempo reuse the cached pitcher mesh");
    }
    const auto finite = [](const Physics::Vector3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    std::size_t priorTriangles = 0, releaseTriangles = 0;
    for (int frame = 0; frame <= 180; ++frame) {
        pitcher.secondsFromRelease = -1.2 + frame * .01;
        const auto pose = Common::BuildPitcherPose(pitcher);
        Tests::Check(finite(pose.rightHand) && finite(pose.rightElbow) && finite(pose.leftKnee),
            "Entire windup and follow-through produce finite connected joints");
        Tests::Check(pose.leftFoot.y >= pitcher.moundHeightMeters && pose.rightFoot.y >= pitcher.moundHeightMeters,
            "Animation never moves feet below the mound");
        std::size_t triangles = 0;
        Common::EmitPitcherMesh(pitcher,[&](const auto& a,const auto& b,const auto& c,const float* color) {
            ++triangles;
            Tests::Check(finite(a) && finite(b) && finite(c),"Pitcher mesh vertices remain finite");
            Tests::Check(color[0] >= 0 && color[0] <= 1 && color[3] == 1,"Pitcher shading is opaque and normalized");
        });
        if (frame == 0) priorTriangles = triangles;
        if (frame == 120) releaseTriangles = triangles;
    }
    Tests::Check(priorTriangles > releaseTriangles,"Held ball disappears at release to avoid duplicating the simulated ball");
    pitcher.visible = false;
    std::size_t hiddenTriangles = 0;
    Common::EmitPitcherMesh(pitcher,[&](const auto&,const auto&,const auto&,const float*) { ++hiddenTriangles; });
    Tests::Check(hiddenTriangles == 0,"Hidden pitcher emits no geometry outside duel mode");
    const auto savedGround = Common::GetGroundRuntimeSettings();
    const auto savedPitchDistance = Common::GetPitchDistanceMeters();
    Common::SetGroundRuntimeSettings({145});
    const auto board = Common::BuildFieldScoreboardLayout();
    Tests::Check(std::abs(board.centerZ + 146.0) < 1e-10,"Scoreboard is fixed behind the configured center field radius");
    Common::SetGroundRuntimeSettings({10});
    Common::SetPitchDistanceMeters(30);
    Tests::Check(Common::BuildFieldScoreboardLayout().centerZ < -40,
        "Small field settings still place the board behind the pitcher");
    Common::SetPitchDistanceMeters(savedPitchDistance);
    Common::SetGroundRuntimeSettings(savedGround);
    Common::FieldStatisticsState metrics{};
    metrics.pitchName = L"Fastball";
    metrics.pitchSpeedMetersPerSecond = 40;
    metrics.pitchSpinRpm = 2300;
    const auto waiting = Common::BuildFieldStatisticsLines(metrics);
    Tests::Check(waiting[1].find(L"144.0") != std::wstring::npos,"Scoreboard converts pitch velocity to km/h");
    metrics.hasBattedMetrics = true;
    metrics.battedHasGroundContact = true;
    metrics.battedLaunchSpeedMetersPerSecond = 50;
    metrics.battedLaunchAngleDegrees = 23.4;
    metrics.battedDistanceMeters = 116.7;
    metrics.battedMaxHeightMeters = 28.5;
    const auto landed = Common::BuildFieldStatisticsLines(metrics);
    Tests::Check(landed[2].find(L"180.0") != std::wstring::npos && landed[2].find(L"23.4") != std::wstring::npos,
        "Scoreboard presents launch speed and angle");
    Tests::Check(landed[3].find(L"116.7") != std::wstring::npos && landed[3].find(L"28.5") != std::wstring::npos,
        "Scoreboard presents distance and height");
    Tests::Check(landed.size() == 4, "The field scoreboard presents the seven core metrics in four rows");
    metrics.pitchMetricsVisible = false;
    metrics.pitchName = L"先読み禁止の秘密球種";
    const auto concealed = Common::BuildFieldStatisticsLines(metrics);
    Tests::Check(concealed[0] == L"球種  --" &&
        concealed[1] == L"球速  -- km/h    回転  -- rpm",
        "A concealed upcoming pitch renders placeholders rather than zeroes or secret values");
    Tests::Check(concealed[2].find(L"180.0") == std::wstring::npos &&
        concealed[3].find(L"116.7") == std::wstring::npos,
        "Concealed pitch display cannot leak a supplied previous batted result");
    metrics.pitchMetricsVisible = true;
    metrics.hasBattedMetrics = false;
    const auto newBall = Common::BuildFieldStatisticsLines(metrics);
    Tests::Check(newBall[2] == L"初速  -- km/h    角度  --°" &&
        newBall[3] == L"飛距離  -- m    最高点  -- m",
        "A new ball displays empty launch metrics even when old numeric storage remains populated");
}
