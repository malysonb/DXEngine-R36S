#include "Precomp.h"
#include "VirtualKeyboard.h"
#include "Engine.h"
#include "LauncherSettings.h"
#include "Packages/Extension/Windows/UGC.h"
#include "Packages/Extension/Windows/UWindow.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Packages/Extension/Windows/Text/UEditWindow.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/Resources/UFont.h"
#include "Utils/Logger.h"
#include <dxwidgets/window/window.h>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	// The keyboard's grid: ten columns and four rows, the last one the
	// commands. A null label is an empty cell, skipped by the d-pad.
	constexpr int GridColumns = 10;
	constexpr int GridRows = 4;

	using Command = unsigned char;
	constexpr Command CmdNone = 0;
	constexpr Command CmdBackspace = 1;
	constexpr Command CmdEnter = 2;
	constexpr Command CmdTab = 3;
	constexpr Command CmdSpace = 4;
	constexpr Command CmdPage = 5;
	constexpr Command CmdShift = 6;
	constexpr Command CmdClose = 7;

	struct KeyDef
	{
		const char* label;
		char text;
		Command command;
	};

	constexpr KeyDef K(const char* label, char text = 0, Command command = CmdNone)
	{
		return KeyDef{ label, text, command };
	}

	constexpr KeyDef Blank = K(nullptr);

	// Page 0: letters, shown upper case while shift is on. Page 1: digits and
	// the punctuation the game's fields ask for (IP addresses included).
	constexpr KeyDef Letters[GridRows][GridColumns] =
	{
		{ K("q", 'q'), K("w", 'w'), K("e", 'e'), K("r", 'r'), K("t", 't'), K("y", 'y'), K("u", 'u'), K("i", 'i'), K("o", 'o'), K("p", 'p') },
		{ K("a", 'a'), K("s", 's'), K("d", 'd'), K("f", 'f'), K("g", 'g'), K("h", 'h'), K("j", 'j'), K("k", 'k'), K("l", 'l'), K("Bsp", 0, CmdBackspace) },
		{ K("z", 'z'), K("x", 'x'), K("c", 'c'), K("v", 'v'), K("b", 'b'), K("n", 'n'), K("m", 'm'), K(",", ','), K(".", '.'), K("/", '/') },
		{ K("123", 0, CmdPage), K("Shift", 0, CmdShift), K("Space", 0, CmdSpace), K("Tab", 0, CmdTab), K("Enter", 0, CmdEnter), K("Close", 0, CmdClose), Blank, Blank, Blank, Blank },
	};

	constexpr KeyDef Numbers[GridRows][GridColumns] =
	{
		{ K("1", '1'), K("2", '2'), K("3", '3'), K("4", '4'), K("5", '5'), K("6", '6'), K("7", '7'), K("8", '8'), K("9", '9'), K("0", '0') },
		{ K("-", '-'), K("_", '_'), K("=", '='), K("+", '+'), K("[", '['), K("]", ']'), K("{", '{'), K("}", '}'), K("\\", '\\'), K("|", '|') },
		{ K(";", ';'), K(":", ':'), K("'", '\''), K("\"", '"'), K(",", ','), K(".", '.'), K("/", '/'), K("?", '?'), K("!", '!'), K("@", '@') },
		{ K("abc", 0, CmdPage), K("Shift", 0, CmdShift), K("Space", 0, CmdSpace), K("Tab", 0, CmdTab), K("Enter", 0, CmdEnter), K("Close", 0, CmdClose), Blank, Blank, Blank, Blank },
	};

	const KeyDef& KeyAt(int page, int row, int col)
	{
		if (row < 0 || row >= GridRows || col < 0 || col >= GridColumns)
			return Blank;
		return page == 0 ? Letters[row][col] : Numbers[row][col];
	}

	// The row nearest the selected column that has a key, so up and down
	// never strand the selection in an empty cell.
	bool FindInRow(int page, int row, int nearCol, int direction, int& outCol)
	{
		for (int step = 0; step < GridColumns; step++)
		{
			int col = nearCol + step * direction;
			if (col < 0 || col >= GridColumns)
				continue;
			if (KeyAt(page, row, col).label)
			{
				outCol = col;
				return true;
			}
		}
		return false;
	}

	std::string ClassName(UWindow* window)
	{
		return window ? UObject::GetUClassName(window).ToString() : std::string();
	}
}

