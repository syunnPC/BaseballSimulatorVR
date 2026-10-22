#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/MetricsPanelControls.hpp"
#include "../BaseballSimulatorVR/Common/StrikeZoneCalibrationUi.hpp"
#include "../BaseballSimulatorVR/Common/UiTextAtlas.hpp"

void RunMetricsPanelControlsTests()
{
	using namespace BaseballSimulatorVR;
	Common::UiText::PanelLayout panel;
	panel.widthMeters = .42;
	panel.heightMeters = Common::MetricsPanelHeightMeters;
	panel.paddingMeters = .009;
	panel.cellGapMeters = .003;
	panel.columns = 5;
	panel.rows = 8;
	panel.center = {.3,1.3,-.8};
	panel.rightAxis = {.8,0.,.6};
	panel.upAxis = {0.,1.,0.};
	Common::UiText::PanelLocalRect body;
	std::array<Common::UiText::PanelLocalRect,Common::MetricsControlButtonCount> buttons;
	Tests::Check(Common::BuildMetricsPanelControlLayout(panel,body,buttons) && buttons.size() == 17,
	    "Metrics panel lays out all 17 controls, including four independent strike-zone buttons");
	const auto world = [&](double x, double y)
	{ return panel.center + panel.rightAxis * x + panel.upAxis * y; };
	for (std::size_t i = 0; i < buttons.size(); ++i)
	{
		const auto& button = buttons[i];
		const double x = (button.left + button.right) * .5;
		const double y = (button.top + button.bottom) * .5;
		Tests::Check(button.right - button.left >= .06 && button.top - button.bottom >= .02,
		    "Metrics buttons have readable label width and usable laser target height");
		Tests::Check(Common::FindMetricsPanelControl(panel,world(x,y),buttons) == static_cast<int>(i),
		    "Every rendered metrics button resolves to the identical laser control index");
		Tests::Check(button.top < body.bottom && button.bottom >= -panel.heightMeters * .5 + panel.paddingMeters - 1e-12,
		    "Metrics controls remain inside the panel and clear of displayed status and zone bounds");
		for (std::size_t j = i + 1; j < buttons.size(); ++j)
		{
			const auto& other = buttons[j];
			const bool overlaps = (std::min)(button.right,other.right) > (std::max)(button.left,other.left) &&
			    (std::min)(button.top,other.top) > (std::max)(button.bottom,other.bottom);
			Tests::Check(!overlaps, "Metrics button hit areas never overlap another action");
		}
	}
	for (const int index : {Common::MetricsZoneBottomDownIndex,Common::MetricsZoneBottomUpIndex,
	    Common::MetricsZoneTopDownIndex,Common::MetricsZoneTopUpIndex})
	{
		Tests::Check(index != Common::CalibrationMetricsButtonIndex &&
		    buttons[index].top == buttons[Common::MetricsZoneBottomDownIndex].top,
		    "All four manual controls form an adjacent row separate from automatic calibration");
	}
	const auto& lower = buttons[Common::MetricsZoneBottomDownIndex];
	const auto& higher = buttons[Common::MetricsZoneBottomUpIndex];
	Tests::Check(!Common::FindMetricsPanelControl(panel,
	    world((lower.right + higher.left) * .5,(lower.top + lower.bottom) * .5),buttons),
	    "The gap between lower-zone minus and plus buttons cannot trigger either command");
	Tests::Check(!Common::FindMetricsPanelControl(panel,
	    world((body.left + body.right) * .5,(body.top + body.bottom) * .5),buttons),
	    "Reading displayed zone bounds cannot trigger a manual adjustment");
	const double rowGap = std::clamp(panel.cellGapMeters * .75,.0015,.0065);
	Tests::Check((body.top - body.bottom - rowGap * 5.) / 6. >= .023,
	    "Six metrics rows, including zone bottom, top and total height, keep a readable height");
	panel.rows = 1;
	Tests::Check(!Common::BuildMetricsPanelControlLayout(panel,body,buttons),
	    "A minimized title-only panel exposes no invisible manual control targets");
}
