#pragma once

namespace BaseballSimulatorVR::Common
{
	struct UiPointerInput final
	{
		bool visible;
		bool pressed;
		bool risingEdge;
		bool fallingEdge;
	};

	[[nodiscard]] inline UiPointerInput AdvanceUiPointerInput(bool batting,
	    bool mainPanelVisible, bool pointerValid, bool selectPressed, bool& wasPressed) noexcept
	{
		const bool visible = pointerValid && (!batting || mainPanelVisible);
		const bool pressed = pointerValid && selectPressed;
		const UiPointerInput result{visible, pressed,
		    visible && pressed && !wasPressed, visible && !pressed && wasPressed};
		// Hidden pointer samples still track the button to avoid a click on reopening.
		wasPressed = pressed;
		return result;
	}
}