VirtualKeyboard& VirtualKeyboard::Get()
{
	static VirtualKeyboard keyboard;
	return keyboard;
}

UEditWindow* VirtualKeyboard::FindEditableFocus(Engine* engine)
{
	if (!engine || !engine->dxRootWindow || !engine->dxRootWindow->IsKeyboardGrabbed())
		return nullptr;
	UWindow* focus = engine->dxRootWindow->FocusWindow();
	for (UWindow* window = focus; window; window = window->parentOwner())
	{
		UEditWindow* edit = UObject::TryCast<UEditWindow>(window);
		if (edit && edit->bEditable() && edit->IsShown())
			return edit;
	}
	return nullptr;
}

void VirtualKeyboard::OpenFor(Engine* engine, UEditWindow* edit)
{
	if (!edit)
		return;
	target = edit;
	open = true;
	hiddenByUser = false;
	openDelay = 0.0f;
	page = 0;
	shift = false;
	selRow = 0;
	selCol = 0;
	if (const KeyDef& first = KeyAt(page, selRow, selCol); !first.label)
	{
		int col = 0;
		if (FindInRow(page, 0, 0, 1, col))
			selCol = col;
	}
	LogMessage("R36S keyboard: opened for " + ClassName(edit));
}

void VirtualKeyboard::Update(Engine* engine, float timeElapsed)
{
	const auto& interface = LauncherSettings::Get().Interface;
	if (interface.LogUI && engine && engine->dxRootWindow && engine->dxRootWindow->IsKeyboardGrabbed())
	{
		UWindow* focusWindow = engine->dxRootWindow->FocusWindow();
		if (focusWindow != loggedFocus)
		{
			loggedFocus = focusWindow;
			if (!focusWindow)
			{
				LogMessage("R36S UI: focus=None");
			}
			else
			{
				float x = 0.0f, y = 0.0f;
				focusWindow->ConvertCoordinates(focusWindow, 0.0f, 0.0f, engine->dxRootWindow, x, y);
				LogMessage("R36S UI: focus=" + ClassName(focusWindow)
					+ " pos=" + std::to_string((int)x) + "," + std::to_string((int)y)
					+ " size=" + std::to_string((int)focusWindow->Width()) + "x" + std::to_string((int)focusWindow->Height()));
			}
		}
	}
	if (!interface.VirtualKeyboard)
	{
		if (open)
			LogMessage("R36S keyboard: closed");
		open = false;
		target = nullptr;
		hiddenByUser = false;
		openDelay = 0.0f;
		return;
	}

	UEditWindow* focus = FindEditableFocus(engine);
	if (target && target->Destroyed)
	{
		if (open)
			LogMessage("R36S keyboard: closed");
		open = false;
		target = nullptr;
		hiddenByUser = false;
		openDelay = 0.0f;
	}
	if (focus != target)
	{
		if (open)
			LogMessage("R36S keyboard: closed");
		open = false;
		target = focus;
		hiddenByUser = false;
		openDelay = 0.0f;
	}

	if (!target)
	{
		open = false;
		openDelay = 0.0f;
		return;
	}

	if (open)
	{
		if (!target->bEditable() || !target->IsShown())
		{
			LogMessage("R36S keyboard: closed");
			open = false;
			target = nullptr;
			hiddenByUser = false;
		}
		return;
	}

	if (hiddenByUser || !interface.KeyboardAutoShow)
		return;

	openDelay += std::max(timeElapsed, 0.0f);
	if (openDelay >= 0.18f)
		OpenFor(engine, target);
}

