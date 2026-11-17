#pragma once

#include "CoreMinimal.h"

class AActor;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USceneComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogHG, Log, All);

/** The three previews. Each one is a place plus the monster that lives there. */
enum class EHGVariant : uint8 { Clinic, Office, Mine };

enum class EHGMonster : uint8 { Walker, Librarian, Mouth };

enum class EHGItem : uint8 { None, Fuse, Pistol, Shotgun, Chicken, Grenade };

struct FHGTheme
{
	FLinearColor Wall, Floor, Ceiling, Prop, Lamp, Fog;
	float LampIntensity = 2500.f;
	float FogDensity = 0.02f;
	float FlickerChance = 0.3f;
};

namespace HG
{
	const TCHAR* VariantKey(EHGVariant V);
	const TCHAR* VariantTitle(EHGVariant V);
	EHGVariant VariantFromKey(const FString& Key);
	EHGMonster MonsterFor(EHGVariant V);
	const TCHAR* ItemName(EHGItem I);

	/** Engine basic shape by name: Cube, Sphere, Cylinder, Cone or Plane. All are 100 units across. */
	UStaticMesh* Shape(const TCHAR* Name);

	/** Gives the component its own material in a flat colour. Glow needs Content/Materials/M_HG. */
	UMaterialInstanceDynamic* Paint(UPrimitiveComponent* Comp, const FLinearColor& Color, float Glow = 0.f);

	/** Creates, registers and paints a basic shape on an actor at runtime. */
	UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, const TCHAR* ShapeName,
		const FVector& RelLocation, const FVector& Scale, const FLinearColor& Color,
		float Glow = 0.f, bool bCollide = false, const FRotator& RelRotation = FRotator::ZeroRotator);

	USceneComponent* AddPivot(AActor* Owner, USceneComponent* Parent, const FVector& RelLocation);

	/** For painted things that move: pins the grime pattern to the object so it does not slide over it. */
	void MarkMoving(UPrimitiveComponent* Comp);

	/** Every sound the game plays. Each is Content/Audio/S_<name>, synthesised by Scripts/make_sounds.py. */
	TConstArrayView<const TCHAR*> SoundNames();
	USoundBase* Sound(const TCHAR* Name);

	/** Plays at a place in the world: quieter and duller with distance, panned by direction. */
	void PlayAt(const UObject* Context, const TCHAR* Name, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);
	/** Plays in the player's own head: their hands, their feet, the end of the round. */
	void Play(const UObject* Context, const TCHAR* Name, float Volume = 1.f, float Pitch = 1.f);
	/** Starts a looping sound that follows Parent around. */
	UAudioComponent* LoopOn(USceneComponent* Parent, const TCHAR* Name, float Volume = 1.f);

	/** Every Blender-built mesh the game uses. Each is Content/Models/SM_<name>, built by Scripts/make_models.py. */
	TConstArrayView<const TCHAR*> ModelNames();
	UStaticMesh* Model(const TCHAR* Name);

	/**
	 * Adds a Blender-built part to an actor. The colours travel in the material slot names
	 * (c_RRGGBB_gN: sRGB colour and glow), so the mesh needs no material assets of its own.
	 * Returns null when the model is missing; callers fall back to basic shapes.
	 */
	UStaticMeshComponent* AddModel(AActor* Owner, USceneComponent* Parent, const TCHAR* ModelName,
		const FVector& RelLocation = FVector::ZeroVector, const FRotator& RelRotation = FRotator::ZeroRotator,
		const FVector& Scale = FVector::OneVector);
}
