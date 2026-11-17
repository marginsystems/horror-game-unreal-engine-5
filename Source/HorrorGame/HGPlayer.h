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