void VirtualKeyboard::Forget()
{
	target = nullptr;
	open = false;
	hiddenByUser = false;
	openDelay = 0.0f;
	repeatButton = -1;
	loggedFocus = nullptr;
}

void VirtualKeyboard::Open(Engine* engine)
{
	if (!target)
		target = FindEditableFocus(engine);
	if (target)
		OpenFor(engine, target);
}

void VirtualKeyboard::Hide(Engine* engine)
{
	(void)engine;
	if (open)
		LogMessage("R36S keyboard: closed");
	open = false;
	hiddenByUser = true;
	repeatButton = -1;
}

void VirtualKeyboard::SetPage(int newPage)
{
	page = newPage == 0 ? 0 : 1;
	if (const KeyDef& key = KeyAt(page, selRow, selCol); !key.label)
	{
		int col = 0;
		if (FindInRow(page, selRow, selCol, 1, col) || FindInRow(page, selRow, selCol, -1, col))
			selCol = col;
	}
}

void VirtualKeyboard::MoveSelection(int button)
{
	int row = selRow;
	int col = selCol;

	if (button == (int)GamepadButton::DpadLeft || button == (int)GamepadButton::DpadRight)
	{
		int direction = button == (int)GamepadButton::DpadLeft ? -1 : 1;
		for (int step = 1; step <= GridColumns; step++)
		{
			int next = (col + direction * step + GridColumns * 2) % GridColumns;
			if (KeyAt(page, row, next).label)
			{
				selCol = next;
				return;
			}
		}
		return;
	}

	if (button == (int)GamepadButton::DpadUp)
		row = (selRow + GridRows - 1) % GridRows;
	else if (button == (int)GamepadButton::DpadDown)
		row = (selRow + 1) % GridRows;
	else
		return;

	if (KeyAt(page, row, selCol).label)
	{
		selRow = row;
		return;
	}
	int next = 0;
	if (FindInRow(page, row, selCol, -1, next) || FindInRow(page, row, selCol, 1, next))
	{
		selRow = row;
		selCol = next;
	}
}

void VirtualKeyboard::Activate(Engine* engine, EInputKey& releaseKey)
{
	const KeyDef& key = KeyAt(page, selRow, selCol);
	if (!key.label)
		return;

	switch (key.command)
	{
	case CmdBackspace:
		engine->OnWindowKeyDown(IK_Backspace);
		releaseKey = IK_Backspace;
		return;
	case CmdEnter:
		engine->OnWindowKeyDown(IK_Enter);
		releaseKey = IK_Enter;
		return;
	case CmdTab:
		FocusNextEdit(engine);
		return;
	case CmdSpace:
		engine->OnWindowKeyChar(" ");
		return;
	case CmdPage:
		SetPage(page == 0 ? 1 : 0);
		return;
	case CmdShift:
		shift = !shift;
		return;
	case CmdClose:
		Hide(engine);
		return;
	default:
		break;
	}

	char c = key.text;
	if (page == 0 && shift && c >= 'a' && c <= 'z')
		c = (char)std::toupper((unsigned char)c);
	if (c)
		engine->OnWindowKeyChar(std::string(1, c));
}

bool VirtualKeyboard::Press(Engine* engine, int button, EInputKey& releaseKey)
{
	releaseKey = IK_None;
	if (!open)
		return false;

	repeatButton = -1;

	if (button == (int)GamepadButton::A)
	{
		Activate(engine, releaseKey);
		return true;
	}
	if (button == (int)GamepadButton::B || button == (int)GamepadButton::Back)
	{
		Hide(engine);
		return true;
	}
	if (button == (int)GamepadButton::X)
	{
		shift = !shift;
		return true;
	}
	if (button == (int)GamepadButton::Y)
	{
		engine->OnWindowKeyDown(IK_Backspace);
		releaseKey = IK_Backspace;
		repeatButton = button;
		return true;
	}
	if (button == (int)GamepadButton::Start)
	{
		engine->OnWindowKeyDown(IK_Enter);
		releaseKey = IK_Enter;
		return true;
	}
	if (button == (int)GamepadButton::DpadUp || button == (int)GamepadButton::DpadDown ||
		button == (int)GamepadButton::DpadLeft || button == (int)GamepadButton::DpadRight)
	{
		MoveSelection(button);
		repeatButton = button;
		return true;
	}
	if (button == (int)GamepadButton::LeftShoulder)
	{
		SetPage(0);
		return true;
	}
	if (button == (int)GamepadButton::RightShoulder)
	{
		SetPage(1);
		return true;
	}

	// Everything else is swallowed while the keyboard is up: a stray R2 or
	// trigger must not click the screen behind it.
	return true;
}

