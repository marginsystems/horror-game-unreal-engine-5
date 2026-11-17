#include "HGTypes.h"

#include "Animation/AnimSequence.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, HorrorGame, "HorrorGame");

DEFINE_LOG_CATEGORY(LogHG);

namespace
{
	UMaterialInterface* BaseMaterial()
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
		return Base.Get();
	}

	/** How far a sound carries and how it dulls on the way. One setting for the whole game. */
	USoundAttenuation* Falloff()
	{
		static TWeakObjectPtr<USoundAttenuation> Shared;
		if (!Shared.IsValid())
		{
			USoundAttenuation* Made = NewObject<USoundAttenuation>(GetTransientPackage(), TEXT("HGFalloff"));
			Made->AddToRoot();
			FSoundAttenuationSettings& S = Made->Attenuation;
			S.bAttenuate = true;
			S.bSpatialize = true;
			S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
			S.AttenuationShape = EAttenuationShape::Sphere;
			S.AttenuationShapeExtents = FVector(250.f);
			S.FalloffDistance = 4200.f;
			S.dBAttenuationAtMax = -42.f;
			S.bAttenuateWithLPF = true;
			S.LPFRadiusMin = 700.f;
			S.LPFRadiusMax = 3600.f;
			S.LPFFrequencyAtMin = 20000.f;
			S.LPFFrequencyAtMax = 1400.f;
			Shared = Made;
		}
		return Shared.Get();
	}

	/** "c_RRGGBB_gN" -> colour and glow. Anything else is painted mid grey. */
	void ReadSlotName(const FString& Slot, FLinearColor& OutColor, float& OutGlow)
	{
		OutColor = FLinearColor(0.2f, 0.2f, 0.2f);
		OutGlow = 0.f;
		TArray<FString> Parts;
		Slot.ParseIntoArray(Parts, TEXT("_"));
		if (Parts.Num() >= 2 && Parts[0] == TEXT("c") && Parts[1].Len() == 6)
		{
			OutColor = FLinearColor::FromSRGBColor(FColor::FromHex(Parts[1]));
		}
		if (Parts.Num() >= 3 && Parts[2].StartsWith(TEXT("g")))
		{
			OutGlow = FCString::Atof(*Parts[2].Mid(1));
		}
	}
}

namespace HG
{
	const TCHAR* VariantKey(EHGVariant V)
	{
		switch (V)
		{
		case EHGVariant::Office: return TEXT("office");
		case EHGVariant::Mine: return TEXT("mine");
		case EHGVariant::Hospital: return TEXT("hospital");
		case EHGVariant::Ward: return TEXT("ward");
		default: return TEXT("clinic");
		}
	}

	const TCHAR* VariantTitle(EHGVariant V)
	{
		switch (V)
		{
		case EHGVariant::Office: return TEXT("THE OFFICE  -  she can hear you");
		case EHGVariant::Mine: return TEXT("THE MINE  -  it cannot turn corners");
		case EHGVariant::Hospital: return TEXT("THE HOSPITAL  -  do not look away");
		case EHGVariant::Ward: return TEXT("1997");
		default: return TEXT("THE NIGHT CLINIC  -  do not look away");
		}
	}

