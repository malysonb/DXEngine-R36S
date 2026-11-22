
#include "Precomp.h"
#include "LauncherSettings.h"
#include "Utils/JsonValue.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include <algorithm>
#include <climits>
#include <cstdlib>

LauncherSettings& LauncherSettings::Get()
{
	static LauncherSettings settings;
	return settings;
}

static std::string GetSettingsFilename()
{
	return (Directory::engineUserDir() / "Settings.json").string();
}

// A key of the wrong type, a string where true or false or a number goes
// ("UseVSync": "true"), keeps its default and is said, as a key left out
// keeps its default; DXLauncher writes every key with its own type
static bool KeyTaken(const JsonValue& section, const char* sectionName, const char* key, JsonType type, const char* what)
{
	const JsonValue& value = section[key];
	if (value.is_undefined())
		return false;
	if (value.type() == type)
		return true;
	LogMessage("Settings.json: " + std::string(sectionName) + "." + key + " is not " + what + ": its default stands");
	return false;
}

static void ReadBool(const JsonValue& section, const char* sectionName, const char* key, bool& target)
{
	if (KeyTaken(section, sectionName, key, JsonType::boolean, "true or false"))
		target = section[key].to_boolean();
}

static void ReadFloat(const JsonValue& section, const char* sectionName, const char* key, float& target)
{
	if (KeyTaken(section, sectionName, key, JsonType::number, "a number"))
		target = (float)section[key].to_number();
}

// Held to an int's range as the double it is before the cast, which past
// that range is undefined (1e10, or 1e999, read as the infinity)
static void ReadInt(const JsonValue& section, const char* sectionName, const char* key, int& target)
{
	if (KeyTaken(section, sectionName, key, JsonType::number, "a number"))
		target = (int)std::clamp(section[key].to_number(), (double)INT_MIN, (double)INT_MAX);
}

static std::string ReadChoice(const JsonValue& section, const char* sectionName, const char* key)
{
	if (KeyTaken(section, sectionName, key, JsonType::string, "a name"))
		return section[key].to_string();
	return {};
}

// The R36S launcher's environment overrides (DXE_R36S_*), applied after
// Settings.json: a value the launcher's r36s.ini names wins over the file, as
// its RenderScale and VSync do.
static void ApplyEnvInt(const char* name, int& target, int lo, int hi)
{
	if (const char* value = std::getenv(name))
	{
		char* end = nullptr;
		long v = std::strtol(value, &end, 10);
		if (end != value && *end == '\0')
			target = (int)std::clamp(v, (long)lo, (long)hi);
	}
}

static void ApplyEnvBool(const char* name, bool& target)
{
	if (const char* value = std::getenv(name))
	{
		std::string setting(value);
		if (setting == "0" || setting == "1")
			target = setting == "1";
	}
}

static void ApplyEnvFloat(const char* name, float& target, float lo, float hi)
{
	if (const char* value = std::getenv(name))
	{
		char* end = nullptr;
		float v = std::strtof(value, &end);
		if (end != value && *end == '\0')
			target = std::clamp(v, lo, hi);
	}
}

