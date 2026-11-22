#include "Precomp.h"
#include "GamepadInput.h"
#include "AimAssist.h"
#include "Engine.h"
#include "LauncherSettings.h"
#include "OriginalCommandLine.h"
#include "VirtualKeyboard.h"
#include "Utils/Logger.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Packages/Extension/Windows/Text/UButtonWindow.h"
#include <dxwidgets/window/window.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace
{
	// Index = GamepadButton, then the two triggers.
	enum { TriggerLeft = (int)GamepadButton::Count, TriggerRight };

	const EInputKey GameKeys[15 + 2] =
	{
		IK_Joy1,        // A
		IK_Joy2,        // B
		IK_Joy3,        // X
		IK_Joy4,        // Y
		IK_Joy7,        // SELECT
		IK_None,        // GUIDE: the system's own (a handheld's menu), not the game's
		IK_Joy8,        // START
		IK_Joy9,        // L3
		IK_Joy10,       // R3
		IK_Joy5,        // L1
		IK_Joy6,        // R1
		IK_JoyPovUp, IK_JoyPovDown, IK_JoyPovLeft, IK_JoyPovRight,
		IK_Joy11,       // L2
		IK_Joy12,       // R2
	};

	// R36S positions, in SDL order. A/B/X/Y are south/east/west/north.
	const EInputKey HandheldKeys[15 + 2] =
	{
		IK_Space, IK_X, IK_Semicolon, IK_F1,
		IK_None, IK_None, IK_Escape,
		IK_Backspace, IK_LeftBracket,
		IK_Q, IK_E,
		IK_None, IK_None, IK_MouseWheelUp, IK_MouseWheelDown,
		IK_RightMouse, IK_LeftMouse
	};
	const EInputKey ShortcutKeys[15 + 2] =
	{
		IK_F12, IK_Tab, IK_SingleQuote, IK_F2,
		IK_None, IK_None, IK_None,
		IK_None, IK_None,
		IK_GreyPlus, IK_GreySlash,
		IK_F3, IK_F4, IK_F5, IK_F6,
		IK_None, IK_None
	};

	// Triggers are buttons past half travel; the gap stops a trigger resting
	// near the threshold from chattering.
	const float TriggerPress = 0.5f;
	const float TriggerRelease = 0.35f;

	// Menus: a held d-pad or shoulder button repeats like a held key.
	const float RepeatDelay = 0.35f;
	const float RepeatRate = 0.09f;

	// Pointer speed at full deflection, in screen pixels per second, before
	// the CursorSpeed setting. Crosses a 1280-wide window in about 1.3 s.
	const float PointerSpeed = 1000.0f;

	// Game axes in the order they are reported: left stick, right stick,
	// triggers.
	const EInputKey AxisKeys[6] = { IK_JoyX, IK_JoyY, IK_JoyU, IK_JoyV, IK_JoyZ, IK_JoyR };

	// Radial dead zone: the stick's distance from centre is rescaled so the
	// edge of the dead zone is 0 and full travel is 1, keeping its direction.
	// Per-axis dead zones make diagonals snap to the axes.
	void ApplyDeadZone(float& x, float& y, float deadZone, float curve)
	{
		float mag = std::sqrt(x * x + y * y);
		if (mag <= deadZone || mag <= 0.0f)
		{
			x = y = 0.0f;
			return;
		}
		float scaled = std::min((mag - deadZone) / (1.0f - deadZone), 1.0f);
		scaled = std::pow(scaled, curve);
		x = x / mag * scaled;
		y = y / mag * scaled;
	}

	bool IsUIOpen(Engine* engine)
	{
		// Deus Ex's interface holds the keyboard while it is up: every modal
		// grabs it -- menus, the inventory, conversations and keypads.
		return engine->dxRootWindow && engine->dxRootWindow->IsKeyboardGrabbed();
	}

	// The topmost modal as a conversation window (ConWindow for a
	// first-person speech, ConWindowActive for the interactive one with
	// replies). A modal further in -- a message box over the conversation --
	// is not one, and keeps the pad's usual buttons.
	UWindow* ConversationWindow(Engine* engine)
	{
		if (!engine->dxRootWindow)
			return nullptr;
		UWindow* modal = engine->dxRootWindow->TopmostModal();
		if (!modal)
			return nullptr;
		NameString name = UObject::GetUClassName(modal);
		if (name == "ConWindow" || name == "ConWindowActive")
			return modal;
		return nullptr;
	}

	// The conversation's reply buttons, in the tile's order (top to bottom).
	void CollectChoices(UWindow* window, std::vector<UWindow*>& choices)
	{
		for (UWindow* child = window->firstChild(); child; child = child->nextSibling())
		{
			if (child->bIsVisible() && UObject::GetUClassName(child) == "ConChoiceWindow")
				choices.push_back(child);
			CollectChoices(child, choices);
		}
	}

	bool IsChoiceFocused(Engine* engine, const std::vector<UWindow*>& choices)
	{
		for (UWindow* w = engine->dxRootWindow->FocusWindow(); w; w = w->parentOwner())
		{
			for (UWindow* choice : choices)
			{
				if (w == choice)
					return true;
			}
		}
		return false;
	}

	// The d-pad walks the conversation's replies, one at a time and round
	// the ends, as the keyboard's arrows move the focus in the original.
	// A reply list with none focused starts at its first or last one, so
	// Up and Down both reach the buttons as soon as they appear.
	void MoveChoiceFocus(Engine* engine, int step)
	{
		UWindow* conversation = ConversationWindow(engine);
		if (!conversation)
			return;
		std::vector<UWindow*> choices;
		CollectChoices(conversation, choices);
		if (choices.empty())
			return;

		int index = -1;
		UWindow* focus = engine->dxRootWindow->FocusWindow();
		for (int i = 0; i < (int)choices.size() && index < 0; i++)
		{
			for (UWindow* w = focus; w; w = w->parentOwner())
			{
				if (w == choices[i])
				{
					index = i;
					break;
				}
			}
		}
		if (index < 0)
			index = step < 0 ? (int)choices.size() - 1 : 0;
		else
			index = ((index + step) % (int)choices.size() + (int)choices.size()) % (int)choices.size();
		engine->dxRootWindow->SetRootFocusWindow(choices[index]);
	}

	// The keypad's window follows the pointer in the original and seeds no
	// focus, so A had nothing to press until a d-pad move found a button.
	// Seed its first button, and the overlay draws the focus box the
	// button's own textures lack.
	void EnsureKeypadFocus(Engine* engine)
	{
		if (!engine->dxRootWindow)
			return;
		UWindow* modal = engine->dxRootWindow->TopmostModal();
		if (!modal || UObject::GetUClassName(modal) != "HUDKeypadWindow")
			return;
		if (engine->dxRootWindow->FocusWindow())
			return;
		for (UWindow* child = modal->firstChild(); child; child = child->nextSibling())
		{
			if (child->bIsSelectable() && child->IsTraversable(true))
			{
				engine->dxRootWindow->SetRootFocusWindow(child);
				return;
			}
		}
	}

	// The first reply takes the focus as soon as the list appears, as the
	// original's choices light up: B then presses it without the d-pad
	// having to be touched. A pointer click still acts on what it is over.
	void FocusFirstChoice(Engine* engine)
	{
		UWindow* conversation = ConversationWindow(engine);
		if (!conversation)
			return;
		std::vector<UWindow*> choices;
		CollectChoices(conversation, choices);
		if (choices.empty() || IsChoiceFocused(engine, choices))
			return;
		engine->dxRootWindow->SetRootFocusWindow(choices.front());
	}
}

