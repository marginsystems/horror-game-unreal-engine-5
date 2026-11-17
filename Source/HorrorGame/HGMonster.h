#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGTypes.h"
#include "HGMonster.generated.h"

class AHGLevel;
class AHGPlayer;
class UAudioComponent;
class UPointLightComponent;
class UTextRenderComponent;

/**
 * The thing that hunts you. One actor, three behaviours:
 *  - Walker: always knows where you are, but can only move while you are not looking at it.
 *  - Librarian: blind. Runs at any noise. Stops to shush it first.
 *  - Mouth: charges in a straight line and cannot turn. Hits the wall, has to back out.
 * It walks the level grid directly, so it needs no navigation mesh.
 */
UCLASS()
class AHGMonster : public AActor
{
	GENERATED_BODY()

public:
	AHGMonster();
	virtual void Tick(float DeltaSeconds) override;

	void Setup(EHGMonster InKind, AHGLevel* InLevel);
	/** Something made a sound it can hear. It goes there and lingers for HoldSeconds. False when it ignores it. */
	bool HearNoise(const FVector& Location, float HoldSeconds, bool bFootstep);
	void Stun(float Seconds);

	const TCHAR* DisplayName() const;
	const TCHAR* CatchLine() const;
	/** One word for the debug line and the tests. */
	FString Mood() const;

	EHGMonster Kind = EHGMonster::Walker;

private:
	enum class EState : uint8 { Patrol, Investigate, Linger, Hunt, WindUp, Charge };

	void BuildStandIn(const FLinearColor& Color, float Height);
	void BuildWalker();
	void BuildLibrarian();
	void BuildMouth();
	void MakeGlow(const FVector& RelLocation, const FLinearColor& Color, float Intensity);

	void TickWalker(float Dt, AHGPlayer* Player);
	void TickLibrarian(float Dt, AHGPlayer* Player);
	void TickMouth(float Dt, AHGPlayer* Player);

	/** Plans a path to the tile under Target. Returns false when there is none. */
	bool GoTo(const FVector& Target);
	/** Walks the planned path. Returns true once the end is reached. */
	bool Advance(float Speed, float Dt);
	void Face(const FVector& Direction, float Dt, float Rate = 8.f);
	void PickPatrolGoal();
	void SetState(EState NewState);
	void Shout(const TCHAR* Text, float Seconds, const FColor& Color);
	void StrikeAPose();
	void Footsteps();
	void Animate(float Dt);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<USceneComponent> Body;

	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> Arms;

	UPROPERTY()
	TObjectPtr<USceneComponent> Head;

	/** The Mouth's top jaw. */
	UPROPERTY()
	TObjectPtr<USceneComponent> Jaw;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> Bubble;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY()
	TObjectPtr<AHGLevel> Level;

	/** The Mouth's idle growl, which follows it around. */
	UPROPERTY()
	TObjectPtr<UAudioComponent> Voice;

	EState State = EState::Patrol;
	float StateTime = 0.f;
	float StunLeft = 0.f;
	float BubbleLeft = 0.f;
	float RepathIn = 0.f;
	float LingerSeconds = 0.f;
	float Age = 0.f;
	float StepBank = 0.f;
	float JawAngle = 16.f;
	FVector LastSpot = FVector::ZeroVector;
	bool bWasObserved = false;
	FVector NoiseLocation = FVector::ZeroVector;
	FVector ChargeDirection = FVector::ForwardVector;
	TArray<FVector> Path;
	int32 PathIndex = 0;
};
