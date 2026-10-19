#include "../BaseballSimulatorVR/Common/PhaseTrajectoryOverlay.hpp"
#include "../BaseballSimulatorVR/Common/PitcherPresentation.hpp"
#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/GdiAlphaTriangleRasterizer.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace
{
	using namespace BaseballSimulatorVR;
	struct Bounds
	{
		Physics::Vector3 minimum{1e9, 1e9, 1e9}, maximum{-1e9, -1e9, -1e9};
		void Add(const Physics::Vector3& p)
		{
			minimum = {
			    (std::min)(minimum.x, p.x), (std::min)(minimum.y, p.y), (std::min)(minimum.z, p.z)};
			maximum = {
			    (std::max)(maximum.x, p.x), (std::max)(maximum.y, p.y), (std::max)(maximum.z, p.z)};
		}
	};
	void CheckWorldSphere(double radius)
	{
		const Physics::Vector3 center{-.31, 1.0, -18.4};
		Bounds bounds;
		std::size_t triangles = 0;
		Common::EmitBallWorldMesh(center, radius,
		    [&](const auto& a, const auto& b, const auto& c)
		    {
			    ++triangles;
			    for (const auto& point : {a, b, c})
			    {
				    bounds.Add(point);
				    Tests::Check(std::abs(Physics::Length(point - center) - radius) < 1e-13,
				        "Every ball mesh corner is on the configured physical sphere");
			    }
		    });
		Tests::Check(triangles == Common::DebugWorldStyle::BallMeshLatitudeSegments *
		                              Common::DebugWorldStyle::BallMeshLongitudeSegments * 2,
		    "Changing ball size retains the complete sphere tessellation");
		const auto diameter = bounds.maximum - bounds.minimum;
		Tests::Check(std::abs(diameter.x - 2 * radius) < 1e-13 &&
		                 std::abs(diameter.y - 2 * radius) < 1e-13 &&
		                 std::abs(diameter.z - 2 * radius) < 1e-13,
		    "Ball mesh has its configured diameter along every world axis");
	}
	void CheckProjectedPaths(double radius)
	{
		Physics::BallState ball;
		ball.properties.radiusMeters = radius;
		ball.position = {0, 1.6,
		    Common::GetPitchDistanceMeters() +
		        Common::DebugWorldLayout::BatterToPlateDistanceMeters - 4.};
		Common::ViewProjectionState view;
		view.isValid = true;
		view.position = {0, 1.6, 0};
		struct Triangle
		{
			std::array<Common::ScreenPoint, 3> points;
			std::array<float, 4> color;
		};
		std::vector<Triangle> direct, phased, plate, phasedPlate;
		const auto append = [](auto& target)
		{
			return [&target](const auto& a, const auto& b, const auto& c, const float* color)
			{ target.push_back({{a, b, c}, {color[0], color[1], color[2], color[3]}}); };
		};
		Common::EmitProjectedBallAndTrajectoryMeshOverlay(
		    view, 4096, 4096, {}, ball, append(direct));
		Common::EmitProjectedHomePlateOverlay(view, 4096, 4096, append(plate));
		Common::PhaseTrajectoryTrails trails;
		Common::OverlayVisibilitySettings visibility;
		visibility.pitchTrajectoryDisplayMode = Common::PitchTrajectoryDisplayMode::None;
		visibility.showBattedTrajectory = false;
		Common::EmitProjectedPhaseBallAndTrajectoryMeshOverlay(
		    view, 4096, 4096, trails, visibility, ball,
		    [&](const auto& a, const auto& b, const auto& c, const float* color)
		    {
			    const bool isPlate = std::equal(Common::DebugWorldStyle::PlateColor.begin(),
				    Common::DebugWorldStyle::PlateColor.end(), color);
			    append(isPlate ? phasedPlate : phased)(a, b, c, color);
		    }, 1.F, true);
		Tests::Check(!plate.empty() && plate.size() == phasedPlate.size(),
		    "Phase ball rendering retains exactly one independently rendered white home plate");
		Tests::Check(!direct.empty() && direct.size() == phased.size(),
		    "Desktop, XR and software phase rendering share one projected ball sphere");
		for (std::size_t index = 0; index < direct.size(); ++index)
		{
			Tests::Check(direct[index].color == phased[index].color,
			    "Phase ball routing retains sphere shading");
			for (std::size_t corner = 0; corner < 3; ++corner)
			{
				const auto& a = direct[index].points[corner];
				const auto& b = phased[index].points[corner];
				Tests::Check(a.x == b.x && a.y == b.y && a.depth == b.depth,
				    "Phase routing emits one identical sphere with no extra radius or position "
				    "offset");
			}
		}
		std::size_t hidden = 0;
		Common::EmitProjectedBallAndTrajectoryMeshOverlay(
		    view, 4096, 4096, {}, ball, [&](const auto&, const auto&, const auto&, const float*)
		    { ++hidden; }, 1.F, 0.F, false);
		Tests::Check(hidden == 0, "Trajectory-only calls cannot duplicate the current ball mesh");
		Common::ScreenPoint center;
		double radiusPixels{};
		Tests::Check(Common::TryProjectBallMarker(ball, view, 4096, 4096, center, radiusPixels),
		    "Fast projected ball marker accepts the configured sphere");
		const double expected = radius / 4. / (2. * std::tan(.75)) * 4095.;
		Tests::Check(std::abs(radiusPixels - expected) < 1e-10,
		    "Fast marker size derives from physical radius and camera projection");
		std::size_t markers = 0;
		Common::EmitProjectedDebugWorldOverlay(
		    view, 4096, 4096, {}, ball,
		    [&](const auto&, LONG halfSize, const float*)
		    {
			    ++markers;
			    Tests::Check(halfSize == static_cast<LONG>(std::ceil(radiusPixels)),
			        "Actual fast overlay uses the same projected physical ball size");
		    },
		    [](const auto&, const auto&, LONG, const float*) {}, false, false, Common::StrikeZoneDisplayMode::None, true);
		Tests::Check(markers == 1, "Fast overlay emits only one ball marker");
	}
	void CheckSizeRatio()
	{
		Physics::BallState ball;
		ball.position = {0, 1.6,
		    Common::GetPitchDistanceMeters() +
		        Common::DebugWorldLayout::BatterToPlateDistanceMeters - 4.};
		Common::ViewProjectionState view;
		view.isValid = true;
		view.position = {0, 1.6, 0};
		Common::ScreenPoint center;
		ball.properties.radiusMeters = .037;
		double baseball{};
		Tests::Check(Common::TryProjectBallMarker(ball, view, 4096, 4096, center, baseball),
		    "Baseball marker projects");
		ball.properties.radiusMeters = .0485;
		double softball{};
		Tests::Check(Common::TryProjectBallMarker(ball, view, 4096, 4096, center, softball),
		    "Softball marker projects");
		Tests::Check(std::abs(softball / baseball - .097 / .074) < 1e-12,
		    "Projected softball/baseball diameter ratio matches 97mm/74mm");
		const auto sphereWidth = [&](double radius)
		{
			ball.properties.radiusMeters = radius;
			double left = 1e9, right = -1e9;
			Common::EmitProjectedBallAndTrajectoryMeshOverlay(view, 4096, 4096, {}, ball,
			    [&](const auto& a, const auto& b, const auto& c, const float*)
			    {
				    for (const auto& point : {a, b, c})
				    {
					    left = (std::min)(left, point.x);
					    right = (std::max)(right, point.x);
				    }
			    });
			return right - left;
		};
		Tests::Check(std::abs(sphereWidth(.0485) / sphereWidth(.037) - .097 / .074) < .002,
		    "Rendered sphere silhouette follows the physical softball/baseball diameter ratio");
		Tests::Check(Common::LegacyBallMarkerHalfSizePixels(.0485) >
		                 Common::LegacyBallMarkerHalfSizePixels(.037),
		    "Legacy ball markers also respond to physical size");
		Tests::Check(Common::OrthographicBallRadiusPixels(.0485, 10000, 2.5) == 194 &&
		                 Common::OrthographicBallRadiusPixels(.037, 10000, 2.5) == 148,
		    "GDI orthographic ball radii preserve the physical diameter ratio");
		view.orientation={.12,-.08,.03};
		ball.properties.radiusMeters=.0485;
		view.orientationW=std::sqrt(1.-Physics::LengthSquared(view.orientation));
		const auto q=view.orientation;
		const Physics::Vector3 back{2*(q.x*q.z+q.y*view.orientationW),
		    2*(q.y*q.z-q.x*view.orientationW),1-2*(q.x*q.x+q.y*q.y)};
		ball.position=view.position-back*4.;
		ball.position.z+=Common::GetPitchDistanceMeters()+Common::DebugWorldLayout::BatterToPlateDistanceMeters;
		double rotated{};
		Tests::Check(Common::TryProjectBallMarker(ball,view,4096,4096,center,rotated) &&
		    std::abs(rotated-softball)<1e-10,
		    "Head pitch, yaw and roll preserve the physical radius at the same view depth");
	}
	void CheckBatMaterialGeometry()
	{
		constexpr std::array materials{Physics::BatMaterial::Wood,
		    Physics::BatMaterial::Metal, Physics::BatMaterial::Urethane};
		Tests::Check(Common::GetBatMaterialColor(materials[1])[0] > .7F &&
		    Common::GetBatMaterialColor(materials[1])[2] > .7F &&
		    Common::GetBatMaterialColor(materials[2])[0] < .3F,
		    "Metal is silver and urethane is dark gray");
		for (const auto material : materials)
		for (const double physicalLength : {.5, .84, 1.16})
		for (const auto axis : {Physics::NormalizeOrZero(Physics::Vector3{.7, .4, -.2}),
		    Physics::Vector3{0., 1., 0.}})
		{
			const Physics::Vector3 grip{-.4, 1.6,
			    Common::GetPitchDistanceMeters() + Common::DebugWorldLayout::BatterToPlateDistanceMeters - 4.};
			const auto endpoints = Physics::BatEndpointsFromGrip(grip, axis, physicalLength);
			Common::BatOverlayState bat{endpoints.knob, endpoints.tip, .035, material, {0., 1., 0.}};
			const auto worldKnob = Common::ConvertSimulationPositionToWorld(endpoints.knob);
			const auto geometry = Common::BuildBatRenderGeometry(bat);
			Tests::Check(geometry.has_value(), "Every configured bat has a finite radial frame");
			const auto worldGrip = Common::ConvertSimulationPositionToWorld(grip);
			Tests::Check(Physics::Length(worldKnob + axis * (physicalLength * Physics::BatGripCenterRatio) -
			    worldGrip) < 1e-12, "The controller anchor lies at the grip center of the full visible bat");
			const auto& profile = Physics::GetBatMaterialProfile(material);
			const double sweetStart = profile.sweetSpotStartRatio * physicalLength;
			const double sweetEnd = profile.sweetSpotEndRatio * physicalLength;
			double greenMinimum = 1e9, greenMaximum = -1e9;
			double bodyMinimum = 1e9, bodyMaximum = -1e9, signedVolume = 0.;
			std::size_t triangles = 0, greenTriangles = 0, knobCaps = 0;
			using Vertex = std::array<long long, 3>;
			struct EdgeUse { int count{}; int orientation{}; };
			std::map<std::pair<Vertex, Vertex>, EdgeUse> edges;
			std::map<long long, double> silhouetteRadii;
			std::vector<std::array<float, 4>> bodyColors;
			const auto vertex = [&](const Physics::Vector3& p)
			{
				const auto d = p - worldKnob;
				return Vertex{std::llround(d.x * 1e10), std::llround(d.y * 1e10), std::llround(d.z * 1e10)};
			};
			Common::EmitBatWorldMesh(bat,
			    [&](const auto& a, const auto& b, const auto& c, const auto& color, bool sweetSpot)
			    {
				    ++triangles;
				    const auto normal = Physics::Cross(b - a, c - a);
				    const double area = Physics::Length(normal) * .5;
				    Tests::Check(area > 1e-10, "Bat material segmentation emits no degenerate triangles");
				    double minimum = 1e9, maximum = -1e9;
				    for (const auto& point : {a, b, c})
				    {
					    const double along = Physics::Dot(point - worldKnob, axis);
					    minimum = (std::min)(minimum, along);
					    maximum = (std::max)(maximum, along);
				    }
				    Tests::Check(minimum >= -1e-12 && maximum <= physicalLength + 1e-12,
				        "All bat triangles remain within the exact knob-to-tip configured length");
				    if (sweetSpot)
				    {
					    ++greenTriangles;
					    greenMinimum = (std::min)(greenMinimum, minimum);
					    greenMaximum = (std::max)(greenMaximum, maximum);
					    Tests::Check(color == Common::DebugWorldStyle::BatSweetSpotColor &&
					        minimum >= sweetStart - 1e-12 && maximum <= sweetEnd + 1e-12 &&
					        std::abs(Physics::Dot(normal, axis)) < 1e-12,
					        "Green covers only the shared physical sweet region, with no internal caps");
				    }
				    else
				    {
					    bodyMinimum = (std::min)(bodyMinimum, minimum);
					    bodyMaximum = (std::max)(bodyMaximum, maximum);
					    bodyColors.push_back(color);
					    Tests::Check(color[3] == 1.0F, "The entire profiled material body stays opaque beneath green");
					    if (minimum > physicalLength * .55 && maximum < physicalLength * .94)
					    {
						    const bool correctFinish = material == Physics::BatMaterial::Wood ?
						        color[0] > color[1] * 1.3F && color[1] > color[2] * 1.8F :
						        material == Physics::BatMaterial::Metal ? color[0] > .7F && color[2] > .7F :
						        color[0] < .3F && color[2] < .3F;
						    Tests::Check(correctFinish, "The emitted barrel uses brown wood grain, silver metal or dark urethane");
					    }
					    const std::array vertices{vertex(a), vertex(b), vertex(c)};
					    for (std::size_t i = 0; i < vertices.size(); ++i)
					    {
						    const auto& v0 = vertices[i];
						    const auto& v1 = vertices[(i + 1) % vertices.size()];
						    auto& use = edges[v0 < v1 ? std::pair{v0, v1} : std::pair{v1, v0}];
						    ++use.count;
						    use.orientation += v0 < v1 ? 1 : -1;
					    }
					    signedVolume += Physics::Dot(a - worldKnob, Physics::Cross(b - worldKnob, c - worldKnob)) / 6.;
					    if (maximum < 1e-12)
					    {
						    ++knobCaps;
						    Tests::Check(Physics::Dot(normal, axis) < 0., "The knob face has an outward cap normal");
					    }
					    else
					    {
						    const auto center = (a + b + c) / 3. - worldKnob;
						    const auto radial = center - axis * Physics::Dot(center, axis);
						    Tests::Check(Physics::Dot(normal, radial) > 0.,
						        "Knob, taper, barrel and rounded-tip side triangles all face outward");
					    }
				    }
				    for (const auto& point : {a, b, c})
				    {
					    const auto delta = point - worldKnob;
					    const double along = Physics::Dot(delta, axis);
					    const double radial = Physics::Length(delta - axis * along);
					    if (!sweetSpot)
					    {
						    auto& radius = silhouetteRadii[std::llround(along / physicalLength * 1e9)];
						    radius = (std::max)(radius, radial);
					    }
					    const bool knobCenter = !sweetSpot && Physics::Length(delta) < 1e-12;
					    const double expected = Physics::BatRadiusAtFraction(along / physicalLength, bat.radiusMeters) +
					        (sweetSpot ? .001 : 0.);
					    Tests::Check(knobCenter || std::abs(radial - expected) < 1e-11,
					        "Each surface vertex follows the shared physical profile; the green sleeve sits one millimeter above it");
				    }
			    });
			Tests::Check(std::all_of(edges.begin(), edges.end(), [](const auto& edge)
			    { return edge.second.count == 2 && edge.second.orientation == 0; }) && signedVolume > 0. && knobCaps > 0,
			    "The opaque bat is a watertight consistently oriented solid including its knob cap and rounded tip");
			Tests::Check(triangles > greenTriangles && greenTriangles > 0 && triangles < 4000 &&
			    std::abs(bodyMinimum) < 1e-12 && std::abs(bodyMaximum - physicalLength) < 1e-12 &&
			    std::abs(greenMinimum - sweetStart) < 1e-12 &&
			    std::abs(greenMaximum - sweetEnd) < 1e-12,
			    "Profile tessellation retains full physical length and exact green boundaries within the render budget");
			Tests::Check(std::any_of(bodyColors.begin(), bodyColors.end(), [&](const auto& color)
			    { return color != bodyColors.front(); }), "Material finishes include grip and surface detail");
			const auto ringRadius = [&](double fraction)
			{
				const auto ring = silhouetteRadii.find(std::llround(fraction * 1e9));
				Tests::Check(ring != silhouetteRadii.end(), "The rendered silhouette includes every physical profile ring");
				return ring->second;
			};
			Tests::Check(ringRadius(.012) > ringRadius(.1) * 1.7 &&
			    ringRadius(.1) < ringRadius(.75) * .4 &&
			    ringRadius(.4) < ringRadius(.55) && ringRadius(.55) < ringRadius(.66) &&
			    ringRadius(.99) < ringRadius(.98) && ringRadius(1.) < 1e-12,
			    "The silhouette has a flared knob, narrow grip, growing taper, full barrel and rounded tip");
			std::size_t greenLines = 0, lines = 0;
			greenMinimum = 1e9;
			greenMaximum = -1e9;
			Common::EmitBatWorldWireframe(bat, [&](const auto& a, const auto& b, const auto& color)
			    {
				    ++lines;
				    const double t0 = Physics::Dot(a - worldKnob, axis);
				    const double t1 = Physics::Dot(b - worldKnob, axis);
				    const double minimum = (std::min)(t0, t1), maximum = (std::max)(t0, t1);
				    const bool shell = color == Common::DebugWorldStyle::BatSweetSpotColor;
				    for (const auto& point : {a, b})
				    {
					    const auto delta = point - worldKnob;
					    const double along = Physics::Dot(delta, axis);
					    const double radius = Physics::Length(delta - axis * along);
					    Tests::Check(along >= -1e-12 && along <= physicalLength + 1e-12 &&
					        std::abs(radius - Physics::BatRadiusAtFraction(along / physicalLength, bat.radiusMeters) -
					            (shell ? .001 : 0.)) < 1e-11,
					        "Wireframe vertices follow the same full physical profile and sweet sleeve as the solid mesh");
				    }
				    if (color == Common::DebugWorldStyle::BatSweetSpotColor)
				    {
					    ++greenLines;
					    greenMinimum = (std::min)(greenMinimum, minimum);
					    greenMaximum = (std::max)(greenMaximum, maximum);
					    Tests::Check(minimum >= sweetStart - 1e-12 && maximum <= sweetEnd + 1e-12,
					        "Fallback green lines stay within the mesh and physics sweet boundaries");
				    }
				    else
					    Tests::Check(color[3] == 1.0F, "Wireframe preserves the opaque material profile");
			    });
			Tests::Check(lines > greenLines && greenLines > 0 &&
			    std::abs(greenMinimum - sweetStart) < 1e-12 && std::abs(greenMaximum - sweetEnd) < 1e-12,
			    "Solid and wireframe bat paths share the exact full-length sweet boundaries");
		}
		Common::BatOverlayState rollBat{{0., 0., 0.}, {1., 0., 0.}, .035, Physics::BatMaterial::Wood, {0., 1., 0.}};
		const auto first = Common::BuildBatRenderGeometry(rollBat);
		rollBat.upSimulationDirection = {0., 0., 1.};
		const auto rolled = Common::BuildBatRenderGeometry(rollBat);
		Tests::Check(first && rolled && Physics::Dot(first->radialBasisX, Physics::Vector3{0., 1., 0.}) > .999 &&
		    Physics::Dot(rolled->radialBasisX, Physics::Vector3{0., 0., 1.}) > .999 &&
		    std::abs(Physics::Dot(first->radialBasisX, rolled->radialBasisX)) < 1e-12,
		    "Controller roll rotates the material and grip-tape frame around the bat axis");
	}

	void CheckProjectedBatMaterials()
	{
		const Physics::Vector3 handle{-.42, 1.6,
		    Common::GetPitchDistanceMeters() + Common::DebugWorldLayout::BatterToPlateDistanceMeters - 4.};
		const auto endpoints = Physics::BatEndpointsFromGrip(handle, {1., 0., 0.}, .84);
		Common::BatOverlayState bat{endpoints.knob, endpoints.tip, .035, Physics::BatMaterial::Wood, {0., 1., 0.}};
		Common::ViewProjectionState view;
		view.isValid = true;
		for (const double eyeHeight : {1.1, 1.6, 2.1})
		for (const auto material : {Physics::BatMaterial::Wood, Physics::BatMaterial::Metal,
		    Physics::BatMaterial::Urethane})
		{
			bat.material = material;
			view.position = {0, eyeHeight, 0};
			std::size_t expectedBody = 0, expectedGreen = 0;
			Common::EmitBatWorldMesh(bat,
			    [&](const auto& a, const auto& b, const auto& c, const auto&, bool sweetSpot)
			    {
				    const auto normal = Physics::Cross(b - a, c - a);
				    if (Physics::Dot(normal, view.position - (a + b + c) / 3.) > 0.)
					    ++(sweetSpot ? expectedGreen : expectedBody);
			    });
			std::size_t green = 0, body = 0;
			double minimumX = 1e9, maximumX = -1e9;
			Common::EmitProjectedSolidBatOverlay(view, 4096, 4096, bat,
			    [&](const auto& a, const auto& b, const auto& c, const float* color)
			    {
				    for (const auto& point : {a, b, c})
				    {
					    minimumX = (std::min)(minimumX, point.x);
					    maximumX = (std::max)(maximumX, point.x);
				    }
				    const std::array rendered{color[0], color[1], color[2], color[3]};
				    if (rendered == Common::DebugWorldStyle::BatSweetSpotColor) ++green;
				    else
				    {
					    ++body;
					    Tests::Check(color[3] == 1.F && std::all_of(rendered.begin(), rendered.end(),
					        [](float component) { return std::isfinite(component) && component >= 0.F && component <= 1.F; }),
					        "Projected material grain, tape and lighting produce finite opaque colors");
				    }
			    });
			Tests::Check(green > 0 && body > green && maximumX - minimumX > 100.,
			    "Every visible bat face retains translucent green independently of body lighting");
			Tests::Check(body == expectedBody && green == expectedGreen,
			    "Projected solid rendering emits each outward visible body and sleeve face once and culls every back face");
			std::size_t greenLines = 0, bodyLines = 0;
			Common::EmitProjectedBatOverlay(view, 4096, 4096, bat,
			    [&](const auto&, const auto&, LONG, const float* color)
			    {
				    const std::array rendered{color[0], color[1], color[2], color[3]};
				    if (rendered == Common::DebugWorldStyle::BatSweetSpotColor) ++greenLines;
				    else
				    {
					    ++bodyLines;
					    Tests::Check(color[3] == 1.F,
					        "Desktop line rendering retains the current opaque material and grip");
				    }
			    });
			Tests::Check(greenLines > 0 && bodyLines > greenLines,
			    "Projected fallback preserves all colored sweet sections when material changes");
		}
		bat.endSimulationPosition = bat.startSimulationPosition;
		std::size_t invalidTriangles = 0;
		Common::EmitBatWorldMesh(bat, [&](const auto&, const auto&, const auto&, const auto&, bool)
		    { ++invalidTriangles; });
		Tests::Check(invalidTriangles == 0, "A zero-length bat emits no invalid surface geometry");
		bat.endSimulationPosition = bat.startSimulationPosition + Physics::Vector3{1., 0., 0.};
		bat.material = static_cast<Physics::BatMaterial>(999);
		Tests::Check(!Common::BuildBatRenderGeometry(bat), "Invalid materials emit no geometry or profile lookup");
		bat.material = Physics::BatMaterial::Wood;
		for (double radius : {0., -1., std::numeric_limits<double>::quiet_NaN()})
		{
			bat.radiusMeters = radius;
			Tests::Check(!Common::BuildBatRenderGeometry(bat), "Nonpositive or nonfinite radii cannot produce bat geometry");
		}
		bat.radiusMeters = .035;
		bat.upSimulationDirection.x = std::numeric_limits<double>::quiet_NaN();
		Tests::Check(!Common::BuildBatRenderGeometry(bat), "Nonfinite controller orientation cannot produce bat geometry");
	}

	void CheckSoftwareBatShellBlend()
	{
		const HDC dc = CreateCompatibleDC(nullptr);
		Tests::Check(dc != nullptr, "Create software shell compositing target");
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = 16;
		info.bmiHeader.biHeight = -16;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		void* bits{};
		const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		Tests::Check(bitmap != nullptr, "Create software shell pixel buffer");
		const auto previous = SelectObject(dc, bitmap);
		auto* pixels = static_cast<std::uint32_t*>(bits);
		constexpr std::uint32_t BodyPixel = (180U << 16) | (120U << 8) | 60U;
		std::fill_n(pixels, 256, BodyPixel);
		Common::GdiAlphaTriangleRasterizer rasterizer;
		const auto& color = Common::DebugWorldStyle::BatSweetSpotColor;
		const bool rendered = rasterizer.Draw(dc, 16, 16, {{{2.,2.,.5F},{14.,2.,.5F},{2.,14.,.5F}}}, color) &&
		    rasterizer.Draw(dc, 16, 16, {{{14.,2.,.5F},{14.,14.,.5F},{2.,14.,.5F}}}, color);
		GdiFlush();
		bool blendCorrect = true, outsidePreserved = true;
		for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x)
		{
			const auto pixel = pixels[y * 16 + x];
			if (x < 2 || x >= 14 || y < 2 || y >= 14)
			{ outsidePreserved &= (pixel & 0xFFFFFFU) == BodyPixel; continue; }
			for (int channel = 0; channel < 3; ++channel)
			{
				const int shift = 16 - channel * 8;
				const double body = (BodyPixel >> shift) & 255U;
				const double expected = color[channel] * color[3] * 255. + body * (1. - color[3]);
				const double actual = (pixel >> shift) & 255U;
				blendCorrect &= std::abs(actual - expected) <= 2.;
			}
		}
		SelectObject(dc, previous);
		DeleteObject(bitmap);
		DeleteDC(dc);
		Tests::Check(rendered && blendCorrect && outsidePreserved,
		    "Software mirror blends the translucent shell once, preserves material beneath and has no shared-edge seam");
	}

	void CheckHeldBallAndToss()
	{
		Common::PitcherPresentationState state;
		state.visible = true;
		state.underhandToss = true;
		state.releasePositionWorld = {-.31, 1.0, -19.94};
		state.moundHeightMeters = .18;
		state.secondsFromRelease = 0;
		for (bool left : {false, true})
		{
			for (double speed : {.25, .5, 1., 2.})
			{
				state.throwsLeftHanded = left;
				state.armSpeedScale = speed;
				state.secondsFromRelease = 0;
				const auto release = Common::BuildPitcherPose(state);
				Tests::Check(
				    Physics::Length(release.heldBall - state.releasePositionWorld) < 1e-12 &&
				        Physics::Length((left ? release.leftHand : release.rightHand) -
				                        state.releasePositionWorld) < 1e-12,
				    "Either underhand throwing hand reaches the exact release point at every "
				    "tempo");
				Tests::Check(release.scale == 1.0 &&
				                 release.head.y > state.moundHeightMeters + 1.6 &&
				                 release.chest.y > state.moundHeightMeters + 1.3,
				    "A one-meter toss release keeps an adult-sized torso and head");
				Physics::Vector3 previous{};
				bool hasPrevious = false;
				for (int frame = 0; frame <= 360; ++frame)
				{
					state.secondsFromRelease = -1.2 + frame * (1.2 + .6 / speed) / 360.;
					const auto pose = Common::BuildPitcherPose(state);
					Tests::Check(pose.leftFoot.y >= state.moundHeightMeters &&
					                 pose.rightFoot.y >= state.moundHeightMeters,
					    "Underhand toss feet remain grounded throughout the motion");
					Tests::Check(std::isfinite(pose.heldBall.x) && std::isfinite(pose.heldBall.y) &&
					                 std::isfinite(pose.heldBall.z),
					    "Slow underhand motion stays finite");
					if (hasPrevious)
					{
						Tests::Check(Physics::Length(pose.heldBall - previous) < .1,
						    "Underhand toss follows a continuous low-hand swing at slow and fast "
						    "tempos");
					}
					previous = pose.heldBall;
					hasPrevious = true;
				}
				for (double radius : {.037, .0485})
				{
					state.secondsFromRelease = -.1;
					state.heldBallRadiusMeters = radius;
					const auto pose = Common::BuildPitcherPose(state);
					const auto& mesh = Common::GetPitcherWorldMesh(state);
					Bounds bounds;
					constexpr std::size_t HeldBallVertexCount = 9 * 13;
					Tests::Check(mesh.vertices.size() > HeldBallVertexCount,
					    "Held ball is included before release");
					for (std::size_t index = mesh.vertices.size() - HeldBallVertexCount;
					    index < mesh.vertices.size(); ++index)
					{
						const auto& point = mesh.vertices[index];
						bounds.Add(point);
						Tests::Check(
						    std::abs(Physics::Length(point - pose.heldBall) - radius) < 1e-12,
						    "Held ball uses its configured world radius independently of the "
						    "pitcher's body scale");
					}
					const auto diameter = bounds.maximum - bounds.minimum;
					Tests::Check(std::abs(diameter.x - 2 * radius) < 1e-12 &&
					                 std::abs(diameter.y - 2 * radius) < 1e-12 &&
					                 std::abs(diameter.z - 2 * radius) < 1e-12,
					    "Held softball and baseball have their true physical diameters");
				}
			}
		}
		Common::PitcherWorldMeshCache cache;
		state.secondsFromRelease = -.1;
		(void)cache.Get(state);
		state.heldBallRadiusMeters = .037;
		(void)cache.Get(state);
		Tests::Check(
		    cache.BuildCount() == 2, "Changing held ball size invalidates the pitcher mesh cache");
		state.underhandToss = false;
		(void)cache.Get(state);
		Tests::Check(
		    cache.BuildCount() == 3, "Changing delivery style invalidates the pitcher mesh cache");
	}
} // namespace

void RunBallRenderingTests()
{
	CheckWorldSphere(.037);
	CheckWorldSphere(.0485);
	CheckProjectedPaths(.037);
	CheckProjectedPaths(.0485);
	CheckSizeRatio();
	CheckHeldBallAndToss();
	CheckBatMaterialGeometry();
	CheckProjectedBatMaterials();
	CheckSoftwareBatShellBlend();
}
