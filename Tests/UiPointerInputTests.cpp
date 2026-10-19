#include "TestSupport.hpp"
#include "../BaseballSimulatorVR/Common/UiPointerInput.hpp"

void RunUiPointerInputTests()
{
	using BaseballSimulatorVR::Common::AdvanceUiPointerInput;
	bool wasPressed = false;
	auto input = AdvanceUiPointerInput(true, false, true, true, wasPressed);
	Tests::Check(!input.visible && !input.risingEdge && wasPressed,
	    "Swinging with the batting panel closed hides the pointer and consumes the trigger state");
	input = AdvanceUiPointerInput(true, true, true, true, wasPressed);
	Tests::Check(input.visible && !input.risingEdge,
	    "Opening the opposite-hand panel with the trigger held cannot click a control");
	input = AdvanceUiPointerInput(true, true, true, false, wasPressed);
	Tests::Check(input.visible && input.fallingEdge,
	    "Releasing the trigger after opening the panel ends any existing press");
	input = AdvanceUiPointerInput(true, true, true, true, wasPressed);
	Tests::Check(input.risingEdge && input.pressed,
	    "A fresh trigger press operates the visible batting controls");
	input = AdvanceUiPointerInput(true, false, true, true, wasPressed);
	Tests::Check(!input.visible && !input.risingEdge && !input.fallingEdge,
	    "Closing the main panel disables interaction even if a subordinate panel remains displayed");
	input = AdvanceUiPointerInput(true, false, true, false, wasPressed);
	Tests::Check(!input.visible && !wasPressed,
	    "A release made during batting rearms the next intentional UI press");
	input = AdvanceUiPointerInput(true, true, true, false, wasPressed);
	Tests::Check(input.visible && !input.risingEdge,
	    "Reopening without a trigger press only displays the laser");
	input = AdvanceUiPointerInput(false, false, true, true, wasPressed);
	Tests::Check(input.visible && input.risingEdge,
	    "Pitch viewing retains its pointer and non-main-panel interaction");
	input = AdvanceUiPointerInput(false, true, false, true, wasPressed);
	Tests::Check(!input.visible && !input.pressed && !input.risingEdge && !wasPressed,
	    "Invalid tracking cannot render a laser or retain a pressed pointer");
}