LauncherSettings::LauncherSettings()
{
	const bool handheld = std::getenv("DXE_R36S_GAMEPAD") != nullptr;
	Gamepad.InvertY = handheld;
	if (handheld)
	{
		Gamepad.CursorSpeed = 0.45f;
		// The R36S profile: the content the game draws small is enlarged;
		// controls keep the original's size until each screen is checked.
		Interface.DialogTextScale = 1.5f;
		Interface.HUDTextScale = 1.5f;
		Interface.MenuTextScale = 1.25f;
		Interface.VirtualKeyboard = true;
		Interface.KeyboardAutoShow = true;
		Interface.ShowButtonHints = true;
	}
	try
	{
		JsonValue settings = JsonValue::parse(File::read_all_text(GetSettingsFilename()));

		const JsonValue& rendev = settings["RenderDevice"];
		std::string rendevtype = ReadChoice(rendev, "RenderDevice", "Type");
		if (rendevtype == "Vulkan")
			RenderDevice.Type = RenderDeviceType::Vulkan;
		else if (rendevtype == "D3D11")
			RenderDevice.Type = RenderDeviceType::D3D11;
		else if (rendevtype == "OpenGL")
			RenderDevice.Type = RenderDeviceType::OpenGL;
		else if (rendevtype == "GLES")
			RenderDevice.Type = RenderDeviceType::GLES;
		else if (rendevtype == "Software")
			RenderDevice.Type = RenderDeviceType::Software;
		RenderDevice.TypeNamed = rendevtype == "Vulkan" || rendevtype == "D3D11" || rendevtype == "OpenGL" || rendevtype == "GLES" || rendevtype == "Software";
		ReadBool(rendev, "RenderDevice", "CPUDriver", RenderDevice.CPUDriver);
		RenderDevice.Device = ReadChoice(rendev, "RenderDevice", "Device");

		std::string rendevaa = ReadChoice(rendev, "RenderDevice", "Antialias");
		if (rendevaa == "Off")
			RenderDevice.Antialias = AntialiasMode::Off;
		else if (rendevaa == "MSAA2x")
			RenderDevice.Antialias = AntialiasMode::MSAA2x;
		else if (rendevaa == "MSAA4x")
			RenderDevice.Antialias = AntialiasMode::MSAA4x;

		std::string rendevlight = ReadChoice(rendev, "RenderDevice", "Light");
		if (rendevlight == "Normal")
			RenderDevice.Light = LightMode::Normal;
		else if (rendevlight == "OneX")
			RenderDevice.Light = LightMode::OneX;
		else if (rendevlight == "BrighterActors")
			RenderDevice.Light = LightMode::BrighterActors;

		std::string rendevgamma = ReadChoice(rendev, "RenderDevice", "Gamma");
		if (rendevgamma == "D3D9")
			RenderDevice.Gamma = GammaMode::D3D9;
		else if (rendevgamma == "XOpenGL")
			RenderDevice.Gamma = GammaMode::XOpenGL;

		// Absent keys keep the header's defaults (UseVSync on, the scales 128),
		// as the Gamepad block's do: a file without them read vsync off and
		// each scale's floor
		ReadBool(rendev, "RenderDevice", "GammaCorrectScreenshots", RenderDevice.GammaCorrectScreenshots);
		ReadBool(rendev, "RenderDevice", "UseVSync", RenderDevice.UseVSync);
		ReadBool(rendev, "RenderDevice", "Hdr", RenderDevice.Hdr);
		ReadInt(rendev, "RenderDevice", "HdrScale", RenderDevice.HdrScale);
		ReadBool(rendev, "RenderDevice", "Bloom", RenderDevice.Bloom);
		ReadInt(rendev, "RenderDevice", "BloomAmount", RenderDevice.BloomAmount);
		ReadBool(rendev, "RenderDevice", "UseDebugLayer", RenderDevice.UseDebugLayer);
		ReadFloat(rendev, "RenderDevice", "Contrast", RenderDevice.Contrast);
		ReadFloat(rendev, "RenderDevice", "Saturation", RenderDevice.Saturation);
		ReadFloat(rendev, "RenderDevice", "GammaOffset", RenderDevice.GammaOffset);
		ReadFloat(rendev, "RenderDevice", "GammaOffsetRed", RenderDevice.GammaOffsetRed);
		ReadFloat(rendev, "RenderDevice", "GammaOffsetGreen", RenderDevice.GammaOffsetGreen);
		ReadFloat(rendev, "RenderDevice", "GammaOffsetBlue", RenderDevice.GammaOffsetBlue);
		ReadFloat(rendev, "RenderDevice", "LODBias", RenderDevice.LODBias);

		// Absent members keep their defaults: a missing bool would otherwise
		// read as false and quietly turn controller support off.
		const JsonValue& pad = settings["Gamepad"];
		ReadBool(pad, "Gamepad", "Enabled", Gamepad.Enabled);
		ReadFloat(pad, "Gamepad", "DeadZone", Gamepad.DeadZone);
		ReadFloat(pad, "Gamepad", "LookSensitivityX", Gamepad.LookSensitivityX);
		ReadFloat(pad, "Gamepad", "LookSensitivityY", Gamepad.LookSensitivityY);
		ReadBool(pad, "Gamepad", "InvertY", Gamepad.InvertY);
		ReadFloat(pad, "Gamepad", "CursorSpeed", Gamepad.CursorSpeed);
		ReadBool(pad, "Gamepad", "DXController", Gamepad.DXController);

		const JsonValue& perf = settings["Performance"];
		ReadFloat(perf, "Performance", "RenderScale", Performance.RenderScale);
		ReadInt(perf, "Performance", "DetailBands", Performance.DetailBands);
		ReadBool(perf, "Performance", "Trilinear", Performance.Trilinear);
		ReadInt(perf, "Performance", "Anisotropy", Performance.Anisotropy);
		ReadBool(perf, "Performance", "MacroTwoPass", Performance.MacroTwoPass);
		ReadBool(perf, "Performance", "SceneRGBA8", Performance.SceneRGBA8);
		ReadBool(perf, "Performance", "NoDynamicLights", Performance.NoDynamicLights);
		ReadBool(perf, "Performance", "FreezeAnimatedLights", Performance.FreezeAnimatedLights);
		ReadBool(perf, "Performance", "PrecacheTextures", Performance.PrecacheTextures);
		ReadBool(perf, "Performance", "PrecacheLightmaps", Performance.PrecacheLightmaps);
		ReadInt(perf, "Performance", "StreamVertices", Performance.StreamVertices);
		ReadInt(perf, "Performance", "StreamIndices", Performance.StreamIndices);
		ReadBool(perf, "Performance", "StreamOrphan", Performance.StreamOrphan);

		const JsonValue& ui = settings["Interface"];
		ReadFloat(ui, "Interface", "DialogTextScale", Interface.DialogTextScale);
		ReadFloat(ui, "Interface", "HUDTextScale", Interface.HUDTextScale);
		ReadFloat(ui, "Interface", "MenuTextScale", Interface.MenuTextScale);
		ReadFloat(ui, "Interface", "MenuControlTextScale", Interface.MenuControlTextScale);
		ReadBool(ui, "Interface", "VirtualKeyboard", Interface.VirtualKeyboard);
		ReadBool(ui, "Interface", "KeyboardAutoShow", Interface.KeyboardAutoShow);
		ReadBool(ui, "Interface", "ShowButtonHints", Interface.ShowButtonHints);
		ReadBool(ui, "Interface", "LogUI", Interface.LogUI);
		Interface.DialogTextScale = std::clamp(Interface.DialogTextScale, 1.0f, 2.0f);
		Interface.HUDTextScale = std::clamp(Interface.HUDTextScale, 1.0f, 2.0f);
		Interface.MenuTextScale = std::clamp(Interface.MenuTextScale, 1.0f, 2.0f);
		Interface.MenuControlTextScale = std::clamp(Interface.MenuControlTextScale, 1.0f, 1.5f);
	}
	catch (const std::exception& e)
	{
		// Said, not dropped in silence: every renderer and pad setting then
		// stands at its default. The error's own line alone: an engine
		// error's text goes on with where it was thrown, which is no news of
		// a file that is not there, as in a run by hand.
		std::string why = e.what();
		why = why.substr(0, why.find('\n'));
		LogMessage("Settings.json was not read (" + why + "): the defaults stand");
	}
	catch (...)
	{
		LogMessage("Settings.json was not read: the defaults stand");
	}
	if (handheld)
	{
		if (const char* value = std::getenv("DXE_R36S_RENDER_SCALE"))
		{
			char* end = nullptr;
			float scale = std::strtof(value, &end);
			if (end != value && *end == '\0' && scale >= 0.25f && scale <= 1.0f)
				Performance.RenderScale = scale;
		}
		if (const char* value = std::getenv("DXE_R36S_VSYNC"))
		{
			std::string setting(value);
			if (setting == "0" || setting == "1")
				RenderDevice.UseVSync = setting == "1";
		}
		ApplyEnvFloat("DXE_R36S_DEADZONE", Gamepad.DeadZone, 0.0f, 0.9f);
		ApplyEnvInt("DXE_R36S_DETAIL_BANDS", Performance.DetailBands, 0, 3);
		ApplyEnvBool("DXE_R36S_TRILINEAR", Performance.Trilinear);
		ApplyEnvInt("DXE_R36S_ANISOTROPY", Performance.Anisotropy, 0, 16);
		ApplyEnvBool("DXE_R36S_MACRO_TWO_PASS", Performance.MacroTwoPass);
		ApplyEnvBool("DXE_R36S_SCENE_RGBA8", Performance.SceneRGBA8);
		ApplyEnvBool("DXE_R36S_NO_DYNAMIC_LIGHTS", Performance.NoDynamicLights);
		ApplyEnvBool("DXE_R36S_FREEZE_ANIMATED_LIGHTS", Performance.FreezeAnimatedLights);
		ApplyEnvBool("DXE_R36S_PRECACHE", Performance.PrecacheTextures);
		ApplyEnvBool("DXE_R36S_PRECACHE_LIGHTMAPS", Performance.PrecacheLightmaps);
		ApplyEnvInt("DXE_R36S_STREAM_VERTICES", Performance.StreamVertices, 4096, 1048576);
		ApplyEnvInt("DXE_R36S_STREAM_INDICES", Performance.StreamIndices, 8192, 4194304);
		ApplyEnvBool("DXE_R36S_STREAM_ORPHAN", Performance.StreamOrphan);
		ApplyEnvFloat("DXE_R36S_DIALOG_SCALE", Interface.DialogTextScale, 1.0f, 2.0f);
		ApplyEnvFloat("DXE_R36S_HUD_SCALE", Interface.HUDTextScale, 1.0f, 2.0f);
		ApplyEnvFloat("DXE_R36S_MENU_SCALE", Interface.MenuTextScale, 1.0f, 2.0f);
		ApplyEnvFloat("DXE_R36S_MENU_CONTROL_SCALE", Interface.MenuControlTextScale, 1.0f, 1.5f);
		ApplyEnvBool("DXE_R36S_VIRTUAL_KEYBOARD", Interface.VirtualKeyboard);
		ApplyEnvBool("DXE_R36S_KEYBOARD_AUTO_SHOW", Interface.KeyboardAutoShow);
		ApplyEnvBool("DXE_R36S_BUTTON_HINTS", Interface.ShowButtonHints);
		ApplyEnvBool("DXE_R36S_LOG_UI", Interface.LogUI);
		LogMessage("R36S interface: dialog=" + std::to_string(Interface.DialogTextScale)
			+ " hud=" + std::to_string(Interface.HUDTextScale)
			+ " menu=" + std::to_string(Interface.MenuTextScale)
			+ " controls=" + std::to_string(Interface.MenuControlTextScale)
			+ " keyboard=" + (Interface.VirtualKeyboard ? "1" : "0")
			+ " auto-show=" + (Interface.KeyboardAutoShow ? "1" : "0")
			+ " hints=" + (Interface.ShowButtonHints ? "1" : "0"));
	}
}