void GamepadInput::Start(PackageManager* packages)
{
	dxcMode = LauncherSettings::Get().Gamepad.DXController;
	if (!dxcMode)
		return;
	LogMessage("Gamepad: the DXController mode");
	dxc.LoadIni(packages);
}

void GamepadInput::Configure()
{
	if (configured)
		return;
	configured = true;
	handheld = std::getenv("DXE_R36S_GAMEPAD") != nullptr;
	// The original's -nojoy: no pad at all, as its client then opens no
	// joystick.
	if (OriginalCommandLine::Get().Param("nojoy"))
	{
		LogMessage("-nojoy: no pad");
		enabled = false;
		DisplayBackend::Get()->SetGamepadKeyEmulation(false);
		return;
	}
	enabled = LauncherSettings::Get().Gamepad.Enabled;
	// The setting the sticks are read with (LauncherSettings::Gamepad):
	// r36s.ini's DeadZone through the launcher's DXE_R36S_DEADZONE, else
	// Settings.json's Gamepad.DeadZone, else 0.2.
	LogMessage("Gamepad: deadzone=" + std::to_string(LauncherSettings::Get().Gamepad.DeadZone));
	// With our own handling on, the backend's button-to-key emulation would
	// deliver every button twice.
	DisplayBackend::Get()->SetGamepadKeyEmulation(!enabled);
}

