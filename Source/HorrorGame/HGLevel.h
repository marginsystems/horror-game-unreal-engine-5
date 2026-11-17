#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HGTypes.h"
#include "HGLevel.generated.h"

class UInstancedStaticMeshComponent;
class UPointLightComponent;

/**
 * The whole place, built at runtime from a text map: walls, floor, ceiling, lamps and props.
 * It also answers the grid questions the monster needs (paths, line of sight).
 */
UCLASS()
class AHGLevel : public AActor
{
	GENERATED_BODY()

public:
	AHGLevel();
	virtual void Tick(float DeltaSeconds) override;

	void Build(EHGVariant InVariant);
	/** The children's ward room of the first scene: one bright room, a window, a door, a rug. */
	void BuildWard();
	/** Swings the ward's door: 0 shut, 1 wide open onto the hall. */
	void SetDoorOpen(float Alpha);
	/** Shows a time on the ward's wall clock. */
	void SetClock(float Hours);
	FVector ClockSpot() const { return FVector(256, -40, 232); }
	/**
	 * For a ready-made map: walks the floor outwards from Start, a step at a time, and remembers
	 * every place a body can stand and which of them connect. Stairs and ramps count, so it covers
	 * every storey you can reach. Then picks where the fuses, weapons, monster and exit go.
	 */
	void BuildFromWorld(const FVector& Start);
	/** A place inside a ready-made map where a person can stand, for maps that ship without a player start. */
	bool FindStandableSpot(FVector& OutSpot) const;
	/** A route over the walked floor. OutPath leaves out the start. */
	bool FindWorldPath(const FVector& From, const FVector& To, TArray<FVector>& OutPath) const;
	FVector RandomSpot() const;

	/** True when the place is a ready-made map and not one built from text. */
	bool bWorld = false;
	FVector StartSpot = FVector::ZeroVector;
	FVector ExitSpot = FVector::ZeroVector;
	FVector MonsterSpot = FVector::ZeroVector;
	TArray<FVector> FuseSpots;
	TArray<TPair<FVector, EHGItem>> ItemSpots;
	void SetExitOpen(bool bOpen);

	static constexpr float Tile = 400.f;
	static constexpr float WallHeight = 340.f;

	/** Walkable: inside the map, not a wall and not a prop. */
	bool IsOpen(FIntPoint T) const;
	/** Blocks sight: a wall or outside the map. Props are low and do not. */
	bool IsSolid(FIntPoint T) const;
	FIntPoint ToTile(const FVector& World) const;
	FVector ToWorld(FIntPoint T, float Z = 0.f) const;
	/** Breadth-first path. OutPath leaves out From and ends with To. */
	bool FindPath(FIntPoint From, FIntPoint To, TArray<FIntPoint>& OutPath) const;
	bool HasLineOfSight(const FVector& A, const FVector& B) const;
	/** How far From can go along a flat Direction before it meets a wall, or -1 if there is none within MaxDistance. */
	float WallDistance(const FVector& From, const FVector& Direction, float MaxDistance) const;
	FIntPoint RandomOpenTile() const;
	FIntPoint NearestOpenTile(FIntPoint T) const;

	EHGVariant Variant = EHGVariant::Clinic;
	FHGTheme Theme;
	int32 Width = 0;
	int32 Height = 0;
	FIntPoint PlayerTile = FIntPoint(1, 1);
	FIntPoint MonsterTile = FIntPoint(1, 1);
	FIntPoint ExitTile = FIntPoint(1, 1);
	TArray<FIntPoint> FuseTiles;
	TArray<TPair<FIntPoint, EHGItem>> ItemTiles;

private:
	void AddLamp(FIntPoint T);
	void AddProp(FIntPoint T);
	void AddExit(FIntPoint T);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Walls;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Floors;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Ceilings;

	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> Lamps;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> ExitLamp;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ExitSign;

	UPROPERTY()
	TObjectPtr<USceneComponent> WardDoor;

	UPROPERTY()
	TObjectPtr<USceneComponent> HourHand;

	UPROPERTY()
	TObjectPtr<USceneComponent> MinuteHand;

	int32 NearestSpot(const FVector& At) const;
	bool BodyFits(const FVector& A, const FVector& B) const;

	/** The walked floor: a place to stand, the four places it leads to (-1 for none), and a lookup by grid column. */
	TArray<FVector> Spots;
	TArray<int32> Links;
	TMap<FIntPoint, TArray<int32>> Columns;

	TArray<FString> Rows;
	TArray<bool> LampFlickers;
	TArray<float> LampHoldUntil;
};
