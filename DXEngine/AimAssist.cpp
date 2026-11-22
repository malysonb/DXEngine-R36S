#include "Precomp.h"
#include "AimAssist.h"
#include "Engine.h"
#include "LauncherSettings.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Utils/Logger.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float Pi = 3.14159265358979f;
	constexpr float CacheSeconds = 0.1f;

	// How high inside the pawn's cylinder a shot is aimed: the original
	// player auto-aim's spot, 0.3 of the height above its centre.
	constexpr float ChestHeight = 0.3f;

	vec3 EyeLocation(UPlayerPawn* player)
	{
		return player->Location() + vec3(0.0f, 0.0f, player->EyeHeight());
	}

	// The cone a setting asks for, scaled to the field of view in use: a
	// scoped view covers less of the world across the same screen, so the
	// same screen-space help is a narrower world angle there. The default
	// 90-degree view leaves the setting as it is.
	float EffectiveCone(UPlayerPawn* player, float coneDegrees)
	{
		float fovScale = 1.0f;
		if (player)
		{
			const float defaultFov = player->DefaultFOV();
			const float desiredFov = player->DesiredFOV();
			if (defaultFov > 1.0f && desiredFov > 1.0f)
				fovScale = std::clamp(desiredFov / defaultFov, 0.15f, 1.5f);
		}
		return std::clamp(coneDegrees * fovScale, 0.5f, 30.0f);
	}
}

AimAssist& AimAssist::Get()
{
	static AimAssist aim;
	return aim;
}

UPlayerPawn* AimAssist::LocalPlayer(Engine* engine) const
{
	if (!engine || !engine->viewport)
		return nullptr;

	UPlayerPawn* player = engine->viewport->Actor();
	if (!player || player->bDeleteMe() || player->Health() <= 0)
		return nullptr;

	// A menu, a camera, a scripted view or a conversation: no aiming help
	// is wanted there, and the HUD's windows have already taken the input.
	if (player->bShowMenu() || player->bFixedCamera())
		return nullptr;
	UActor* viewTarget = player->ViewTarget();
	if (viewTarget && viewTarget != player)
		return nullptr;
	if (engine->dxRootWindow && engine->dxRootWindow->IsKeyboardGrabbed())
		return nullptr;

	if (engine->LevelInfo)
	{
		if (engine->LevelInfo->NetMode() != NM_Standalone)
			return nullptr;
		if (!engine->LevelInfo->Pauser().empty())
			return nullptr;
	}
	return player;
}

UActor* AimAssist::FindTarget(UPlayerPawn* player, float coneDegrees, float& distance, float& aim)
{
	distance = 0.0f;
	aim = 0.0f;
	const float cone = EffectiveCone(player, coneDegrees);
	const Rotator view = player->ViewRotation();
	const vec3 forward = Coords::Rotation(view).XAxis;
	const vec3 start = EyeLocation(player);

	// PickTarget's aim is the cosine of the angle from the view: the cone's
	// edge is the smallest that passes. It checks the pawn list, alive and
	// visible, and its own 2000-unit reach.
	float bestAim = std::cos(cone * Pi / 180.0f);
	float bestDist = 0.0f;
	UActor* target = player->PickTarget(bestAim, bestDist, forward, start);
	if (target && (target->bHidden() || target->bDeleteMe()))
		return nullptr;
	distance = bestDist;
	aim = bestAim;
	return target;
}

Rotator AimAssist::AimAt(UPlayerPawn* player, UActor* target) const
{
	vec3 point = target->Location();
	point.z += target->CollisionHeight() * ChestHeight;
	Rotator aim = Rotator::FromVector(point - EyeLocation(player));
	aim.Roll = player->ViewRotation().Roll;
	return aim;
}