void GamepadInput::ReadButtons(const GamepadState& state, bool down[NumButtons])
{
	for (int i = 0; i < (int)GamepadButton::Count; i++)
		down[i] = state.Buttons[i];

	const float lt = state.Axes[(int)GamepadAxis::LeftTrigger];
	const float rt = state.Axes[(int)GamepadAxis::RightTrigger];
	down[TriggerLeft] = wasDown[TriggerLeft] ? lt > TriggerRelease : lt > TriggerPress;
	down[TriggerRight] = wasDown[TriggerRight] ? rt > TriggerRelease : rt > TriggerPress;
}

void GamepadInput::Press(Engine* engine, int button, bool ui)
{
	if (!ui)
	{
		EInputKey key = handheld ? HandheldKeys[button] : GameKeys[button];
		if (shortcutActive)
			key = ShortcutKeys[button];
		if (key == IK_None)
			return;
		owner[button] = Owner::Game;
		pressedKey[button] = key;

		// R2 fires: before the binding runs, the view is put on the pawn
		// inside the assist cone and put back as soon as the shot has been
		// traced. Only the shot turns; the camera does not. L2 is the
		// game's right click (interact), not a shot.
		if (key == IK_LeftMouse)
		{
			Rotator saved(0, 0, 0);
			if (AimAssist::Get().BeginShot(engine, saved))
			{
				engine->OnWindowKeyDown(key);
				AimAssist::Get().EndShot(engine, saved);
				return;
			}
		}

		engine->OnWindowKeyDown(key);
		return;
	}

	VirtualKeyboard& keyboard = VirtualKeyboard::Get();
	owner[button] = Owner::UI;
	uiKey[button] = IK_None;
	const Point pos(0.0, 0.0);   // the root window clicks at its own pointer

	// SELECT opens the keyboard on a text field, where it is otherwise
	// hidden; with no field it keeps its usual Escape.
	if (button == (int)GamepadButton::Back && keyboard.CanOpen())
	{
		keyboard.Open(engine);
		return;
	}

	// While the keyboard is up it takes every button: its own A, B, X, Y,
	// d-pad, shoulders, START and SELECT, and nothing behind it.
	if (keyboard.IsOpen())
	{
		owner[button] = Owner::Keyboard;
		EInputKey key = IK_None;
		keyboard.Press(engine, button, key);
		uiKey[button] = key;
		if (button == (int)GamepadButton::DpadUp || button == (int)GamepadButton::DpadDown ||
			button == (int)GamepadButton::DpadLeft || button == (int)GamepadButton::DpadRight ||
			button == (int)GamepadButton::Y)
		{
			repeatButton = button;
			repeatWait = RepeatDelay;
		}
		return;
	}

	// In a conversation both face buttons confirm -- the one the pointer
	// would click (A on SDL's positional layout, the bottom button) and B
	// -- so whichever the handheld labels B presses the reply highlighted
	// or under the pointer, as Enter does: Enter is what a reply's button
	// takes as its press, and a speech with no replies advances on it. The
	// d-pad walks the replies. Y, SELECT and START still leave.
	if (ConversationWindow(engine))
	{
		if (button == (int)GamepadButton::A || button == (int)GamepadButton::B)
		{
			uiKey[button] = IK_Enter;
			engine->OnWindowKeyDown(IK_Enter);
			return;
		}
		if (button == (int)GamepadButton::DpadUp || button == (int)GamepadButton::DpadDown)
		{
			MoveChoiceFocus(engine, button == (int)GamepadButton::DpadUp ? -1 : 1);
			repeatButton = button;
			repeatWait = RepeatDelay;
			return;
		}
		// The tab-group buttons and the scroll modifier have no place in a
		// conversation; keep them from moving its focus.
		if (button == (int)GamepadButton::LeftShoulder || button == (int)GamepadButton::RightShoulder ||
			button == TriggerLeft || button == TriggerRight)
			return;
	}

	// A menu setting's action button refuses Enter so the menu can use it as
	// OK; on the pad A is the natural "next value" there, as a click on it is.
	if (button == (int)GamepadButton::A && engine->dxRootWindow)
	{
		UWindow* focus = engine->dxRootWindow->FocusWindow();
		if (focus && focus->IsA("MenuUIChoiceButton") && focus->parentOwner())
		{
			UWindow* choice = focus->parentOwner();
			if (choice->IsA("MenuUIChoiceSlider") || choice->IsA("MenuUIChoiceEnum"))
			{
				uiKey[button] = IK_Right;
				engine->OnWindowKeyDown(IK_Right);
				return;
			}
		}
	}

	// An inventory item's own button refuses the keyboard (its
	// VirtualKeyPressed returns false, so the mouse click is what selects
	// it); press it here, and the screen's own Enter uses the selection.
	if (button == (int)GamepadButton::A && engine->dxRootWindow)
	{
		UWindow* focus = engine->dxRootWindow->FocusWindow();
		if (focus && focus->IsA("PersonaItemButton"))
		{
			if (UButtonWindow* item = UObject::TryCast<UButtonWindow>(focus))
			{
				item->PressButton(IK_Enter);
				return;
			}
		}
	}

	switch (button)
	{
	// A activates what holds the focus, as Enter does: a menu's button, a
	// list's row, an item in the inventory, a reply in a conversation.
	case (int)GamepadButton::A: uiKey[button] = IK_Enter; engine->OnWindowKeyDown(IK_Enter); break;
	// The pointer stays for what only a mouse can do: R2 clicks with it.
	case TriggerRight: engine->OnWindowMouseDown(pos, IK_LeftMouse); break;
	// L2 turns the d-pad into the wheel for long text that is not a list.
	case TriggerLeft: scrollModifier = true; break;
	// A right click steps a menu value back, as the game's own right click.
	case (int)GamepadButton::X: engine->OnWindowMouseDown(pos, IK_RightMouse); break;
	// L1 and R1 walk the tab groups: the nav bar, a list, a button bar.
	case (int)GamepadButton::LeftShoulder:
		if (engine->dxRootWindow)
			engine->dxRootWindow->MoveTabGroupPrev();
		break;
	case (int)GamepadButton::RightShoulder:
		if (engine->dxRootWindow)
			engine->dxRootWindow->MoveTabGroupNext();
		break;
	// Back out of the menu. SELECT, Y and START also open menus in play, so
	// the button that opened a screen closes it again.
	case (int)GamepadButton::B:
	case (int)GamepadButton::Y:
	case (int)GamepadButton::Back:
	case (int)GamepadButton::Start: uiKey[button] = IK_Escape; engine->OnWindowKeyDown(IK_Escape); break;
	case (int)GamepadButton::DpadUp:
		if (scrollModifier) engine->OnWindowMouseWheel(pos, IK_MouseWheelUp);
		else { uiKey[button] = IK_Up; engine->OnWindowKeyDown(IK_Up); }
		break;
	case (int)GamepadButton::DpadDown:
		if (scrollModifier) engine->OnWindowMouseWheel(pos, IK_MouseWheelDown);
		else { uiKey[button] = IK_Down; engine->OnWindowKeyDown(IK_Down); }
		break;
	case (int)GamepadButton::DpadLeft: uiKey[button] = IK_Left; engine->OnWindowKeyDown(IK_Left); break;
	case (int)GamepadButton::DpadRight: uiKey[button] = IK_Right; engine->OnWindowKeyDown(IK_Right); break;
	default: owner[button] = Owner::None; return;
	}

	switch (button)
	{
	case (int)GamepadButton::DpadUp: case (int)GamepadButton::DpadDown:
	case (int)GamepadButton::DpadLeft: case (int)GamepadButton::DpadRight:
		repeatButton = button;
		repeatWait = RepeatDelay;
		break;
	default:
		break;
	}
}

