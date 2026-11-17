#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HGTypes.h"
#include "HGPlayer.generated.h"

class UCameraComponent;
class UPointLightComponent;
class USpotLightComponent;

/**
 * First-person player. Keys are read directly each frame, so the project needs no input assets:
 * WASD move, mouse look, Shift sprint (loud), Ctrl tiptoe (silent), left mouse use item,
 * 1-4 choose item, F smack the flashlight, R restart, F1-F3 switch preview, Esc quit.
 */
UCLASS()
class AHGPlayer : public ACharacter
{
	GENERATED_BODY()

public:
	AHGPlayer();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Adds an item to the first free slot. False when the hands are full or it is a duplicate. */
	bool GiveItem(EHGItem Item);
	/** True when Target is in view, in line of sight, lit, and the player is not mid-blink. */
	bool CanObserve(const FVector& Target) const;
	bool IsBlinking() const { return BlinkLeft > 0.f; }
	bool IsFlashlightOn() const { return bLightOn; }
	FVector EyeLocation() const;
	FVector EyeForward() const;

	void UseItem();
	/** Scene one: pick up the toy being looked at, or put down or post the one being held. */
	void Interact();
	bool IsChild() const { return bChild; }
	/** The camera's colour and exposure, so a cutscene camera can match it. */
	const struct FPostProcessSettings& Look() const;
	class AHGToy* HeldToy() const { return Held; }

	/**
	 * Scene two: he is picked up. His legs and hands stop answering, whatever he held is left on the
	 * floor, and from then on something else decides where his eyes are; he can still look around.
	 */
	void SetCarried(bool bOn);
	bool IsCarried() const { return bCarried; }
	/** Puts his eyes at this point in the world. Only while carried. */
	void CarryEyesTo(const FVector& Eyes);
	/** Eases his gaze towards Target, the way a child's eyes are drawn to a face. He can pull away from it. */
	void TurnTowards(const FVector& Target, float Dt, float Speed);
	/**
	 * Scene five: he is on his own feet again and can get down on his hands and knees (Ctrl or C,
	 * held). Crawling is slow and silent and fits under beds and through gaps no adult can follow.
	 */
	void SetCrawlAllowed(bool bOn) { bCrawlAllowed = bOn; }
	bool IsCrawling() const { return bIsCrouched; }
	/** For the self-play check and the pictures: crawl without a key held. */
	void ForceCrawl(bool bOn) { bForceCrawl = bOn; }
	/** How far the noise he is making right now carries: nothing crawling or still, a little walking, a long way running. */
	float NoiseRadius() const;
	/** Throws the toy in his hands to where he is looking. It squeaks where it lands. */
	void ThrowHeld();
	/** 1.3 is the ward's bright colour, 1 is plain, 0 is none. */
	void SetSaturation(float Saturation);
	/** The lens angle in degrees. Narrower than the usual 92 for a face held close. */
	void SetLens(float Degrees);
	void SmackFlashlight();
	void SelectSlot(int32 Index);
	void LookAt(const FVector& Target);
	void FallOver();

	EHGItem HeldItem() const { return Slots.IsValidIndex(Selected) ? Slots[Selected] : EHGItem::None; }

	TArray<EHGItem> Slots;
	int32 Selected = INDEX_NONE;
	/** 1 is calm. At 0 the player blinks (clinic) or screams (everywhere else). */
	float Composure = 1.f;
	/** What the death screen blames. */
	FString LastCause = TEXT("went in with empty hands and a positive attitude");
	int32 Screams = 0;
	int32 Discharges = 0;

private:
	void HandleKeys(class APlayerController* PC, class AHGGameMode* Game);
	void UpdateFlashlight(float Dt, class AHGGameMode* Game);
	void UpdateComposure(float Dt, class AHGGameMode* Game);
	void RebuildHeld();

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	/** The toy in the child's hands, in the ward. */
	UPROPERTY()
	TObjectPtr<class AHGToy> Held;

	/** True in the story's first scene: a five-year-old's height, pace and hands; no flashlight, no weapons. */
	bool bChild = false;
	bool bCarried = false;
	bool bCrawlAllowed = false;
	bool bForceCrawl = false;

	/** The toy in the air after a throw, and its flight. */
	UPROPERTY()
	TObjectPtr<class AHGToy> Thrown;
	FVector ThrowFrom = FVector::ZeroVector;
	FVector ThrowTo = FVector::ZeroVector;
	float ThrowAlong = 0.f;
	/** Degrees turned per mouse count. The minus and equals keys change it. */
	float LookSpeed = 0.24f;

	UPROPERTY()
	TObjectPtr<USpotLightComponent> Flashlight;

	UPROPERTY()
	TObjectPtr<USceneComponent> Hand;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> HandLight;

	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> HeldParts;

	bool bLightOn = true;
	bool bPistolFlagOut = false;
	bool bDead = false;
	float LightDiesAt = 0.f;
	float ShotgunReadyAt = 0.f;
	float BlinkLeft = 0.f;
	float StepBank = 0.f;
	float HeartIn = 0.f;
	int32 StepCount = 0;
	float UseKick = 0.f;
	float Bob = 0.f;
};