void VirtualKeyboard::Release(Engine* engine, int button)
{
	(void)engine;
	(void)button;
	repeatButton = -1;
}

bool VirtualKeyboard::Repeat(Engine* engine)
{
	if (!open || repeatButton < 0)
		return false;
	if (repeatButton == (int)GamepadButton::Y)
	{
		engine->OnWindowKeyDown(IK_Backspace);
		return true;
	}
	if (repeatButton == (int)GamepadButton::DpadUp || repeatButton == (int)GamepadButton::DpadDown ||
		repeatButton == (int)GamepadButton::DpadLeft || repeatButton == (int)GamepadButton::DpadRight)
	{
		MoveSelection(repeatButton);
		return true;
	}
	return false;
}

void VirtualKeyboard::FocusNextEdit(Engine* engine)
{
	if (!engine || !engine->dxRootWindow)
		return;
	URootWindow* root = engine->dxRootWindow;
	UWindow* modal = root->TopmostModal();
	if (!modal)
		modal = root;

	std::vector<std::pair<float, float>> places;
	std::vector<UEditWindow*> edits;
	std::vector<UWindow*> stack = { modal };
	while (!stack.empty())
	{
		UWindow* window = stack.back();
		stack.pop_back();
		if (!window->bIsVisible())
			continue;
		for (UWindow* child = window->firstChild(); child; child = child->nextSibling())
		{
			UEditWindow* edit = UObject::TryCast<UEditWindow>(child);
			if (edit && edit->bEditable() && edit->IsShown())
			{
				float x = 0.0f, y = 0.0f;
				edit->ConvertCoordinates(edit, 0.0f, 0.0f, root, x, y);
				edits.push_back(edit);
				places.push_back({ y, x });
			}
			stack.push_back(child);
		}
	}
	if (edits.size() < 2)
		return;

	std::vector<size_t> order(edits.size());
	for (size_t i = 0; i < order.size(); i++)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
	{
		if (places[a].first != places[b].first)
			return places[a].first < places[b].first;
		return places[a].second < places[b].second;
	});

	size_t current = 0;
	for (size_t i = 0; i < order.size(); i++)
	{
		if (edits[order[i]] == target)
		{
			current = i;
			break;
		}
	}
	UEditWindow* next = edits[order[(current + 1) % order.size()]];
	if (!next || next == target)
		return;
	if (root->SetRootFocusWindow(next))
	{
		target = next;
		hiddenByUser = false;
		openDelay = 0.0f;
	}
}

