#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGTypes.h"
#include "HGMonster.generated.h"

class AHGLevel;
class AHGPlayer;
class UAudioComponent;
class UPointLightComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
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

	/** The middle of its body in the world, wherever it has put itself: the point the player has to see to be watching it. */
	FVector Chest() const;

	const TCHAR* DisplayName() const;
	const TCHAR* CatchLine() const;
	/** One word for the debug line and the tests. */
	FString Mood() const;

	EHGMonster Kind = EHGMonster::Walker;

private:
	enum class EState : uint8 { Patrol, Investigate, Linger, Hunt, WindUp, Charge };

	void BuildStandIn(const FLinearColor& Color, float Height);
	/** Builds a rigged creature that carries its own clips (one imported file). False when it is not there. */
	bool BuildCreature(const TCHAR* MeshPath, const TCHAR* InClipPrefix, float Scale);
	/** Builds the rigged, animated human from the private assets plugin. False when the plugin is not there. */
	bool BuildHuman(float Scale, const FLinearColor& Tint);
	/** Plays one clip of the animation library on the rig. Does nothing if it is already playing. */
	void PlayClip(const TCHAR* Name, float Rate = 1.f, bool bLoop = true);
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
	void Animate(float Dt, AHGPlayer* Player);
	void UpdateLegs(float Dt);

	/** One of the Mouth's legs. The foot stays planted on a wall or the floor until the body has left it behind. */
	struct FLeg
	{
		FVector Hip = FVector::ZeroVector;
		float Side = 1.f;
		float Along = 0.f;
		int32 Group = 0;
		FVector Foot = FVector::ZeroVector;
		FVector From = FVector::ZeroVector;
		float Step = 1.f;
		bool bPlaced = false;
		UStaticMeshComponent* Upper = nullptr;
		UStaticMeshComponent* Lower = nullptr;
		UStaticMeshComponent* Knee = nullptr;
	};
	TArray<FLeg> Legs;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<USceneComponent> Body;

	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> Arms;

	UPROPERTY()
	TObjectPtr<USceneComponent> Head;

	/** The animated skeleton from the private assets plugin. Hidden; the visible body follows it bone for bone. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Rig;

	FName Clip;
	/** Asset path that every clip name is appended to, and the clip this body runs with. */
	FString ClipPrefix;
	const TCHAR* RunClip = TEXT("Sprint_Loop");

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
	float TwitchLeft = 0.f;
	float TwitchIn = 2.f;
	FVector LastSpot = FVector::ZeroVector;
	bool bWasObserved = false;
	FVector NoiseLocation = FVector::ZeroVector;
	FVector ChargeDirection = FVector::ForwardVector;
	TArray<FVector> Path;
	int32 PathIndex = 0;
};
