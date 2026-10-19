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

	for (const auto hand : {L"RHP",L"LHP"})
	{
		const std::wstring expectedHand = std::wstring_view{hand} == L"RHP" ? L"右投げ" : L"左投げ";
		for (const auto variant : {L"Beginner.Softball.",L"Beginner.BigBreak.Softball."})
		{
			const auto label = Common::BuildPitchPresetLabel(std::wstring{L"PitchSettings."} + variant + hand + L".json");
			Tests::Check(label.find(L"初心者") != std::wstring::npos && label.find(L"ソフトボール") != std::wstring::npos &&
			    label.find(expectedHand) != std::wstring::npos,
			    "Both beginner variants identify training level, ball type and throwing hand");
			Tests::Check(label.find(std::wstring_view{variant}.starts_with(L"Beginner.BigBreak") ?
			    L"大きな変化" : L"標準変化") != std::wstring::npos,
			    "Normal and large beginner breaking pitches remain distinguishable");
		}
		for (const auto ball : {L"Baseball",L"Softball"})
		{
			const auto label = Common::BuildPitchPresetLabel(std::wstring{L"PitchSettings.SlowFastball."} + ball + L"." + hand + L".json");
			Tests::Check(label.find(L"低速ストレート") != std::wstring::npos && label.find(expectedHand) != std::wstring::npos &&
			    label.find(std::wstring_view{ball} == L"Baseball" ? L"硬式野球" : L"ソフトボール") != std::wstring::npos,
			    "Slow fastball labels preserve ball type and throwing hand");
		}
		for (const auto difficulty : {L"VeryEasy",L"Easy",L"Normal",L"Hard",L"Expert"})
		{
			const auto label = Common::BuildPitchPresetLabel(std::wstring{L"PitchSettings.Difficulty."} + difficulty + L"." + hand + L".json");
			Tests::Check(label.find(L"難易度") != std::wstring::npos && label.find(expectedHand) != std::wstring::npos,
			    "Every bundled difficulty identifies the throwing hand");
		}
		Tests::Check(Common::BuildPitchPresetLabel(std::wstring{L"PitchSettings.Difficulty.VeryEasy."} + hand + L".json") ==
		    L"難易度 ベリーイージー / " + expectedHand,
		    "The very easy preset displays its Japanese name and throwing hand");
		for (const auto challenge : {L"Challenge1", L"Challenge2"})
		{
			const auto label = Common::BuildPitchPresetLabel(std::wstring{L"PitchSettings."} + challenge + L"." + hand + L".json");
			Tests::Check(label == std::wstring{std::wstring_view{challenge} == L"Challenge1" ? L"チャレンジ1 / " : L"チャレンジ2 / "} + expectedHand,
			    "Challenge presets display distinct Japanese labels with their throwing hand");
		}
	}
	Tests::Check(Common::BuildPitchPresetLabel(L"PitchSettings.Toss.Baseball.json") == L"トス\n硬式野球" &&
	    Common::BuildPitchPresetLabel(L"PitchSettings.Toss.Softball.json") == L"トス\nソフトボール",
	    "Toss labels distinguish both ball types");

	const std::wstring unknown = L"PitchSettings.Custom.Unrecognized." + std::wstring(180,L'W') + L".日本語.\U0001F94E.json";
	Tests::Check(Common::BuildPitchPresetLabel(unknown) == unknown &&
	    Common::BuildPitchPresetLabel(L"PitchSettings.Beginner.Extra.Softball.RHP.json") == L"PitchSettings.Beginner.Extra.Softball.RHP.json" &&
	    Common::BuildPitchPresetLabel(L"PitchSettings.json") == L"PitchSettings.json",
	    "Unknown and malformed names retain their complete filename");
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
