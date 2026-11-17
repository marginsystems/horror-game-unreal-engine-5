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

/** Something a small child can pick up and carry: a toy, or one of the sorter's three shapes. */
UCLASS()
class AHGToy : public AActor
{
	GENERATED_BODY()

public:
	AHGToy();

	/** Shows one of the game's own models (see HG::ModelNames). */
	void SetupModel(FName InKind, const TCHAR* ModelName);
	/** Shows any static mesh, for toys that come from an asset pack. */
	void SetupMesh(FName InKind, UStaticMesh* Mesh, float Scale);

	FName Kind;
	/** True once it sits in the sorter. It stays there. */
	bool bPlaced = false;

private:
	/** An invisible ball around the toy, so looking at it finds it. */
	void AddGrip(const FVector& Centre, float Radius);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
};

/** The shape sorter: a wooden box that takes the circle, the square and the triangle. */
UCLASS()
class AHGSorter : public AActor
{
	GENERATED_BODY()

public:
	AHGSorter();

	void Setup();
	/** Puts the toy in its hole if it is one of the three shapes. False for anything else. */
	bool Accept(AHGToy* Toy);

	int32 Filled = 0;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
};