// A release goes to whichever side saw the press, even if a menu opened or
// closed in between -- otherwise a button held across the switch would stay
// down forever on one side.
void GamepadInput::Release(Engine* engine, int button)
{
	Owner was = owner[button];
	owner[button] = Owner::None;
	if (repeatButton == button)
		repeatButton = -1;

	if (was == Owner::Game)
	{
		engine->OnWindowKeyUp(pressedKey[button]);
		return;
	}
	if (was == Owner::Keyboard)
	{
		if (uiKey[button] != IK_None)
		{
			EInputKey key = uiKey[button];
			uiKey[button] = IK_None;
			engine->OnWindowKeyUp(key);
		}
		VirtualKeyboard::Get().Release(engine, button);
		return;
	}
	if (was != Owner::UI)
		return;

	// The key the press sent, if it sent one: B in a conversation sent
	// Enter, a d-pad that walked the replies sent none.
	if (uiKey[button] != IK_None)
	{
		EInputKey key = uiKey[button];
		uiKey[button] = IK_None;
		engine->OnWindowKeyUp(key);
		return;
	}

	if (button == TriggerLeft)
	{
		scrollModifier = false;
		return;
	}

	const Point pos(0.0, 0.0);
	switch (button)
	{
	case TriggerRight: engine->OnWindowMouseUp(pos, IK_LeftMouse); break;
	case (int)GamepadButton::X: engine->OnWindowMouseUp(pos, IK_RightMouse); break;
	default: break;   // the wheel has no release
	}
}