void VirtualKeyboard::DrawHints(Engine* engine, UGC* gc)
{
	const auto& interface = LauncherSettings::Get().Interface;
	if (!interface.ShowButtonHints || !gc || !engine || !engine->dxRootWindow)
		return;
	if (!target || open || !engine->dxRootWindow->IsKeyboardGrabbed())
		return;
	if (!hiddenByUser && interface.KeyboardAutoShow)
		return;

	const std::string hint = "SELECT: teclado";
	const float screenWidth = UWindow::GetExtendedVirtualWidth();
	const float screenHeight = UWindow::GetVirtualHeight();
	const float width = 150.0f;
	const float height = 13.0f;
	const float x = (screenWidth - width) * 0.5f;
	float targetX = 0.0f, targetY = 0.0f;
	if (target && engine->dxRootWindow)
		target->ConvertCoordinates(target, 0.0f, 0.0f, engine->dxRootWindow, targetX, targetY);
	(void)targetX;
	bool targetLow = target && (targetY + target->Height() * 0.5f) > screenHeight * 0.5f;
	const float y = targetLow ? 2.0f : screenHeight - height - 2.0f;

	UGC::State saved = gc->SaveState();
	gc->ResetClip(gc->ScaleRect(Rectf::xywh(0.0f, 0.0f, screenWidth, screenHeight)));
	gc->offsetX = 0.0f;
	gc->offsetY = 0.0f;
	gc->EnableDrawing(true);

	UTexture* fill = engine->DefaultTexture;
	if (fill)
	{
		gc->SetStyle(EDrawStyle::Translucent);
		gc->SetTileColor(Color{ 0, 0, 0, 170 });
		gc->DrawStretchedTexture(x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f, fill);
	}
	UFont* font = target ? target->normalFont() : nullptr;
	if (!font && engine->canvas)
		font = engine->canvas->SmallFont();
	gc->SetStyle(EDrawStyle::Normal);
	gc->SetFonts(font, font);
	gc->SetTextColor(Color{ 225, 225, 232, 255 });
	gc->SetAlignments((uint8_t)EHAlign::Center, (uint8_t)EVAlign::Center);
	gc->textScale = 1.0f;
	gc->DrawText(x, y, width, height, hint);

	gc->RestoreState(saved);
}

void VirtualKeyboard::Draw(Engine* engine, UGC* gc)
{
	if (!engine || !gc)
		return;
	if (target && target->Destroyed)
	{
		open = false;
		target = nullptr;
		hiddenByUser = false;
	}
	DrawFocusHighlight(engine, gc);
	if (!open)
	{
		DrawHints(engine, gc);
		return;
	}
	DrawKeyboard(engine, gc);
}

// The keypad's buttons draw no focus mark of their own (HUDKeypadButton only
// has its normal and pressed textures), and its window seeds no focus -- it
// follows the pointer in the original. A box around the focus window lets
// the d-pad and A work there without guessing where the focus sits.
void VirtualKeyboard::DrawFocusHighlight(Engine* engine, UGC* gc)
{
	if (!engine || !engine->dxRootWindow || !gc)
		return;
	UWindow* modal = engine->dxRootWindow->TopmostModal();
	if (!modal || UObject::GetUClassName(modal) != "HUDKeypadWindow")
		return;
	UWindow* focus = engine->dxRootWindow->FocusWindow();
	if (!focus || !focus->IsShown())
		return;
	UTexture* fill = engine->DefaultTexture ? engine->DefaultTexture : gc->DefaultUnderlineTexture;
	if (!fill)
		return;
	float x = 0.0f, y = 0.0f;
	focus->ConvertCoordinates(focus, 0.0f, 0.0f, engine->dxRootWindow, x, y);
	UGC::State saved = gc->SaveState();
	gc->ResetClip(gc->ScaleRect(Rectf::xywh(0.0f, 0.0f, UWindow::GetExtendedVirtualWidth(), UWindow::GetVirtualHeight())));
	gc->offsetX = 0.0f;
	gc->offsetY = 0.0f;
	gc->EnableDrawing(true);
	gc->SetStyle(EDrawStyle::Normal);
	gc->SetTileColor(Color{ 255, 214, 90, 255 });
	gc->DrawBox(x - 1.0f, y - 1.0f, focus->Width() + 2.0f, focus->Height() + 2.0f, 0.0f, 0.0f, 2.0f, fill);
	gc->RestoreState(saved);
}

