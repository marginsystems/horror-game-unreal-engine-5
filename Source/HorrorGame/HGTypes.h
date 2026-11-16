#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USceneComponent;
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
}