void GamepadInput::UIRepeat(Engine* engine, float timeElapsed)
{
	if (repeatButton < 0)
		return;
	Owner side = owner[repeatButton];
	if (side != Owner::UI && side != Owner::Keyboard)
		return;
	repeatWait -= timeElapsed;
	if (repeatWait > 0.0f)
		return;
	repeatWait += RepeatRate;
	if (repeatWait < 0.0f)
		repeatWait = RepeatRate;   // after a long frame, one repeat, not a burst

	if (side == Owner::Keyboard)
	{
		if (!VirtualKeyboard::Get().Repeat(engine))
			repeatButton = -1;
		return;
	}

	const Point pos(0.0, 0.0);
	if (ConversationWindow(engine))
	{
		if (repeatButton == (int)GamepadButton::DpadUp || repeatButton == (int)GamepadButton::DpadDown)
		{
			MoveChoiceFocus(engine, repeatButton == (int)GamepadButton::DpadUp ? -1 : 1);
			return;
		}
	}
	switch (repeatButton)
	{
	case (int)GamepadButton::DpadUp:
		if (scrollModifier) engine->OnWindowMouseWheel(pos, IK_MouseWheelUp);
		else engine->OnWindowKeyDown(IK_Up);
		break;
	case (int)GamepadButton::DpadDown:
		if (scrollModifier) engine->OnWindowMouseWheel(pos, IK_MouseWheelDown);
		else engine->OnWindowKeyDown(IK_Down);
		break;
	case (int)GamepadButton::DpadLeft: engine->OnWindowKeyDown(IK_Left); break;
	case (int)GamepadButton::DpadRight: engine->OnWindowKeyDown(IK_Right); break;
	default: break;
	}
}

