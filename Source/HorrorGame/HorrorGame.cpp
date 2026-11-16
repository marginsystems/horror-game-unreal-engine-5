#include "HGTypes.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, HorrorGame, "HorrorGame");

DEFINE_LOG_CATEGORY(LogHG);

namespace HG
{
	const TCHAR* VariantKey(EHGVariant V)
	{
		switch (V)
		{
		case EHGVariant::Office: return TEXT("office");
		case EHGVariant::Mine: return TEXT("mine");
		default: return TEXT("clinic");
		}
	}

	const TCHAR* VariantTitle(EHGVariant V)
	{
		switch (V)
		{
		case EHGVariant::Office: return TEXT("THE OFFICE  -  she can hear you");
		case EHGVariant::Mine: return TEXT("THE MINE  -  it cannot turn corners");
		default: return TEXT("THE NIGHT CLINIC  -  do not look away");
		}
	}

	EHGVariant VariantFromKey(const FString& Key)
	{
		if (Key.Equals(TEXT("office"), ESearchCase::IgnoreCase)) { return EHGVariant::Office; }
		if (Key.Equals(TEXT("mine"), ESearchCase::IgnoreCase)) { return EHGVariant::Mine; }
		return EHGVariant::Clinic;
	}

	EHGMonster MonsterFor(EHGVariant V)
	{
		switch (V)
		{
		case EHGVariant::Office: return EHGMonster::Librarian;
		case EHGVariant::Mine: return EHGMonster::Mouth;
		default: return EHGMonster::Walker;
		}
	}

	const TCHAR* ItemName(EHGItem I)
	{
		switch (I)
		{
		case EHGItem::Fuse: return TEXT("Fuse");
		case EHGItem::Pistol: return TEXT("Pistol");
		case EHGItem::Shotgun: return TEXT("Shotgun");
		case EHGItem::Chicken: return TEXT("Rubber chicken");
		case EHGItem::Grenade: return TEXT("Grenade");
		default: return TEXT("Empty hands");
		}
	}

	UStaticMesh* Shape(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name);
		return LoadObject<UStaticMesh>(nullptr, *Path);
	}

	UMaterialInstanceDynamic* Paint(UPrimitiveComponent* Comp, const FLinearColor& Color, float Glow)
	{
		static TWeakObjectPtr<UMaterialInterface> Base;
		if (!Base.IsValid())
		{
			UMaterialInterface* Found = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_HG.M_HG"), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Found)
			{
				Found = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			}
			Base = Found;
		}
		if (!Comp || !Base.IsValid())
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base.Get(), Comp);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Glow"), Glow);
		Comp->SetMaterial(0, Material);
		return Material;
	}

	UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, const TCHAR* ShapeName,
		const FVector& RelLocation, const FVector& Scale, const FLinearColor& Color,
		float Glow, bool bCollide, const FRotator& RelRotation)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Shape(ShapeName));
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(RelLocation);
		Comp->SetRelativeRotation(RelRotation);
		Comp->SetRelativeScale3D(Scale);
		Comp->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->RegisterComponent();
		Owner->AddInstanceComponent(Comp);
		Paint(Comp, Color, Glow);
		return Comp;
	}

	USceneComponent* AddPivot(AActor* Owner, USceneComponent* Parent, const FVector& RelLocation)
	{
		USceneComponent* Comp = NewObject<USceneComponent>(Owner);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(RelLocation);
		Comp->RegisterComponent();
		Owner->AddInstanceComponent(Comp);
		return Comp;
	}
}
