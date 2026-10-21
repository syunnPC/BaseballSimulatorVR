#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/PitchPresetUi.hpp"
#include "../BaseballSimulatorVR/Common/UiTextAtlas.hpp"

#include <cmath>

void RunPitchPresetUiTests()
{
	using namespace BaseballSimulatorVR;
	namespace Ui = Common::UiText;
	Ui::PanelLayout panel;
	Common::ConfigureExpandedPitchPanel(panel);
	Ui::PanelLocalRect name;
	Tests::Check(Common::BuildPitchPresetNameRect(panel,name),
	    "The production pitch panel reserves a dedicated preset name rectangle");
	const double innerTop = panel.heightMeters * .5 - panel.paddingMeters;
	const double cellHeight = (panel.heightMeters - 2 * panel.paddingMeters -
	    (panel.rows - 1) * panel.cellGapMeters) / panel.rows;
	const double cellWidth = (panel.widthMeters - 2 * panel.paddingMeters -
	    (panel.columns - 1) * panel.cellGapMeters) / panel.columns;
	const auto previous = Ui::BuildPanelCellRect(name.left,innerTop,cellWidth,cellHeight,
	    panel.cellGapMeters,panel.columns,40);
	const auto next = Ui::BuildPanelCellRect(name.left,innerTop,cellWidth,cellHeight,
	    panel.cellGapMeters,panel.columns,42);
	Tests::Check(name.right - name.left > .32 && name.top - name.bottom > .09,
	    "Preset names receive full panel width and two readable rows");
	Tests::Check(name.top < previous.bottom && name.top < next.bottom && cellHeight > .044,
	    "The dedicated label clears existing selector laser targets and preserves control height");
	panel.rows = 1;
	Tests::Check(!Common::BuildPitchPresetNameRect(panel,name),
	    "A minimized panel exposes no preset name outside its title bar");
	Common::ConfigureExpandedPitchPanel(panel);
	Tests::Check(Common::BuildPitchPresetNameRect(panel,name),"Restore production preset layout");

	// Preset names come from each JSON file's displayName (bundled labels are checked in
	// BundledSettingsTests); the UI appends the hand for presets that offer both hands.
	Tests::Check(Common::BuildPitchPresetLabel(L"難易度 標準", false) == L"難易度 標準 / 右投げ" &&
	    Common::BuildPitchPresetLabel(L"難易度 標準", true) == L"難易度 標準 / 左投げ" &&
	    Common::BuildPitchPresetLabel(L"トス\nソフトボール", std::nullopt) == L"トス\nソフトボール",
	    "Preset labels append the throwing hand only for presets offering both hands");

	const std::wstring unknown = L"PitchSettings.Custom.Unrecognized." + std::wstring(180,L'W') + L".日本語.\U0001F94E.json";
	Tests::Check(Common::BuildPitchPresetLabel(unknown) == unknown &&
	    Common::BuildPitchPresetLabel(std::wstring_view{L"初心者 / 標準変化\nソフトボール / 左投げ"}) ==
	        L"初心者 / 標準変化\nソフトボール / 左投げ",
	    "Published labels and files without a displayName are shown unchanged");
	Ui::GlyphAtlas atlas;
	Ui::TextLayoutCache layouts;
	const auto wrapped = Common::WrapPitchPresetLabel(unknown,900.,[&](std::uint32_t scalar)
	{ return atlas.Metrics().Get(scalar,Ui::GlyphAtlas::FontPixelHeight).advance; });
	std::wstring recovered = wrapped;
	recovered.erase(std::remove(recovered.begin(),recovered.end(),L'\n'),recovered.end());
	Tests::Check(recovered == unknown && wrapped.find(L'\n') != std::wstring::npos,
	    "Measured wrapping retains every character of long custom filenames");
	auto wrappedScalars = Ui::DecodeUtf16(wrapped);
	wrappedScalars.erase(std::remove(wrappedScalars.begin(),wrappedScalars.end(),L'\n'),wrappedScalars.end());
	Tests::Check(wrappedScalars == Ui::DecodeUtf16(unknown),
	    "Wrapping preserves supplementary Unicode characters without splitting surrogate pairs");
	const auto& textLayout = layouts.Get(wrapped,atlas);
	const auto scalars = Ui::DecodeUtf16(unknown);
	Tests::Check(textLayout.glyphs.size() == scalars.size(),
	    "The production atlas receives every glyph of the complete preset label");
	panel.center = {0.,0.,-1.};
	Common::ViewProjectionState view;
	view.isValid = true;
	std::vector<Ui::AtlasVertex> vertices;
	Ui::AtlasDrawContext context{atlas,layouts,vertices};
	Ui::AppendProjectedPanelText(panel,name,wrapped,.94,.88,.5,.5,{1.F,1.F,1.F,1.F},view,1024,768,context);
	Tests::Check(!vertices.empty(),"The long wrapped filename emits actual production atlas geometry");
	Common::ScreenPoint topLeft, bottomRight;
	const double depth = .024 * (Physics::Dot(view.position - panel.center,panel.forwardAxis) >= 0. ? 1. : -1.);
	Tests::Check(Common::TryProjectWorldToScreen(panel.center + panel.rightAxis * name.left +
	    panel.upAxis * name.top + panel.forwardAxis * depth,view,1024,768,&topLeft,false) &&
	    Common::TryProjectWorldToScreen(panel.center + panel.rightAxis * name.right +
	    panel.upAxis * name.bottom + panel.forwardAxis * depth,view,1024,768,&bottomRight,false),
	    "Project the shared production preset name rectangle");
	for (const auto& vertex : vertices)
	{
		Tests::Check(std::isfinite(vertex.position[0]) && std::isfinite(vertex.position[1]),
		    "Long preset names produce finite fitted text geometry");
		const double x = (vertex.position[0] / vertex.position[3] + 1.) * .5 * 1023;
		const double y = (1. - vertex.position[1] / vertex.position[3]) * .5 * 767;
		Tests::Check(x >= topLeft.x - 1. && x <= bottomRight.x + 1. &&
		    y >= topLeft.y - 1. && y <= bottomRight.y + 1.,
		    "Every rendered glyph of a long filename stays inside the dedicated preset area");
	}
}