void GamepadInput::GameAxes(Engine* engine, const GamepadState& state, float timeElapsed)
{
	const auto& settings = LauncherSettings::Get().Gamepad;
	const float deadZone = std::clamp(settings.DeadZone, 0.0f, 0.9f);

	float lx = state.Axes[(int)GamepadAxis::LeftX], ly = state.Axes[(int)GamepadAxis::LeftY];
	float rx = state.Axes[(int)GamepadAxis::RightX], ry = state.Axes[(int)GamepadAxis::RightY];
	// Movement is linear, so half a push walks at half speed. Looking gets a
	// curve: small movements for aiming, full speed at the edge.
	ApplyDeadZone(lx, ly, deadZone, 1.0f);
	ApplyDeadZone(rx, ry, deadZone, 1.6f);
	// A target under the crosshair makes the look linger on it (AimAssist):
	// the same stick movement turns less, so fine corrections are easier.
	const bool looking = rx != 0.0f || ry != 0.0f;
	const float lookScale = AimAssist::Get().LookScale(engine, timeElapsed, looking);
	rx *= settings.LookSensitivityX * lookScale;
	ry *= settings.LookSensitivityY * (settings.InvertY ? -1.0f : 1.0f) * lookScale;

	// SDL reports down as positive; UE1 bindings expect up to be.
	const float values[6] =
	{
		lx, -ly, rx, -ry,
		state.Axes[(int)GamepadAxis::LeftTrigger],
		state.Axes[(int)GamepadAxis::RightTrigger],
	};

	for (int i = 0; i < 6; i++)
	{
		int delta = (int)std::lround(values[i] * 100.0f);
		if (delta != 0)
		{
			// Sent every tick while deflected: the engine holds the last
			// value until the key is released.
			engine->InputEvent(AxisKeys[i], IST_Axis, delta);
			axisActive[i] = true;
		}
		else if (axisActive[i])
		{
			engine->InputEvent(AxisKeys[i], IST_Release);
			axisActive[i] = false;
		}
	}
}

void GamepadInput::ReleaseGameAxes(Engine* engine)
{
	for (int i = 0; i < 6; i++)
	{
		if (axisActive[i])
		{
			engine->InputEvent(AxisKeys[i], IST_Release);
			axisActive[i] = false;
		}
	}
}

void GamepadInput::Pointer(Engine* engine, const GamepadState& state, float timeElapsed)
{
	const auto& settings = LauncherSettings::Get().Gamepad;
	const float deadZone = std::clamp(settings.DeadZone, 0.0f, 0.9f);

	// Either stick moves the pointer; whichever is pushed further wins.
	float lx = state.Axes[(int)GamepadAxis::LeftX], ly = state.Axes[(int)GamepadAxis::LeftY];
	float rx = state.Axes[(int)GamepadAxis::RightX], ry = state.Axes[(int)GamepadAxis::RightY];
	ApplyDeadZone(lx, ly, deadZone, 2.0f);
	ApplyDeadZone(rx, ry, deadZone, 2.0f);
	float x = std::abs(rx) + std::abs(ry) > std::abs(lx) + std::abs(ly) ? rx : lx;
	float y = std::abs(rx) + std::abs(ry) > std::abs(lx) + std::abs(ly) ? ry : ly;
	if (x == 0.0f && y == 0.0f)
	{
		pointerRemX = pointerRemY = 0.0f;
		return;
	}

	float speed = PointerSpeed * std::max(settings.CursorSpeed, 0.05f) * std::min(timeElapsed, 0.1f);
	pointerRemX += x * speed;
	pointerRemY += y * speed;
	int dx = (int)pointerRemX, dy = (int)pointerRemY;
	pointerRemX -= dx;
	pointerRemY -= dy;
	if (dx != 0 || dy != 0)
		engine->OnWindowRawMouseMove(dx, dy);
}

