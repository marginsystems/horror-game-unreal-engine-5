#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGTypes.h"
#include "HGActors.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;

/** Something lying on the floor that the player picks up by walking over it. */
UCLASS()
class AHGPickup : public AActor
{
	GENERATED_BODY()

public:
	AHGPickup();
	virtual void Tick(float DeltaSeconds) override;

	void Setup(EHGItem InItem);

	/** Builds the look of an item out of basic shapes. Shared with the player's hand. */
	static void BuildItem(AActor* Owner, USceneComponent* Parent, EHGItem Item, TArray<USceneComponent*>& OutParts);

	EHGItem Item = EHGItem::None;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<USceneComponent> Spinner;

	float Age = 0.f;
};

/** A thrown rubber chicken or grenade. The chicken squeaks on every bounce; the grenade is a dud, for a while. */
UCLASS()
class AHGThrown : public AActor
{
	GENERATED_BODY()

public:
	AHGThrown();
	virtual void Tick(float DeltaSeconds) override;

	void Launch(EHGItem InItem, const FVector& Velocity);

	EHGItem Item = EHGItem::Chicken;

private:
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void Explode();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Flash;

	float Age = 0.f;
	float LastSqueak = -10.f;
	float FuseSeconds = 0.f;
	float FlashLeft = 0.f;
	bool bExploded = false;
};

/** A burst of confetti: small coloured squares with physics that settle on the floor and fade out of the world. */
UCLASS()
class AHGConfetti : public AActor
{
	GENERATED_BODY()

public:
	AHGConfetti();

	void Burst(const FVector& Direction);

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
};