float AimAssist::LookScale(Engine* engine, float timeElapsed, bool looking)
{
	const auto& settings = LauncherSettings::Get().Gamepad;
	if (!settings.AimAssist || settings.AimAssistSlowdown >= 0.999f || !looking)
	{
		cacheRemaining = 0.0f;
		cachedScale = 1.0f;
		return 1.0f;
	}

	cacheRemaining -= std::max(timeElapsed, 0.0f);
	if (cacheRemaining > 0.0f)
		return cachedScale;

	cacheRemaining = CacheSeconds;
	cachedScale = 1.0f;
	if (UPlayerPawn* player = LocalPlayer(engine))
	{
		float distance = 0.0f;
		float aim = 0.0f;
		if (FindTarget(player, settings.AimAssistCone, distance, aim))
		{
			// From the cone's edge (the look speed unchanged) to dead centre
			// (the full slowdown), so the stick does not step as a target
			// enters the cone.
			const float edge = std::cos(EffectiveCone(player, settings.AimAssistCone) * Pi / 180.0f);
			const float span = 1.0f - edge;
			const float closeness = span > 0.0001f ? std::clamp((aim - edge) / span, 0.0f, 1.0f) : 1.0f;
			const float slowdown = std::clamp(settings.AimAssistSlowdown, 0.2f, 1.0f);
			cachedScale = 1.0f - (1.0f - slowdown) * closeness;
		}
	}
	return cachedScale;
}

bool AimAssist::BeginShot(Engine* engine, Rotator& saved)
{
	const auto& settings = LauncherSettings::Get().Gamepad;
	if (!settings.AimAssist)
		return false;

	UPlayerPawn* player = LocalPlayer(engine);
	if (!player || !player->Weapon())
		return false;

	float distance = 0.0f;
	float aim = 0.0f;
	UActor* target = FindTarget(player, settings.AimAssistCone, distance, aim);
	if (!target)
		return false;

	const Rotator current = player->ViewRotation();
	saved = current;
	player->ViewRotation() = AimAt(player, target);

	if (settings.LogAim)
	{
		const int angle = (int)(std::acos(std::clamp(aim, -1.0f, 1.0f)) * (180.0f / Pi) + 0.5f);
		LogMessage("R36S aim: shot target=" + UObject::GetUClassName(target).ToString()
			+ " dist=" + std::to_string((int)(distance + 0.5f))
			+ " angle=" + std::to_string(angle));
	}
	return true;
}

void AimAssist::EndShot(Engine* engine, const Rotator& saved)
{
	if (engine && engine->viewport)
	{
		if (UPlayerPawn* player = engine->viewport->Actor())
			player->ViewRotation() = saved;
	}
}

void AimAssist::ApplyPull(Engine* engine, UPlayerPawn* player, float timeElapsed)
{
	const auto& settings = LauncherSettings::Get().Gamepad;
	if (!settings.AimAssist || settings.AimAssistPull <= 0.0f || timeElapsed <= 0.0f)
		return;
	if (!player || !player->Weapon() || (player->bFire() == 0 && player->bAltFire() == 0))
	{
		pullAimValid = false;
		pullCacheRemaining = 0.0f;
		return;
	}
	if (LocalPlayer(engine) != player)
		return;

	// The aim is looked for twenty times a second, not every tick: the
	// rotation kept is the target's place then, not the target itself.
	const float dt = std::min(timeElapsed, 0.1f);
	pullCacheRemaining -= dt;
	if (pullCacheRemaining <= 0.0f)
	{
		pullCacheRemaining = 0.05f;
		float distance = 0.0f;
		float aim = 0.0f;
		UActor* target = FindTarget(player, settings.AimAssistCone, distance, aim);
		pullAimValid = target != nullptr;
		if (target)
			pullAim = AimAt(player, target);
	}
	if (!pullAimValid)
		return;

	Rotator view = player->ViewRotation();
	const int deltaYaw = ((pullAim.Yaw - view.Yaw + 32768) & 0xffff) - 32768;
	const int deltaPitch = ((pullAim.Pitch - view.Pitch + 32768) & 0xffff) - 32768;
	const int step = (int)(settings.AimAssistPull * dt * (65536.0f / 360.0f) + 0.5f);
	if (step <= 0)
		return;
	view.Yaw += std::clamp(deltaYaw, -step, step);
	view.Pitch += std::clamp(deltaPitch, -step, step);
	player->ViewRotation() = view;
}

void AimAssist::Forget()
{
	cacheRemaining = 0.0f;
	cachedScale = 1.0f;
	pullAimValid = false;
	pullCacheRemaining = 0.0f;
}
