#pragma once

#include <string>

enum class RenderDeviceType
{
	Vulkan,
	D3D11,
	OpenGL,
	GLES,
	Software
};

enum class AntialiasMode
{
	Off,
	MSAA2x,
	MSAA4x
};

enum class LightMode
{
	Normal,
	OneX,
	BrighterActors
};

enum class GammaMode
{
	D3D9,
	XOpenGL
};

class LauncherSettings
{
public:
	static LauncherSettings& Get();

	struct
	{
		RenderDeviceType Type = RenderDeviceType::Vulkan;
		bool TypeNamed = false; // Settings.json names Type: else the game's ini chooses (Engine::ChooseRenderDevice)
		bool CPUDriver = false; // the Vulkan or GL device on Mesa's CPU driver, lavapipe or llvmpipe
		std::string Device; // a part of the GPU's name the Vulkan device takes; empty, the automatic order's first (VulkanDeviceBuilder::SelectDeviceByName)
		bool UseVSync = true;
		AntialiasMode Antialias = AntialiasMode::MSAA4x;
		LightMode Light = LightMode::Normal;
		GammaMode Gamma = GammaMode::D3D9;
		bool GammaCorrectScreenshots = false;
		bool Hdr = false;
		int HdrScale = 128;
		bool Bloom = false;
		int BloomAmount = 128;
		bool UseDebugLayer = false;
		// The present's colour correction, 1 each its picture unchanged, and
		// its gamma's offsets, all of them and each channel's; the textures'
		// mip bias
		float Contrast = 1.0f;
		float Saturation = 1.0f;
		float GammaOffset = 0.0f;
		float GammaOffsetRed = 0.0f;
		float GammaOffsetGreen = 0.0f;
		float GammaOffsetBlue = 0.0f;
		float LODBias = -0.5f;
	} RenderDevice;

	// Controller support (Engine::UpdateGamepad).
	struct
	{
		bool Enabled = true;
		float DeadZone = 0.2f;
		float LookSensitivityX = 1.0f;
		float LookSensitivityY = 1.0f;
		bool InvertY = false;
		float CursorSpeed = 1.0f;
		// The handheld's aim aid (AimAssist): the look stick slows while a
		// target is near, a shot inside the cone is turned to it with the
		// view put back afterwards, and holding fire eases the view toward
		// the target. Slowdown 1.0 leaves the look speed alone; the cone is
		// the half-angle from the crosshair, in degrees.
		bool AimAssist = false;
		float AimAssistCone = 8.0f;
		float AimAssistSlowdown = 0.6f;
		// While the fire button is held, the view is eased toward the target
		// at this many degrees a second, which keeps a running pawn under the
		// crosshair. 0 leaves the view alone.
		float AimAssistPull = 8.0f;
		bool LogAim = false;
		// The pad as the DXController mod's scripts take it: of the keys
		// above Enabled alone read (DXControllerPad)
		bool DXController = false;
	} Gamepad;

	// The handheld's interface: text larger than the original's on the
	// 640x480 screen, and the pad's on-screen keyboard for the game's edit
	// fields. Every scale is 1.0 (the original's) off the handheld.
	struct
	{
		// Conversation speech and replies, and the first-person conversation.
		float DialogTextScale = 1.0f;
		// The HUD's log and mission start text.
		float HUDTextScale = 1.0f;
		// Terminal, persona, goal, log, message and HUD bark text.
		float MenuTextScale = 1.0f;
		// Menu buttons, as opposed to their labels and content: kept at the
		// original's size until a screen is checked for overflow.
		float MenuControlTextScale = 1.0f;
		// The on-screen keyboard for UEditWindows; only the handheld shows
		// one. AutoShow opens it as an edit field takes the focus.
		bool VirtualKeyboard = false;
		bool KeyboardAutoShow = true;
		// A small "SELECT: teclado" hint while a field waits for the
		// keyboard to be opened again.
		bool ShowButtonHints = false;
		// Log the focused window's class and place when the focus changes.
		bool LogUI = false;
	} Interface;

	// Speed for slow hardware.
	struct
	{
		float RenderScale = 1.0f; // the scene's size as a fraction of the window's (RenderDevice::GetRenderScale)

		// The GL device's scene, tuned for the R36S's Mali-G31: the detail
		// bands D3DDrv draws (0 off, 1 to 3 bands), trilinear filtering, and
		// the anisotropy a smooth sampler asks for.
		int DetailBands = 3;
		bool Trilinear = true;
		int Anisotropy = 8;
		// D3DDrv's two-pass macro/light-map maths, in the scene shader's one
		// pass. No extra texture sampling: on or off alike.
		bool MacroTwoPass = true;
		// The scene's colour buffer made RGBA8 instead of RGB10_A2.
		bool SceneRGBA8 = false;
		// Force the game's NoDynamicLights: light maps built once, no
		// animated or flickering light rebuilt every frame (20-40 ms a frame
		// in some areas on the R36S). False leaves the game's own setting.
		// Turns the flashlight off too, as the game's option does.
		bool NoDynamicLights = false;
		// Bake animating lights once (flickers, pulses, wavers) while still
		// drawing the bDynamicLight ones every frame: the flashlight, muzzle
		// flashes and explosions keep working. This is the R36S profile's
		// choice; NoDynamicLights is the game's own, harsher one.
		bool FreezeAnimatedLights = false;
		// Upload the level's textures while the load screen shows, in place
		// of on each area's first appearance. DXE_R36S_PRECACHE (0 or 1)
		// wins over this when set.
		bool PrecacheTextures = false;
		// Build and upload the level's light maps while the load screen
		// shows, in place of on each area's first appearance. Costs load
		// time and memory. DXE_R36S_PRECACHE_LIGHTMAPS wins when set.
		bool PrecacheLightmaps = false;
		// The scene's streaming vertex and index buffers: larger buffers flush
		// fewer times in a frame, and each flush is a driver stall candidate.
		int StreamVertices = 16 * 1024;
		int StreamIndices = 32 * 1024;
		// Upload each flush into a fresh buffer (glBufferData) in place of a
		// glBufferSubData into one the GPU may still be reading.
		bool StreamOrphan = false;
	} Performance;

private:
	LauncherSettings();
};