void VirtualKeyboard::DrawKeyboard(Engine* engine, UGC* gc)
{
	const float screenWidth = UWindow::GetExtendedVirtualWidth();
	const float screenHeight = UWindow::GetVirtualHeight();

	const float keyWidth = 52.0f;
	const float keyHeight = 29.0f;
	const float gap = 4.0f;
	const float pad = 9.0f;
	const float gridWidth = GridColumns * keyWidth + (GridColumns - 1) * gap;
	const float gridHeight = GridRows * keyHeight + (GridRows - 1) * gap;
	const float hintHeight = 15.0f;
	const float panelWidth = gridWidth + pad * 2.0f;
	const float panelHeight = gridHeight + hintHeight + pad * 2.0f + 4.0f;
	const float panelX = std::max(2.0f, (screenWidth - panelWidth) * 0.5f);

	float targetX = 0.0f, targetY = 0.0f;
	if (target && engine->dxRootWindow)
		target->ConvertCoordinates(target, 0.0f, 0.0f, engine->dxRootWindow, targetX, targetY);
	(void)targetX;
	bool targetLow = target ? (targetY + target->Height() * 0.5f) > screenHeight * 0.5f : false;
	float panelY = targetLow ? 4.0f : std::max(4.0f, screenHeight - panelHeight - 4.0f);

	UGC::State saved = gc->SaveState();
	gc->ResetClip(gc->ScaleRect(Rectf::xywh(0.0f, 0.0f, screenWidth, screenHeight)));
	gc->offsetX = 0.0f;
	gc->offsetY = 0.0f;
	gc->EnableDrawing(true);

	UTexture* fill = engine->DefaultTexture;
	if (!fill)
		fill = gc->DefaultUnderlineTexture;

	if (fill)
	{
		gc->SetStyle(EDrawStyle::Translucent);
		gc->SetTileColor(Color{ 0, 0, 0, 205 });
		gc->DrawStretchedTexture(panelX, panelY, panelWidth, panelHeight, 0.0f, 0.0f, 1.0f, 1.0f, fill);
	}

	UFont* font = target ? target->normalFont() : nullptr;
	if (!font && engine->canvas)
		font = engine->canvas->SmallFont();
	gc->SetFonts(font, font);

	const float gridX = panelX + pad;
	const float gridY = panelY + pad;

	for (int row = 0; row < GridRows; row++)
	{
		for (int col = 0; col < GridColumns; col++)
		{
			const KeyDef& key = KeyAt(page, row, col);
			if (!key.label)
				continue;
			float x = gridX + col * (keyWidth + gap);
			float y = gridY + row * (keyHeight + gap);
			bool selected = row == selRow && col == selCol;
			if (fill)
			{
				gc->SetStyle(EDrawStyle::Translucent);
				gc->SetTileColor(selected ? Color{ 62, 104, 184, 245 } : Color{ 38, 38, 46, 235 });
				gc->DrawStretchedTexture(x, y, keyWidth, keyHeight, 0.0f, 0.0f, 1.0f, 1.0f, fill);
				if (selected)
				{
					gc->SetStyle(EDrawStyle::Normal);
					gc->SetTileColor(Color{ 190, 214, 255, 255 });
					gc->DrawBox(x, y, keyWidth, keyHeight, 0.0f, 0.0f, 1.0f, fill);
				}
			}
			gc->SetStyle(EDrawStyle::Normal);
			gc->SetTextColor(selected ? Color{ 255, 255, 255, 255 } : Color{ 225, 225, 232, 255 });
			gc->SetAlignments((uint8_t)EHAlign::Center, (uint8_t)EVAlign::Center);
			gc->textScale = 1.25f;
			std::string label = key.label ? key.label : "";
			if (page == 0 && shift && label.size() == 1 && label[0] >= 'a' && label[0] <= 'z')
				label[0] = (char)std::toupper((unsigned char)label[0]);
			gc->DrawText(x, y, keyWidth, keyHeight, label);
		}
	}

	gc->SetStyle(EDrawStyle::Normal);
	gc->SetTextColor(Color{ 205, 205, 214, 255 });
	gc->SetAlignments((uint8_t)EHAlign::Center, (uint8_t)EVAlign::Center);
	gc->textScale = 1.0f;
	gc->DrawText(gridX, panelY + panelHeight - hintHeight, gridWidth, hintHeight,
		"A digitar  B fechar  X shift  Y apagar  L1/R1 pagina  START enter");

	gc->RestoreState(saved);
}