void GamepadInput::Forget(Engine* engine)
{
	if (dxcMode)
	{
		dxc.ReleaseAll(engine);
		return;
	}
	for (int i = 0; i < NumButtons; i++)
	{
		wasDown[i] = false;
		owner[i] = Owner::None;
		uiKey[i] = IK_None;
	}
	repeatButton = -1;
	exitChordDown = false;
	suppressStart = false;
	suppressMenuButton = false;
	menuChordDown = false;
	scrollModifier = false;
	exitWindow = 0.0f;
	VirtualKeyboard::Get().Forget();
	AimAssist::Get().Forget();
	for (bool& active : axisActive)
		active = false;
	pointerRemX = pointerRemY = 0.0f;
}

void GamepadInput::Update(Engine* engine, float timeElapsed)
{
	Configure();
	if (!enabled || !engine->GameWindowActive)
		return;

	if (dxcMode)
	{
		// The tick's time held as the mod's own launcher holds a frame's, 0
		// to 0.2 s
		engine->padAxisScale = std::clamp(timeElapsed, 0.0f, 0.2f) * 120.0f;
		dxc.Update(engine);
		return;
	}

	GamepadState state;
	if (!DisplayBackend::Get()->GetGamepadState(state))
		state = {};   // unplugged: everything reads as released

	const bool ui = IsUIOpen(engine);
	if (ui && !uiLast)
	{
		ReleaseGameAxes(engine);   // do not keep walking under a menu
		AimAssist::Get().Forget();
	}
	uiLast = ui;
	if (ui)
	{
		FocusFirstChoice(engine);   // a reply list's first button takes the focus as it appears
		EnsureKeypadFocus(engine);
	}
	VirtualKeyboard::Get().Update(engine, timeElapsed);

	bool down[NumButtons];
	ReadButtons(state, down);
	if (handheld)
	{
		const int back = (int)GamepadButton::Back;
		const int guide = (int)GamepadButton::Guide;
		const int start = (int)GamepadButton::Start;
		const bool select = down[back];
		const bool fn = down[guide];
		const bool chord = select && down[start];
		const bool shoulders = down[(int)GamepadButton::LeftShoulder] &&
			down[(int)GamepadButton::RightShoulder];
		const int north = (int)GamepadButton::Y;
		const int west = (int)GamepadButton::X;
		if (shoulders && down[north])
		{
			engine->OnWindowClose();
			return;
		}
		if (shoulders && down[west])
			suppressMenuButton = true;
		else if (!down[west])
			suppressMenuButton = false;
		if (suppressMenuButton && !menuChordDown)
		{
			engine->OnWindowKeyDown(IK_Escape);
			engine->OnWindowKeyUp(IK_Escape);
		}
		menuChordDown = suppressMenuButton;
		if (suppressMenuButton)
			down[west] = false;
		exitWindow = std::max(0.0f, exitWindow - timeElapsed);
		if ((fn && down[start]) || (chord && !exitChordDown && exitWindow > 0.0f))
		{
			engine->OnWindowClose();
			return;
		}
		if (chord && !exitChordDown)
			exitWindow = 1.5f;
		exitChordDown = chord;
		shortcutActive = select || fn;
		if (shortcutActive && down[start])
			suppressStart = true;
		if (!down[start])
			suppressStart = false;
		if (suppressStart)
			down[start] = false;
		// The game mode keeps SELECT for its shortcuts and chords. In the
		// interface it is a normal button: it opens a waiting keyboard or
		// backs out, as the port's controls say.
		if (!ui)
			down[back] = false;
		down[guide] = false;
	}

	for (int i = 0; i < NumButtons; i++)
	{
		if (down[i] && !wasDown[i])
			Press(engine, i, ui);
		else if (!down[i] && wasDown[i])
			Release(engine, i);
		wasDown[i] = down[i];
	}

	if (ui)
	{
		UIRepeat(engine, timeElapsed);
		if (!VirtualKeyboard::Get().IsOpen())
			Pointer(engine, state, timeElapsed);
	}
	else
	{
		GameAxes(engine, state, timeElapsed);
	}
}
