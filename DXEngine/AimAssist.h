#pragma once

#include "Math/rotator.h"

class Engine;
class UActor;
class UPlayerPawn;

// The handheld's aim aid, for a stick too coarse to put the crosshair on a
// pawn:
//
//  - the look stick moves slower while a target is nearly under the
//    crosshair, so the view lingers on it, and
//  - a shot inside that cone is turned to the target -- Half-Life's aiming
//    help -- while the view itself is put back as it was afterwards, so the
//    snap does not jump the camera, and
//  - while a fire button is held, the view is eased toward the target, which
//    keeps a running pawn under the crosshair.
//
// The target is the engine's own Pawn.PickTarget: another pawn, alive, seen,
// in front, no team mate, within its 2000-unit reach. Off unless
// Gamepad.AimAssist (r36s.ini's AimAssist) is on.
class AimAssist
{
public:
	static AimAssist& Get();

	// The gamepad's look speed: 1.0, or the configured slowdown while a
	// target is near. The target is looked for a few times a second, not
	// on every tick, and only while the look stick is being moved.
	float LookScale(Engine* engine, float timeElapsed, bool looking);

	// A shot: the player's view set on the target, to be put back by
	// EndShot once the fire command has run. True when it was moved, and
	// `saved` then holds the view it had.
	bool BeginShot(Engine* engine, Rotator& saved);
	void EndShot(Engine* engine, const Rotator& saved);

	// While a fire button is held, the view eased toward the target at
	// Gamepad.AimAssistPull degrees a second. The target is looked for a
	// few times a second and its last aim kept, so a running pawn stays
	// under the crosshair without a trace every tick.
	void ApplyPull(Engine* engine, UPlayerPawn* player, float timeElapsed);

	// The focus or the interface gone: the target and its cached slowdown
	// forgotten.
	void Forget();

private:
	AimAssist() = default;

	// The local player, or None when there is nobody to aim for: a dead
	// player, a menu, a pause, a camera, a scripted view or a net client.
	UPlayerPawn* LocalPlayer(Engine* engine) const;

	// The best pawn inside coneDegrees of the view, as PickTarget picks it;
	// `distance` is how far it is, and `aim` the cosine of the angle it sits
	// at (1.0 dead centre).
	UActor* FindTarget(UPlayerPawn* player, float coneDegrees, float& distance, float& aim);

	// The player's view with its yaw and pitch on the target's chest, its
	// roll kept as it was.
	Rotator AimAt(UPlayerPawn* player, UActor* target) const;

	float cacheRemaining = 0.0f;
	float cachedScale = 1.0f;

	// The pull's aim, kept between searches: a rotation, not the actor, so a
	// target destroyed between two searches leaves no stale pointer behind.
	Rotator pullAim;
	bool pullAimValid = false;
	float pullCacheRemaining = 0.0f;
};
