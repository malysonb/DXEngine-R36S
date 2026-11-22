#pragma once

#include "GameWindow.h"

#include <string>
#include <vector>

class Engine;
class UGC;
class UEditWindow;
class UWindow;

// The handheld's on-screen keyboard for the game's text fields.
//
// The pad is otherwise a pointer and a focus: nothing can type a terminal's
// user name, a new game's name or a save's name. This follows the focused
// UEditWindow and draws a compact keyboard over the interface; a key press
// goes through the engine's own text path (OnWindowKeyChar, or a virtual key
// for backspace, tab and enter), so the edit window's filter, length, undo
// and sounds keep working.
//
// It is off unless Settings.json's Interface.VirtualKeyboard (the launcher's
// VirtualKeyboard) is on. B closes it for the field; SELECT opens it again;
// it opens by itself as a field takes the focus when KeyboardAutoShow is on.
class VirtualKeyboard
{
public:
	static VirtualKeyboard& Get();

	// Once a pad tick while the game's interface holds the keyboard: follows
	// the focused editable window, opens the keyboard after a short delay
	// and closes it when the focus leaves the field.
	void Update(Engine* engine, float timeElapsed);

	// The interface's focal window is gone, or the pad is: the target and
	// the keyboard forgotten, without any key sent.
	void Forget();

	bool IsOpen() const { return open; }
	// A field waits for the keyboard, hidden by B: SELECT opens it.
	bool CanOpen() const { return target && !open; }

	// A pad press while the interface is up. True when the keyboard took it;
	// releaseKey is the key whose release the caller sends (IK_None for a
	// typed character or a local action such as a page change).
	bool Press(Engine* engine, int button, EInputKey& releaseKey);
	void Release(Engine* engine, int button);

	// A held d-pad or backspace, as the interface's own repeat runs.
	bool Repeat(Engine* engine);

	// Opens the keyboard for the waiting target, as SELECT does.
	void Open(Engine* engine);
	// Hides it for this field only, as B does.
	void Hide(Engine* engine);

	// The overlay, drawn after the root's windows: the keyboard while it
	// shows, and the small control strip when the interface is up.
	void Draw(Engine* engine, UGC* gc);

private:
	VirtualKeyboard() = default;

	UEditWindow* FindEditableFocus(Engine* engine);
	void OpenFor(Engine* engine, UEditWindow* edit);
	void SetPage(int newPage);
	void MoveSelection(int button);
	void Activate(Engine* engine, EInputKey& releaseKey);
	void FocusNextEdit(Engine* engine);
	void DrawKeyboard(Engine* engine, UGC* gc);
	void DrawHints(Engine* engine, UGC* gc);
	void DrawFocusHighlight(Engine* engine, UGC* gc);

	UEditWindow* target = nullptr;
	bool open = false;
	bool hiddenByUser = false;
	float openDelay = 0.0f;

	// 0 letters, 1 numbers and symbols.
	int page = 0;
	bool shift = false;
	// The selected key: row and column in the 10 by 4 grid.
	int selRow = 0;
	int selCol = 0;
	// The button whose press began a repeat, for a held d-pad or Y.
	int repeatButton = -1;

	// The last focus logged, so LogUI says it once.
	UWindow* loggedFocus = nullptr;
};