	EHGVariant VariantFromKey(const FString& Key)
	{
		if (Key.Equals(TEXT("office"), ESearchCase::IgnoreCase)) { return EHGVariant::Office; }
		if (Key.Equals(TEXT("mine"), ESearchCase::IgnoreCase)) { return EHGVariant::Mine; }
		if (Key.Equals(TEXT("hospital"), ESearchCase::IgnoreCase)) { return EHGVariant::Hospital; }
		if (Key.Equals(TEXT("ward"), ESearchCase::IgnoreCase)) { return EHGVariant::Ward; }
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

	const TCHAR* MapFor(EHGVariant V)
	{
		return V == EHGVariant::Hospital ? TEXT("/Game/Horror_Hospital/Maps/DemoMap") : TEXT("/Engine/Maps/Entry");
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
		UMaterialInterface* Base = BaseMaterial();
		if (!Comp || !Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Comp);
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

	USkeletalMeshComponent* AddPerson(AActor* Owner, USceneComponent* Parent, TConstArrayView<FString> SkinPaths, float Scale)
	{
		USkeletalMesh* RigMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/HorrorPrivate/Characters/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_Standard.UAL1_Standard"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!RigMesh)
		{
			return nullptr;
		}
		USkeletalMeshComponent* Rig = NewObject<USkeletalMeshComponent>(Owner);
		Rig->SetMobility(EComponentMobility::Movable);
		Rig->SetupAttachment(Parent);
		Rig->SetSkeletalMeshAsset(RigMesh);
		Rig->SetRelativeScale3D(FVector(Scale));
		// The imported bodies face +Y; the game's people face +X.
		Rig->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		Rig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rig->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Rig->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Rig->SetVisibility(false);
		Rig->RegisterComponent();
		Owner->AddInstanceComponent(Rig);
		for (const FString& Path : SkinPaths)
		{
			USkeletalMesh* SkinMesh = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!SkinMesh)
			{
				UE_LOG(LogHG, Warning, TEXT("HGTEST MISSING person part %s"), *Path);
				continue;
			}
			USkeletalMeshComponent* Skin = NewObject<USkeletalMeshComponent>(Owner);
			Skin->SetMobility(EComponentMobility::Movable);
			Skin->SetupAttachment(Rig);
			Skin->SetSkeletalMeshAsset(SkinMesh);
			Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Skin->SetLeaderPoseComponent(Rig);
			Skin->RegisterComponent();
			Owner->AddInstanceComponent(Skin);
		}
		return Rig;
	}

	void PlayPersonClip(USkeletalMeshComponent* Rig, const TCHAR* ClipName, float Rate, bool bLoop)
	{
		if (!Rig)
		{
			return;
		}
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Characters/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_Standard%s.UAL1_Standard%s"), ClipName, ClipName);
		if (UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Rig->PlayAnimation(Sequence, bLoop);
			Rig->SetPlayRate(Rate);
		}
	}

	void MarkMoving(UPrimitiveComponent* Comp)
	{
		for (int32 i = 0; Comp && i < Comp->GetNumMaterials(); ++i)
		{
			if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(i)))
			{
				Material->SetScalarParameterValue(TEXT("Local"), 1.f);
			}
		}
	}

	TConstArrayView<const TCHAR*> SoundNames()
	{
		static const TCHAR* Names[] = {
			TEXT("step_1"), TEXT("step_2"), TEXT("step_3"), TEXT("pistol"), TEXT("pistol_back"), TEXT("shotgun"),
			TEXT("chicken"), TEXT("pin"), TEXT("boom"), TEXT("light_off"), TEXT("light_on"), TEXT("smack"),
			TEXT("pickup"), TEXT("fuse"), TEXT("power"), TEXT("caught"), TEXT("win"), TEXT("scream"), TEXT("heart"),
			TEXT("blink"), TEXT("question"), TEXT("gary_step"), TEXT("gary_freeze"), TEXT("shush"), TEXT("heels"),
			TEXT("squelch"), TEXT("mouth_loop"), TEXT("mouth_roar"), TEXT("bonk"),
			TEXT("amb_clinic"), TEXT("amb_office"), TEXT("amb_mine"), TEXT("tick"),
		};
		return Names;
	}

	USoundBase* Sound(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Audio/S_%s.S_%s"), Name, Name);
		return LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	void PlayAt(const UObject* Context, const TCHAR* Name, const FVector& Location, float Volume, float Pitch)
	{
		if (USoundBase* Found = Sound(Name))
		{
			UGameplayStatics::PlaySoundAtLocation(Context, Found, Location, Volume, Pitch, 0.f, Falloff());
		}
	}

	void Play(const UObject* Context, const TCHAR* Name, float Volume, float Pitch)
	{
		if (USoundBase* Found = Sound(Name))
		{
			UGameplayStatics::PlaySound2D(Context, Found, Volume, Pitch);
		}
	}

	void Voice(const UObject* Context, const TCHAR* Name, const FVector& Location, float Volume)
	{
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Audio/V_%s.V_%s"), Name, Name);
		if (USoundBase* Found = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			UGameplayStatics::PlaySoundAtLocation(Context, Found, Location, Volume, 1.f, 0.f, Falloff());
		}
	}

	UAudioComponent* VoiceBed(const UObject* Context, const TCHAR* Name, float Volume)
	{
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Audio/V_%s.V_%s"), Name, Name);
		USoundBase* Found = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		return Found ? UGameplayStatics::SpawnSound2D(Context, Found, Volume) : nullptr;
	}

	static USoundBase* ExtraSound(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Audio/X_%s.X_%s"), Name, Name);
		return LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	bool Extra(const UObject* Context, const TCHAR* Name, const FVector& Location, float Volume, float Pitch)
	{
		USoundBase* Found = ExtraSound(Name);
		if (Found)
		{
			UGameplayStatics::PlaySoundAtLocation(Context, Found, Location, Volume, Pitch, 0.f, Falloff());
		}
		return Found != nullptr;
	}

	UAudioComponent* ExtraBed(const UObject* Context, const TCHAR* Name, float Volume)
	{
		USoundBase* Found = ExtraSound(Name);
		return Found ? UGameplayStatics::SpawnSound2D(Context, Found, Volume) : nullptr;
	}

	UAudioComponent* ExtraAt(const UObject* Context, const TCHAR* Name, const FVector& Location, float Volume)
	{
		USoundBase* Found = ExtraSound(Name);
		return Found ? UGameplayStatics::SpawnSoundAtLocation(Context, Found, Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, Falloff()) : nullptr;
	}

	UAudioComponent* LoopOn(USceneComponent* Parent, const TCHAR* Name, float Volume)
	{
		USoundBase* Found = Sound(Name);
		if (!Found || !Parent)
		{
			return nullptr;
		}
		return UGameplayStatics::SpawnSoundAttached(Found, Parent, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, true, Volume, 1.f, 0.f, Falloff());
	}

	TConstArrayView<const TCHAR*> ModelNames()
	{
		static const TCHAR* Names[] = {
			TEXT("Gary_Body"), TEXT("Gary_Head"), TEXT("Gary_Arm"),
			TEXT("Librarian_Body"), TEXT("Librarian_Head"), TEXT("Librarian_Arm"), TEXT("Librarian_Shush"),
			TEXT("Mouth_Top"), TEXT("Mouth_Bottom"),
			TEXT("Toy_Sorter"), TEXT("Toy_Circle"), TEXT("Toy_Square"), TEXT("Toy_Triangle"),
			TEXT("Deco_Rainbow"), TEXT("Deco_Star"), TEXT("Clock_Face"), TEXT("Clock_Hand"),
		};
		return Names;
	}

	UStaticMesh* Model(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Models/SM_%s.SM_%s"), Name, Name);
		return LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UStaticMeshComponent* AddModel(AActor* Owner, USceneComponent* Parent, const TCHAR* ModelName,
		const FVector& RelLocation, const FRotator& RelRotation, const FVector& Scale)
	{
		UStaticMesh* Mesh = Model(ModelName);
		UMaterialInterface* Base = BaseMaterial();
		if (!Mesh || !Base)
		{
			return nullptr;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh);
		Comp->SetupAttachment(Parent);
		Comp->SetRelativeLocation(RelLocation);
		Comp->SetRelativeRotation(RelRotation);
		Comp->SetRelativeScale3D(Scale);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->RegisterComponent();
		Owner->AddInstanceComponent(Comp);

		const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			FLinearColor Color;
			float Glow = 0.f;
			ReadSlotName(Slots[i].MaterialSlotName.ToString(), Color, Glow);
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Comp);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
			Material->SetScalarParameterValue(TEXT("Glow"), Glow);
			Material->SetScalarParameterValue(TEXT("Local"), 1.f);
			Comp->SetMaterial(i, Material);
		}
		return Comp;
	}
}
