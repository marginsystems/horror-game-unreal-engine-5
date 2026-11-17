#include "HGGameMode.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ReferenceSkeleton.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/SlateFontInfo.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HGActors.h"
#include "HGLevel.h"
#include "HGMonster.h"
#include "HGPersonAnim.h"
#include "HGPlayer.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "Sound/SoundBase.h"
#include "UnrealClient.h"

AHGGameMode::AHGGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AHGPlayer::StaticClass();
	HUDClass = AHGHUD::StaticClass();
}

void AHGGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	FString Key = UGameplayStatics::ParseOption(Options, TEXT("Variant"));
	if (Key.IsEmpty())
	{
		FParse::Value(FCommandLine::Get(), TEXT("Variant="), Key);
	}
	Variant = HG::VariantFromKey(Key);
	bBot = FParse::Param(FCommandLine::Get(), TEXT("HGBot"));
	bShots = FParse::Param(FCommandLine::Get(), TEXT("HGShots"));
	bCatch = FParse::Param(FCommandLine::Get(), TEXT("HGCatch"));

	// One notch below the engine's top settings, drawn at three quarters size and scaled up: it looks
	// the same at a glance and runs far smoother. -HGFull keeps everything at full.
	if (!FParse::Param(FCommandLine::Get(), TEXT("HGFull")))
	{
		for (const TCHAR* Command : { TEXT("sg.GlobalIlluminationQuality 2"), TEXT("sg.ReflectionQuality 2"), TEXT("sg.ShadowQuality 2"),
			TEXT("sg.PostProcessQuality 2"), TEXT("sg.EffectsQuality 2"), TEXT("r.ScreenPercentage 75") })
		{
			GEngine->Exec(GetWorld(), Command);
		}
	}

	// Engine notices ("Preparing mesh distance fields...") have no place on a horror game's screen.
	GEngine->bEnableOnScreenDebugMessages = false;

	// Load every sound and model up front. A missing one is reported, and fails the self-test.
	for (const TCHAR* Name : HG::SoundNames())
	{
		if (USoundBase* Found = HG::Sound(Name)) { KeptSounds.Add(Found); }
		else { UE_LOG(LogHG, Warning, TEXT("HGTEST MISSING sound %s"), Name); }
	}
	for (const TCHAR* Name : HG::ModelNames())
	{
		if (UStaticMesh* Found = HG::Model(Name)) { KeptModels.Add(Found); }
		else { UE_LOG(LogHG, Warning, TEXT("HGTEST MISSING model %s"), Name); }
	}
	bAssetsComplete = KeptSounds.Num() == HG::SoundNames().Num() && KeptModels.Num() == HG::ModelNames().Num();
	UE_LOG(LogHG, Log, TEXT("HGTEST ASSETS %s sounds=%d/%d models=%d/%d"), bAssetsComplete ? TEXT("PASS") : TEXT("FAIL"),
		KeptSounds.Num(), HG::SoundNames().Num(), KeptModels.Num(), HG::ModelNames().Num());

	UWorld* World = GetWorld();
	FActorSpawnParameters Always;
	Always.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Level = World->SpawnActor<AHGLevel>(FVector::ZeroVector, FRotator::ZeroRotator, Always);
	if (Variant == EHGVariant::Ward)
	{
		// Scene one of the story: one bright room, built here; no monster, no fuses.
		Level->BuildWard();
		const FVector Start(150, -210, 70);
		StartSpot = World->SpawnActor<APlayerStart>(Start, (FVector(40, -40, 40) - Start).Rotation(), Always);
		PopulateWard();
		UE_LOG(LogHG, Log, TEXT("HGTEST START variant=%s (story scene)"), HG::VariantKey(Variant));
		return;
	}
	if (Variant == EHGVariant::Hospital)
	{
		// A ready-made map. Its floor can only be walked once the map's collision exists, so the
		// round is laid out in StartPlay; the player starts wherever the map says.
		UE_LOG(LogHG, Log, TEXT("HGTEST START variant=%s (ready-made map)"), HG::VariantKey(Variant));
		return;
	}
	Level->Build(Variant);
	FusesTotal = Level->FuseTiles.Num();

	// Face the player down the longest open run from the start tile.
	static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) };
	int32 BestRun = -1;
	float StartYaw = 0.f;
	for (int32 i = 0; i < 4; ++i)
	{
		int32 Run = 0;
		for (FIntPoint T = Level->PlayerTile + Dirs[i]; Level->IsOpen(T); T += Dirs[i])
		{
			++Run;
		}
		if (Run > BestRun)
		{
			BestRun = Run;
			StartYaw = i * 90.f;
		}
	}
	StartSpot = World->SpawnActor<APlayerStart>(Level->ToWorld(Level->PlayerTile, 100.f), FRotator(0.f, StartYaw, 0.f), Always);

	for (const FIntPoint& T : Level->FuseTiles)
	{
		if (AHGPickup* Pickup = World->SpawnActor<AHGPickup>(Level->ToWorld(T, 75.f), FRotator::ZeroRotator, Always))
		{
			Pickup->Setup(EHGItem::Fuse);
		}
	}
	for (const TPair<FIntPoint, EHGItem>& Entry : Level->ItemTiles)
	{
		if (AHGPickup* Pickup = World->SpawnActor<AHGPickup>(Level->ToWorld(Entry.Key, 75.f), FRotator::ZeroRotator, Always))
		{
			Pickup->Setup(Entry.Value);
		}
	}

	Monster = World->SpawnActor<AHGMonster>(Level->ToWorld(Level->MonsterTile), FRotator::ZeroRotator, Always);
	Monster->Setup(HG::MonsterFor(Variant), Level);

	if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector(0, 0, 0), FRotator::ZeroRotator, Always))
	{
		UExponentialHeightFogComponent* FogComp = Fog->GetComponent();
		FogComp->SetMobility(EComponentMobility::Movable);
		FogComp->SetFogDensity(Level->Theme.FogDensity);
		FogComp->SetFogHeightFalloff(0.001f);
		FogComp->SetFogInscatteringColor(Level->Theme.Fog);
		FogComp->SetVolumetricFog(true);
		FogComp->SetVolumetricFogScatteringDistribution(0.5f);
		FogComp->SetVolumetricFogExtinctionScale(1.2f);
	}

	UE_LOG(LogHG, Log, TEXT("HGTEST START variant=%s monster=%s fuses=%d bot=%d shots=%d"),
		HG::VariantKey(Variant), Monster->DisplayName(), FusesTotal, bBot ? 1 : 0, bShots ? 1 : 0);
}

AActor* AHGGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	return StartSpot ? StartSpot.Get() : Super::ChoosePlayerStart_Implementation(Player);
}

void AHGGameMode::StartPlay()
{
	Super::StartPlay();
	if (Variant == EHGVariant::Hospital)
	{
		PopulateWorldMap();
	}
	if (Variant != EHGVariant::Ward)
	{
		Say(FString::Printf(TEXT("Find %d fuses. Power the exit shutter. Leave."), FusesTotal), 7.f);
	}
	else
	{
		// The ward's own sound, when the private assets have it: a quiet hospital room, birds outside the
		// window, a wall clock. A music box plays over the opening shots.
		HG::ExtraBed(this, TEXT("ward_room"), 0.7f);
		HG::ExtraBed(this, TEXT("ward_birds"), 0.5f);
		bRealClock = HG::ExtraAt(this, TEXT("clock"), Level->ClockSpot(), 1.f) != nullptr;
		if (!bBot)
		{
			HG::ExtraBed(this, TEXT("musicbox"), 0.55f);
			BeginCutscene(Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(this, 0)));
		}
	}

	// The room tone for this place, looping under everything else.
	const FString Ambience = FString::Printf(TEXT("amb_%s"), HG::VariantKey(Variant));
	if (USoundBase* Found = HG::Sound(*Ambience))
	{
		UGameplayStatics::SpawnSound2D(this, Found, 0.55f);
	}
}

void AHGGameMode::PopulateWorldMap()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Always;
	Always.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	// A kit's demo map has no player start, so the player arrives at the world origin, which may be
	// outside the building. Put them on a floor inside it.
	const bool bHasStart = static_cast<bool>(TActorIterator<APlayerStart>(World));
	FVector Inside;
	if (Pawn && !bHasStart && Level->FindStandableSpot(Inside))
	{
		Pawn->SetActorLocation(Inside + FVector(0, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
	}
	Level->BuildFromWorld(Pawn ? Pawn->GetActorLocation() : FVector(0, 0, 200));
	FusesTotal = Level->FuseSpots.Num();

	for (const FVector& Spot : Level->FuseSpots)
	{
		if (AHGPickup* Pickup = World->SpawnActor<AHGPickup>(Spot + FVector(0, 0, 75), FRotator::ZeroRotator, Always))
		{
			Pickup->Setup(EHGItem::Fuse);
		}
	}
	for (const TPair<FVector, EHGItem>& Entry : Level->ItemSpots)
	{
		if (AHGPickup* Pickup = World->SpawnActor<AHGPickup>(Entry.Key + FVector(0, 0, 75), FRotator::ZeroRotator, Always))
		{
			Pickup->Setup(Entry.Value);
		}
	}
	Monster = World->SpawnActor<AHGMonster>(Level->MonsterSpot, FRotator::ZeroRotator, Always);
	Monster->Setup(HG::MonsterFor(Variant), Level);
	UE_LOG(LogHG, Log, TEXT("HGTEST START variant=%s monster=%s fuses=%d"), HG::VariantKey(Variant), Monster->DisplayName(), FusesTotal);
}

void AHGGameMode::PopulateWard()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Always;
	Always.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// The sorter in the middle of the rug, its three shapes scattered round it.
	WardSorter = World->SpawnActor<AHGSorter>(FVector(60, -70, 2), FRotator(0.f, 25.f, 0.f), Always);
	WardSorter->Setup();
	const TPair<const TCHAR*, FVector> Shapes[] = {
		{ TEXT("Circle"), FVector(135, -125, 2) }, { TEXT("Square"), FVector(-5, -150, 2) }, { TEXT("Triangle"), FVector(110, 5, 2) },
	};
	for (const TPair<const TCHAR*, FVector>& Shape : Shapes)
	{
		AHGToy* Toy = World->SpawnActor<AHGToy>(Shape.Value, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Always);
		Toy->SetupModel(FName(*FString(Shape.Key).ToLower()), *FString::Printf(TEXT("Toy_%s"), Shape.Key));
		WardShapes.Add(Toy);
	}

	// Everything below comes from the private assets plugin and is simply absent without it.
	USceneComponent* Room = Level->GetRootComponent();

	// The bed, along the window wall. The pack's bed comes in three pieces, a hundred times too big.
	bool bBed = false;
	for (const TCHAR* Part : { TEXT("bed_bed_0"), TEXT("blanket_blanket_0"), TEXT("mattress_mattress_0") })
	{
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Ward/Bed/old_hospital_bed_pbrgr/StaticMeshes/%s.%s"), Part, Part);
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Level);
			Piece->SetMobility(EComponentMobility::Movable);
			Piece->SetStaticMesh(Mesh);
			Piece->SetupAttachment(Room);
			Piece->SetRelativeLocation(FVector(-185, 100, 3));
			Piece->SetRelativeScale3D(FVector(0.01f));
			Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Piece->RegisterComponent();
			Level->AddInstanceComponent(Piece);
			bBed = true;
		}
	}
	if (bBed)
	{
		// Something solid where the bed is, so nobody walks through it.
		HG::AddShape(Level, Room, TEXT("Cube"), FVector(-185, 200, 28), FVector(1.3f, 2.1f, 0.56f), FLinearColor::Black, 0.f, true)->SetVisibility(false);
	}

	// Old toys: a plush rabbit, a tin robot, a train, a ball, a boat, wooden blocks.
	struct FPackToy { const TCHAR* Kind; const TCHAR* Mesh; FVector At; };
	const FPackToy Toys[] = {
		{ TEXT("rabbit"), TEXT("SM_ToyBunny"), FVector(-40, 20, 1) }, { TEXT("robot"), TEXT("SM_ToyRobot"), FVector(170, -15, 1) },
		{ TEXT("train"), TEXT("SM_ToyTrain"), FVector(5, -225, 1) }, { TEXT("ball"), TEXT("SM_Ball"), FVector(190, -175, 1) },
		{ TEXT("boat"), TEXT("SM_ToyBoat"), FVector(-80, -95, 1) }, { TEXT("block"), TEXT("SM_ToyCube_01a"), FVector(108, -212, 1) },
		{ TEXT("block"), TEXT("SM_ToyCube_02a"), FVector(86, -190, 1) },
	};
	for (const FPackToy& Entry : Toys)
	{
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Ward/Toys/%s.%s"), Entry.Mesh, Entry.Mesh);
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			AHGToy* Toy = World->SpawnActor<AHGToy>(Entry.At, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Always);
			Toy->SetupMesh(FName(Entry.Kind), Mesh, 1.f);
			if (FName(Entry.Kind) == FName(TEXT("rabbit")))
			{
				WardRabbit = Toy;
			}
		}
	}

	// The people are MetaHumans kept in the private plugin (docs/METAHUMAN.md). Their bodies take
	// Epic's mannequin clips as they are; face, hair and clothes follow the body.
	for (const TCHAR* ClipPath : {
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"),
		TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Stand_Idle_Loop.AS_MH_Neutral_Stand_Idle_Loop") })
	{
		PersonIdle = PersonIdle ? PersonIdle.Get() : LoadObject<UAnimSequence>(nullptr, ClipPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	for (const TCHAR* ClipPath : {
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd"),
		TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Walk_Loop_F.AS_MH_Neutral_Walk_Loop_F") })
	{
		PersonWalk = PersonWalk ? PersonWalk.Get() : LoadObject<UAnimSequence>(nullptr, ClipPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	// Mom, standing by the bed, watching him play. Without the plugin her voice is still in the
	// room, from where she would stand.
	UHGPersonAnim* Anim = nullptr;
	WardMom = SpawnPerson(TEXT("/HorrorPrivate/MetaHumans/Mom/BP_Mom.BP_Mom_C"), MomSpot, (FVector(60, -70, 0) - MomSpot).Rotation().Yaw, Anim);
	MomAnim = Anim;
	if (WardMom)
	{
		if (MomAnim)
		{
			MomAnim->SetClip(PersonIdle);
			UE_LOG(LogHG, Log, TEXT("HGTEST MOM clip=%s"), PersonIdle ? *PersonIdle->GetName() : TEXT("none"));
		}
		// The window is behind her and her fringe shades her eyes, so her face sat in shadow and
		// read as stern. A small warm lamp in front of it, casting no shadow, lets him see her.
		UPointLightComponent* Fill = NewObject<UPointLightComponent>(WardMom);
		Fill->SetupAttachment(WardMom->GetRootComponent());
		Fill->SetRelativeLocation(FVector(0, 55, 150));
		Fill->SetIntensityUnits(ELightUnits::Unitless);
		Fill->SetIntensity(MomFillIntensity);
		Fill->SetLightColor(FLinearColor(1.f, 0.86f, 0.72f));
		Fill->SetAttenuationRadius(150.f);
		Fill->SetCastShadows(false);
		Fill->RegisterComponent();
		WardMom->AddInstanceComponent(Fill);
		TArray<UPrimitiveComponent*> Pieces;
		WardMom->GetComponents(Pieces);
		UE_LOG(LogHG, Log, TEXT("HGTEST MOM spawned parts=%d"), Pieces.Num());
	}

	// The two doctors wait a long way down the hall, behind the closed door, until ten.
	static const TCHAR* DoctorClasses[] = {
		TEXT("/HorrorPrivate/MetaHumans/DoctorWoman/BP_DoctorWoman.BP_DoctorWoman_C"),
		TEXT("/HorrorPrivate/MetaHumans/DoctorMan/BP_DoctorMan.BP_DoctorMan_C") };
	static const FVector DoctorStarts[] = { FVector(430, 790, 0), FVector(480, 880, 0) };
	for (int32 i = 0; i < 2; ++i)
	{
		UHGPersonAnim* DoctorAnim = nullptr;
		if (AActor* Doctor = SpawnPerson(DoctorClasses[i], DoctorStarts[i], -90.f, DoctorAnim))
		{
			if (DoctorAnim)
			{
				DoctorAnim->SetClip(PersonIdle);
			}
			WardDoctors.Add(Doctor);
			DoctorAnims.Add(DoctorAnim);
		}
	}
	UE_LOG(LogHG, Log, TEXT("HGTEST DOCTORS spawned=%d"), WardDoctors.Num());
}

AActor* AHGGameMode::SpawnPerson(const TCHAR* ClassPath, const FVector& At, float FacingYaw, UHGPersonAnim*& OutAnim)
{
	OutAnim = nullptr;
	UClass* Class = LoadClass<AActor>(nullptr, ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Class)
	{
		return nullptr;
	}
	FActorSpawnParameters Always;
	Always.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Person = GetWorld()->SpawnActor<AActor>(Class, At, FRotator(0.f, FacingYaw + MomYawOffset, 0.f), Always);
	if (!Person)
	{
		return nullptr;
	}
	// Their own collision is a ragdoll's worth of small bodies; one plain capsule is what he bumps into.
	TArray<UPrimitiveComponent*> Pieces;
	Person->GetComponents(Pieces);
	for (UPrimitiveComponent* Piece : Pieces)
	{
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	UCapsuleComponent* Block = NewObject<UCapsuleComponent>(Person);
	Block->InitCapsuleSize(24.f, 84.f);
	Block->SetupAttachment(Person->GetRootComponent());
	Block->SetRelativeLocation(FVector(0, 0, 84));
	Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Block->SetCollisionResponseToAllChannels(ECR_Block);
	Block->RegisterComponent();
	Person->AddInstanceComponent(Block);

	TArray<USkeletalMeshComponent*> Meshes;
	Person->GetComponents(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
		UE_LOG(LogHG, Log, TEXT("HGDEBUG part=%s mesh=%s anim=%s leader=%s post=%s morphs=%d"), *Mesh->GetName(), Asset ? *Asset->GetName() : TEXT("none"),
			Mesh->GetAnimClass() ? *Mesh->GetAnimClass()->GetName() : TEXT("none"), Mesh->LeaderPoseComponent.IsValid() ? *Mesh->LeaderPoseComponent->GetName() : TEXT("none"),
			Asset && Asset->GetPostProcessAnimBlueprint() ? *Asset->GetPostProcessAnimBlueprint()->GetName() : TEXT("none"), Asset ? Asset->GetMorphTargets().Num() : 0);
		if (Mesh->GetName() == TEXT("Body"))
		{
			Mesh->SetAnimInstanceClass(UHGPersonAnim::StaticClass());
			OutAnim = Cast<UHGPersonAnim>(Mesh->GetAnimInstance());
		}
	}
	return Person;
}

void AHGGameMode::TickMom(float Dt, const AHGPlayer* Player, float SecondsToTen)
{
	if (!WardMom || HandStep >= 2)
	{
		// From the moment she goes to pick him up, the handover moves her.
		return;
	}
	// She watches him play, and now and then the door. More often as ten gets close; once it opens, only the door.
	MomNextGlance -= Dt;
	if (MomNextGlance <= 0.f)
	{
		MomGlance = 2.6f;
		MomNextGlance = SecondsToTen < 60.f ? FMath::FRandRange(5.f, 8.f) : FMath::FRandRange(12.f, 18.f);
	}
	MomGlance = FMath::Max(0.f, MomGlance - Dt);
	const FVector Door(280, 150, 150);
	const FVector Child = Player ? Player->GetActorLocation() + FVector(0, 0, 20) : FVector(60, -70, 80);
	const bool bDoorOpen = WardStep == 10;

	// Her body follows him only when he has gone well round her, and then stays put: a person
	// standing by a bed, not a turret. When the door opens she turns to it.
	const float Facing = WardMom->GetActorRotation().Yaw - MomYawOffset;
	const float BodyWant = ((bDoorOpen ? Door : Child) - MomSpot).Rotation().Yaw;
	const float BodyOff = FMath::Abs(FMath::FindDeltaAngleDegrees(Facing, BodyWant));
	bMomTurning = bMomTurning ? BodyOff > 6.f : BodyOff > 48.f;
	if (bMomTurning)
	{
		const FRotator Want(0.f, BodyWant + MomYawOffset, 0.f);
		WardMom->SetActorRotation(FMath::RInterpTo(WardMom->GetActorRotation(), Want, Dt, 1.8f));
	}

	// Her head does the looking: down at him, and for a glance, round to the door.
	if (MomAnim)
	{
		const FVector Eyes = MomSpot + FVector(0, 0, 155);
		const FRotator Look = (((MomGlance > 0.f || bDoorOpen) ? Door : Child) - Eyes).Rotation();
		// Only part of the way down: a full bow to a child at her feet hid her face behind her hair.
		MomAnim->LookTowards(FMath::FindDeltaAngleDegrees(Facing, Look.Yaw), FMath::Clamp(Look.Pitch * 0.4f, -12.f, 12.f));
	}
}

void AHGGameMode::TickDoctors(float Dt, const AHGPlayer* Player)
{
	if (WardStep != 10 || HandStep >= 5)
	{
		return;
	}
	// They come along the hall as their shoes are heard, she first, and stop in the doorway looking in at him.
	static const FVector Stops[] = { FVector(338, 152, 0), FVector(418, 222, 0) };
	const FVector Child = Player ? Player->GetActorLocation() + FVector(0, 0, 20) : FVector(60, -70, 80);
	for (int32 i = 0; i < WardDoctors.Num(); ++i)
	{
		AActor* Doctor = WardDoctors[i];
		UHGPersonAnim* Anim = DoctorAnims.IsValidIndex(i) ? DoctorAnims[i].Get() : nullptr;
		if (!Doctor || WardTimer < 2.2f + 0.8f * i)
		{
			continue;
		}
		const FVector At = Doctor->GetActorLocation();
		FVector To = Stops[i] - At;
		To.Z = 0.f;
		const float Left = To.Size();
		const bool bWalking = Left > 3.f;
		const FVector Ahead = bWalking ? To : (Child - At);
		if (bWalking)
		{
			Doctor->SetActorLocation(At + To / Left * FMath::Min(Left, DoctorSpeed * Dt));
		}
		const FRotator Want(0.f, Ahead.Rotation().Yaw + MomYawOffset, 0.f);
		Doctor->SetActorRotation(FMath::RInterpTo(Doctor->GetActorRotation(), Want, Dt, bWalking ? 6.f : 2.5f));
		if (Anim)
		{
			Anim->SetClip(bWalking ? PersonWalk.Get() : PersonIdle.Get(), bWalking ? 0.8f : 1.f);
			const float Facing = Doctor->GetActorRotation().Yaw - MomYawOffset;
			const FRotator Look = (Child - (At + FVector(0, 0, 160))).Rotation();
			// Their eyes are on him from the moment the doorway lets them see him.
			const bool bSees = Left < 260.f;
			Anim->LookTowards(bSees ? FMath::FindDeltaAngleDegrees(Facing, Look.Yaw) : 0.f, bSees ? FMath::Clamp(Look.Pitch * 0.6f, -18.f, 18.f) : 0.f);
		}
	}
}

FVector AHGGameMode::PersonPoint(const AActor* Person, float Ahead, float Left, float Up) const
{
	const FRotationMatrix Axes(FRotator(0.f, Person->GetActorRotation().Yaw - MomYawOffset, 0.f));
	return Person->GetActorLocation() + Axes.GetUnitAxis(EAxis::X) * Ahead - Axes.GetUnitAxis(EAxis::Y) * Left + FVector(0.f, 0.f, Up);
}

bool AHGGameMode::WalkPerson(AActor* Person, UHGPersonAnim* Anim, const FVector& To, float Speed, float ClipRate, float Dt)
{
	const FVector At = Person->GetActorLocation();
	FVector Step = To - At;
	Step.Z = 0.f;
	const float Left = Step.Size();
	const bool bWalking = Left > 3.f;
	if (bWalking)
	{
		Person->SetActorLocation(At + Step / Left * FMath::Min(Left, Speed * Dt));
		const FRotator Want(0.f, Step.Rotation().Yaw + MomYawOffset, 0.f);
		Person->SetActorRotation(FMath::RInterpTo(Person->GetActorRotation(), Want, Dt, 5.f));
	}
	if (Anim)
	{
		Anim->SetClip(bWalking ? PersonWalk.Get() : PersonIdle.Get(), bWalking ? ClipRate : 1.f);
	}
	return !bWalking;
}

void AHGGameMode::FacePerson(AActor* Person, const FVector& Point, float Dt, float Speed) const
{
	FVector To = Point - Person->GetActorLocation();
	To.Z = 0.f;
	if (To.SizeSquared() > 25.f)
	{
		const FRotator Want(0.f, To.Rotation().Yaw + MomYawOffset, 0.f);
		Person->SetActorRotation(FMath::RInterpTo(Person->GetActorRotation(), Want, Dt, Speed));
	}
}

void AHGGameMode::PersonLooks(const AActor* Person, UHGPersonAnim* Anim, const FVector& Target, float PitchShare) const
{
	if (!Anim)
	{
		return;
	}
	const float Facing = Person->GetActorRotation().Yaw - MomYawOffset;
	const FRotator Look = (Target - (Person->GetActorLocation() + FVector(0.f, 0.f, 158.f))).Rotation();
	Anim->LookTowards(FMath::FindDeltaAngleDegrees(Facing, Look.Yaw), FMath::Clamp(Look.Pitch * PitchShare, -24.f, 24.f));
}

void AHGGameMode::TickHandover(float Dt, AHGPlayer* Player)
{
	AActor* Woman = WardDoctors[0];
	AActor* Man = WardDoctors[1];
	UHGPersonAnim* WomanAnim = DoctorAnims[0];
	UHGPersonAnim* ManAnim = DoctorAnims[1];
	HandTimer += Dt;
	auto Next = [this]() { ++HandStep; HandTimer = 0.f; };
	auto Shot = [this](int32 Bit, const TCHAR* Name)
	{
		if (bShots && !(HandShots & (1 << Bit)))
		{
			HandShots |= 1 << Bit;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / Name, true, false);
			UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), Name);
		}
	};
	const FVector Eyes = Player->EyeLocation();
	// Where his eyes are in each pair of arms: in front of mom's chest, looking up at her; then on
	// the doctor's shoulder, looking back the way they came.
	const FVector MomHold = PersonPoint(WardMom, 50.f, 8.f, 122.f);
	const FVector DoctorHold = PersonPoint(Woman, 8.f, 27.f, 148.f);
	const FVector MomFace = PersonPoint(WardMom, 9.f, 0.f, 153.f);
	const FVector WomanFace = PersonPoint(Woman, 9.f, 0.f, 160.f);
	const FVector ManFace = PersonPoint(Man, 9.f, 0.f, 166.f);
	const FVector WomanAt = Woman->GetActorLocation();

	switch (HandStep)
	{
	case 0:
		// Both of them are in the doorway. She speaks to mom, not to him.
		if (WardTimer > 10.5f)
		{
			Say(TEXT("\"Good morning. We're ready for him now.\""), 4.5f);
			HG::Voice(this, TEXT("doc_ready"), WomanFace);
			Next();
		}
		break;
	case 1:
		if (HandTimer > 3.4f)
		{
			// His legs stop answering here. From now on he can only look.
			Player->SetCarried(true);
			const FVector MomAt = WardMom->GetActorLocation();
			const FVector ChildAt(Player->GetActorLocation().X, Player->GetActorLocation().Y, 0.f);
			FVector Gap = MomAt - ChildAt;
			Gap.Z = 0.f;
			const float Apart = Gap.Size();
			HandMomStop = Apart > 60.f ? ChildAt + Gap / Apart * 52.f : MomAt;
			Next();
		}
		break;
	case 2:
	{
		// Mom comes over to him.
		const bool bThere = WalkPerson(WardMom, MomAnim, HandMomStop, 112.f, 0.85f, Dt);
		PersonLooks(WardMom, MomAnim, Eyes, 0.5f);
		if (bThere)
		{
			Say(TEXT("\"Come here, sweetheart. It's all right. I'll be right here.\""), 5.5f);
			HG::Voice(this, TEXT("mom_lift"), MomFace);
			if (MomAnim)
			{
				MomAnim->SetHold(1.f);
			}
			HandFrom = Eyes;
			Next();
		}
		break;
	}
	case 3:
	{
		// She picks him up: the floor drops away and her face is all there is.
		FacePerson(WardMom, HandFrom, Dt, 3.f);
		const float Lift = FMath::SmoothStep(0.f, 1.f, (HandTimer - 0.5f) / 2.1f);
		Player->CarryEyesTo(FMath::Lerp(HandFrom, MomHold, Lift));
		Player->SetLens(FMath::Lerp(92.f, 72.f, Lift));
		if (HandTimer < 3.2f || bShots)
		{
			Player->TurnTowards(MomFace, Dt, 2.2f);
		}
		PersonLooks(WardMom, MomAnim, Eyes, 0.35f);
		if (HandTimer > 4.f) { Shot(0, TEXT("ward-7-lifted.png")); }
		if (HandTimer > 5.2f)
		{
			Next();
		}
		break;
	}
	case 4:
	{
		// She carries him to the door. He sees who is waiting there.
		const bool bThere = WalkPerson(WardMom, MomAnim, FVector(226, 150, 0), 78.f, 0.62f, Dt);
		Player->CarryEyesTo(MomHold + FVector(0.f, 0.f, FMath::Sin(HandTimer * 5.5f) * 1.1f));
		if (HandTimer < 1.8f || bShots)
		{
			Player->TurnTowards(WomanFace, Dt, 1.6f);
		}
		PersonLooks(WardMom, MomAnim, WomanFace, 0.5f);
		if (HandTimer > 1.7f) { Shot(1, TEXT("ward-8-carried.png")); }
		if (bThere)
		{
			Say(TEXT("\"There we are. Come along, little one. It will all be over very soon.\""), 6.f);
			HG::Voice(this, TEXT("doc_take"), WomanFace);
			if (WomanAnim)
			{
				WomanAnim->SetClip(PersonIdle);
				WomanAnim->SetHold(1.f);
			}
			Next();
		}
		break;
	}
	case 5:
	{
		// From her arms into the doctor's. He looks back at mom the whole way across.
		FacePerson(WardMom, WomanAt, Dt, 2.5f);
		FacePerson(Woman, WardMom->GetActorLocation(), Dt, 2.5f);
		const float Pass = FMath::SmoothStep(0.f, 1.f, (HandTimer - 1.2f) / 2.3f);
		Player->CarryEyesTo(FMath::Lerp(MomHold, DoctorHold, Pass));
		Player->SetLens(FMath::Lerp(72.f, 82.f, Pass));
		if (MomAnim && Pass > 0.7f)
		{
			MomAnim->SetHold(0.f);
		}
		if ((HandTimer > 1.4f && HandTimer < 4.2f) || bShots)
		{
			Player->TurnTowards(MomFace, Dt, 1.5f);
		}
		PersonLooks(WardMom, MomAnim, Eyes, 0.6f);
		PersonLooks(Woman, WomanAnim, Eyes, 0.7f);
		if (WalkPerson(Man, ManAnim, FVector(505, 300, 0), 70.f, 0.6f, Dt))
		{
			FacePerson(Man, Eyes, Dt, 2.5f);
		}
		PersonLooks(Man, ManAnim, Eyes, 0.6f);
		if (HandTimer > 1.5f && !bCried)
		{
			// He cries for her. The sound is only there once a recording of it is in the private plugin.
			bCried = true;
			HG::ExtraBed(this, TEXT("child_cry"), 0.7f);
		}
		if (HandTimer > 3.9f) { Shot(2, TEXT("ward-9-handover.png")); }
		if (HandTimer > 5.f)
		{
			HG::ExtraBed(this, TEXT("steps_adult"), 0.5f);
			Next();
		}
		break;
	}
	default:
	{
		// She turns and carries him out and along the hall to the right. Mom comes as far as the
		// doorway and no further. The other doctor falls in behind, where he cannot help seeing him.
		static const FVector WomanPath[] = { FVector(402, 430, 0), FVector(430, 1340, 0) };
		static const FVector ManPath[] = { FVector(446, 500, 0), FVector(432, 1175, 0) };
		if (WalkPerson(Woman, WomanAnim, WomanPath[HandLeg], 92.f, 0.72f, Dt) && HandLeg < 1)
		{
			++HandLeg;
		}
		if (WomanAnim)
		{
			WomanAnim->LookTowards(0.f, 0.f);
		}
		Player->CarryEyesTo(DoctorHold + FVector(0.f, 0.f, FMath::Sin(HandTimer * 6.f) * 1.3f));
		Player->SetLens(FMath::Lerp(82.f, 88.f, FMath::Clamp(HandTimer / 3.f, 0.f, 1.f)));

		if (WalkPerson(WardMom, MomAnim, FVector(258, 150, 0), 66.f, 0.55f, Dt))
		{
			FacePerson(WardMom, Eyes, Dt, 2.f);
		}
		PersonLooks(WardMom, MomAnim, Eyes, 0.5f);

		bManFollows = bManFollows || WomanAt.Y > Man->GetActorLocation().Y + 150.f;
		if (bManFollows)
		{
			if (WalkPerson(Man, ManAnim, ManPath[ManLeg], 92.f, 0.72f, Dt) && ManLeg < 1)
			{
				++ManLeg;
			}
		}
		else if (WalkPerson(Man, ManAnim, FVector(505, 300, 0), 70.f, 0.6f, Dt))
		{
			FacePerson(Man, Eyes, Dt, 2.f);
		}
		PersonLooks(Man, ManAnim, Eyes, 0.7f);

		// How far he has been taken from her, from none at the door to all of it down the hall.
		const float Away = FMath::Clamp((WomanAt.Y - 152.f) / 950.f, 0.f, 1.f);
		Player->SetSaturation(FMath::Lerp(1.3f, 0.12f, FMath::SmoothStep(0.f, 1.f, Away * 1.1f)));
		const float Wrong = FMath::SmoothStep(0.08f, 1.f, Away);
		if (WomanAnim) { WomanAnim->SetWrong(Wrong); }
		if (ManAnim) { ManAnim->SetWrong(Wrong); }
		// The one behind also grows: too tall for the hall by the end of it.
		Man->SetActorScale3D(FVector(1.f, 1.f, 1.f + 0.13f * Wrong));

		if (HandTimer > 6.2f && !bHushed)
		{
			bHushed = true;
			Say(TEXT("\"Shh. Nobody is going to hurt you.\""), 4.5f);
			HG::Voice(this, TEXT("doc_hush"), ManFace);
		}

		if (bShots)
		{
			// Pictures: mom left in the doorway, the one following, the one carrying, and the far end.
			Player->TurnTowards(HandTimer < 4.4f ? MomFace : ((HandTimer > 8.4f && HandTimer < 10.2f) ? WomanFace : ManFace), Dt, 2.5f);
			if (HandTimer > 4.2f) { Shot(3, TEXT("ward-10-doorway.png")); }
			if (HandTimer > 8.2f) { Shot(4, TEXT("ward-11-following.png")); }
			if (HandTimer > 10.f) { Shot(5, TEXT("ward-12-carrier.png")); }
			if (HandTimer > 12.4f) { Shot(6, TEXT("ward-13-far.png")); }
		}
		else if (HandTimer > 0.3f && HandTimer < 2.6f)
		{
			Player->TurnTowards(MomFace, Dt, 1.4f);
		}

		if (HandTimer > 13.f && !bOver)
		{
			bOver = true;
			bWon = true;
			bHandedOver = true;
			EndTitle = TEXT("HE IS CARRIED OUT");
			EndLines.Reset();
			EndLines.Add(TEXT("End of the second scene."));
			EndLines.Add(TEXT("The next one is being built."));
			UE_LOG(LogHG, Log, TEXT("HGTEST HANDOVER done seconds=%d wrong=%.2f"), FMath::RoundToInt32(RoundSeconds), Wrong);
		}
		break;
	}
	}
}

void AHGGameMode::BeginCutscene(AHGPlayer* Player)
{
	APlayerController* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
	if (!PC)
	{
		return;
	}
	Cine = GetWorld()->SpawnActor<ACameraActor>(FVector(-190, -250, 245), FRotator::ZeroRotator);
	UCameraComponent* Lens = Cine->GetCameraComponent();
	Lens->SetFieldOfView(55.f);
	Lens->bConstrainAspectRatio = false;
	Lens->PostProcessSettings = Player->Look();
	Lens->PostProcessBlendWeight = 1.f;
	PC->SetViewTarget(Cine);
	CineLeft = CineLength;
}

void AHGGameMode::TickCutscene(float Dt, AHGPlayer* Player)
{
	// Two shots. A slow drift along the wall with the sun on it, high up, the way the room is remembered.
	// Then down on the floor, creeping towards the toys, the way it was lived.
	const float T = CineElapsed();
	FVector From, To, LookFrom, LookTo;
	float Along;
	if (T < 4.5f)
	{
		Along = T / 4.5f;
		From = FVector(-190, -250, 245); To = FVector(-80, -230, 236);
		LookFrom = FVector(-90, 320, 215); LookTo = FVector(70, 320, 185);
	}
	else
	{
		Along = (T - 4.5f) / (CineLength - 4.5f);
		From = FVector(238, -268, 20); To = FVector(150, -176, 40);
		LookFrom = LookTo = FVector(60, -70, 14);
	}
	Along = FMath::SmoothStep(0.f, 1.f, Along);
	const FVector At = FMath::Lerp(From, To, Along);
	if (Cine)
	{
		Cine->SetActorLocationAndRotation(At, (FMath::Lerp(LookFrom, LookTo, Along) - At).Rotation());
	}
	if (bShots && ((T < 2.5f && T + Dt >= 2.5f) || (T < 7.5f && T + Dt >= 7.5f)))
	{
		const TCHAR* Name = T < 4.5f ? TEXT("ward-0-opening-a.png") : TEXT("ward-0-opening-b.png");
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / Name, true, false);
		UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), Name);
	}
	CineLeft -= Dt;
	if (CineLeft <= 0.f)
	{
		if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
		{
			PC->SetViewTargetWithBlend(Player, 1.2f, VTBlend_Cubic);
		}
		ShotTimer = 0.f;
	}
}

void AHGGameMode::TickWard(float Dt, AHGPlayer* Player)
{
	// The clock on the wall runs from four minutes to ten. At ten the door opens, whatever he is doing.
	const float ClockRate = bBot ? 16.f : 2.f;
	const float SecondsToTen = 240.f - RoundSeconds * ClockRate;
	Level->SetClock(10.f - FMath::Max(SecondsToTen, 0.f) / 3600.f);
	TickMom(Dt, Player, SecondsToTen);
	TickDoctors(Dt, Player);
	TickBank += Dt;
	if (TickBank >= 1.f && WardStep < 10 && !bRealClock)
	{
		TickBank = 0.f;
		HG::PlayAt(this, TEXT("tick"), Level->ClockSpot(), 0.55f);
	}
	if (SecondsToTen <= 0.f && WardStep < 10)
	{
		WardStep = 10;
		WardTimer = 0.f;
		Say(TEXT("\"It's all right. The doctors are only going to help you.\""), 6.f);
		HG::Voice(this, TEXT("mom_doctors"), MomSpot + FVector(0, 0, 150));
	}
	if (WardStep == 10)
	{
		// The handle, the hinge, the hum of the hall, and then hard shoes coming along it.
		const FVector Doorway(280, 150, 100);
		if (!bDoorSounded)
		{
			bDoorSounded = true;
			HG::Extra(this, TEXT("door_handle"), Doorway, 1.f);
			HG::Extra(this, TEXT("door_open"), Doorway, 0.9f);
			HG::ExtraAt(this, TEXT("hall"), Doorway + FVector(150, 0, 0), 0.9f);
		}
		if (WardTimer > 2.2f && !bStepsSounded)
		{
			bStepsSounded = true;
			HG::Extra(this, TEXT("steps_adult"), Doorway + FVector(150, 500, -90), 1.3f);
		}
		// The door swings open on the hall: no colour out there, and a cold light.
		DoorOpen = FMath::FInterpConstantTo(DoorOpen, 1.f, Dt, 0.7f);
		Level->SetDoorOpen(FMath::SmoothStep(0.f, 1.f, DoorOpen));
		WardTimer += Dt;
		if (WardMom && WardDoctors.Num() == 2)
		{
			TickHandover(Dt, Player);
		}
		else if (WardTimer > 9.5f && !bOver)
		{
			// Without the people (the private plugin is missing) the scene ends on the open door.
			bOver = true;
			bWon = true;
			EndTitle = TEXT("THE DOCTORS COME IN");
			EndLines.Reset();
			EndLines.Add(TEXT("End of the first scene."));
			EndLines.Add(TEXT("The next one is being built."));
			UE_LOG(LogHG, Log, TEXT("HGTEST WARD done seconds=%d"), FMath::RoundToInt32(RoundSeconds));
		}
		return;
	}

	switch (WardStep)
	{
	case 0:
		if (RoundSeconds > 2.5f)
		{
			Say(TEXT("\"Put the shapes in the box, sweetheart.\""), 7.f);
			HG::Voice(this, TEXT("mom_shapes"), MomSpot + FVector(0, 0, 150));
			WardStep = 1;
		}
		break;
	case 1:
		if (WardSorter && WardSorter->Filled >= 3)
		{
			Say(TEXT("\"That's it. Good boy.\""), 4.f);
			HG::Voice(this, TEXT("mom_good"), MomSpot + FVector(0, 0, 150));
			WardTimer = 0.f;
			WardStep = 2;
		}
		break;
	case 2:
		WardTimer += Dt;
		if (WardTimer > 4.5f)
		{
			WardTimer = 0.f;
			if (WardRabbit)
			{
				Say(TEXT("\"Now bring me your rabbit, love.\""), 7.f);
				HG::Voice(this, TEXT("mom_rabbit"), MomSpot + FVector(0, 0, 150));
				WardStep = 3;
			}
			else
			{
				WardStep = 4;
			}
		}
		break;
	case 3:
		if (WardRabbit && Player->HeldToy() != WardRabbit && FVector::Dist2D(WardRabbit->GetActorLocation(), MomSpot) < 170.f)
		{
			Say(TEXT("\"Thank you, my darling. I've got him.\""), 4.f);
			HG::Voice(this, TEXT("mom_thanks"), MomSpot + FVector(0, 0, 150));
			WardTimer = 0.f;
			WardStep = 4;
		}
		break;
	default:
		// Nothing left to do but play, until the clock says otherwise.
		break;
	}
}

void AHGGameMode::TickWardBot(float Dt, AHGPlayer* Player)
{
	// Plays scene one by itself: carries each shape to the sorter, then the rabbit to where mom stands.
	BotTimer += Dt;
	if (BotTimer < 0.5f)
	{
		return;
	}
	BotTimer = 0.f;
	auto StandBy = [Player](const FVector& Thing)
	{
		Player->SetActorLocation(Thing + FVector(70, 0, 60));
		Player->LookAt(Thing + FVector(0, 0, 6));
	};
	const int32 Step = BotStep++;
	const int32 Shape = Step / 4;
	if (WardShapes.IsValidIndex(Shape))
	{
		switch (Step % 4)
		{
		case 0: StandBy(WardShapes[Shape]->GetActorLocation()); break;
		case 1: Player->Interact(); break;
		case 2: StandBy(WardSorter->GetActorLocation() + FVector(0, 0, 10)); break;
		default: Player->Interact(); break;
		}
		return;
	}
	const int32 After = Step - WardShapes.Num() * 4;
	if (After == 0)
	{
		UE_LOG(LogHG, Log, TEXT("HGTEST SORTER %s filled=%d"), WardSorter->Filled == 3 ? TEXT("PASS") : TEXT("FAIL"), WardSorter->Filled);
		if (WardRabbit) { StandBy(WardRabbit->GetActorLocation() + FVector(0, 0, 12)); }
	}
	else if (After == 1 && WardRabbit) { Player->Interact(); }
	else if (After == 2) { Player->SetActorLocation(MomSpot + FVector(60, -60, 60)); Player->LookAt(MomSpot); }
	else if (After == 3 && WardRabbit) { Player->Interact(); }
	else if (After > 3 && (bOver || After > 220))
	{
		// With the people in the game the scene has to run on through the handover.
		const bool bStaged = WardMom && WardDoctors.Num() == 2;
		UE_LOG(LogHG, Log, TEXT("HGTEST HANDOVER %s staged=%d step=%d carried=%d"), (!bStaged || bHandedOver) ? TEXT("PASS") : TEXT("FAIL"), bStaged ? 1 : 0, HandStep, Player->IsCarried() ? 1 : 0);
		const bool bGood = bOver && bWon && WardSorter->Filled == 3 && bAssetsComplete && (!bStaged || (bHandedOver && Player->IsCarried()));
		UE_LOG(LogHG, Log, TEXT("HGTEST RESULT %s ward ended=%d sorter=%d held=%d"), bGood ? TEXT("PASS") : TEXT("FAIL"), bOver ? 1 : 0, WardSorter->Filled, Player->HeldToy() ? 1 : 0);
		Finish(bGood ? 0 : 1);
	}
}

void AHGGameMode::Say(const FString& Text, float Seconds)
{
	Message = Text;
	MessageUntil = GetWorld()->GetTimeSeconds() + Seconds;
}

void AHGGameMode::ReportNoise(const FVector& Location, float Radius, float HoldSeconds, bool bFootstep)
{
	if (bOver || !Monster || Radius <= 0.f)
	{
		return;
	}
	if (FVector::Dist2D(Monster->GetActorLocation(), Location) <= Radius && Monster->HearNoise(Location, HoldSeconds, bFootstep))
	{
		++NoisesHeard;
	}
}

void AHGGameMode::CollectFuse()
{
	++Fuses;
	HG::Play(this, TEXT("fuse"), 0.8f, 1.f + 0.04f * Fuses);
	if (Fuses >= FusesTotal)
	{
		Level->SetExitOpen(true);
		HG::Play(this, TEXT("power"), 0.9f);
		Say(TEXT("That is all of them. The exit shutter has power.  Go."), 6.f);
	}
	else
	{
		Say(FString::Printf(TEXT("Fuse %d of %d."), Fuses, FusesTotal), 2.5f);
	}
}

void AHGGameMode::PlayerCaught(AHGMonster* By)
{
	if (bOver)
	{
		return;
	}
	AHGPlayer* Player = Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	bOver = true;
	EndTitle = TEXT("YOU DIED");
	EndLines.Reset();
	EndLines.Add(By ? By->CatchLine() : TEXT("Something got you."));
	EndLines.Add(FString::Printf(TEXT("Cause of death: %s."), Player ? *Player->LastCause : TEXT("unknown")));
	EndLines.Add(FString::Printf(TEXT("Fuses found: %d of %d.   Time on site: %d s."), Fuses, FusesTotal, FMath::RoundToInt32(RoundSeconds)));
	if (Player)
	{
		Player->FallOver();
	}
	HG::Play(this, TEXT("caught"), 1.f);
	UE_LOG(LogHG, Log, TEXT("HGTEST CAUGHT by=%s fuses=%d/%d"), By ? By->DisplayName() : TEXT("?"), Fuses, FusesTotal);
}

void AHGGameMode::PlayerDied(const FString& Cause)
{
	if (bOver)
	{
		return;
	}
	AHGPlayer* Player = Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	bOver = true;
	EndTitle = TEXT("YOU DIED");
	EndLines.Reset();
	EndLines.Add(TEXT("No monster was involved. This one is on you."));
	EndLines.Add(FString::Printf(TEXT("Cause of death: %s."), *Cause));
	EndLines.Add(FString::Printf(TEXT("Fuses found: %d of %d.   Time on site: %d s."), Fuses, FusesTotal, FMath::RoundToInt32(RoundSeconds)));
	if (Player)
	{
		Player->FallOver();
	}
	HG::Play(this, TEXT("caught"), 0.6f, 0.8f);
	UE_LOG(LogHG, Log, TEXT("HGTEST DIED cause=%s"), *Cause);
}

void AHGGameMode::Win()
{
	AHGPlayer* Player = Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	bOver = true;
	bWon = true;
	EndTitle = TEXT("INCIDENT REPORT");
	EndLines.Reset();
	EndLines.Add(TEXT("You left the building. Head office is calling that a success."));
	EndLines.Add(FString::Printf(TEXT("Time on site: %d s.   Fuses: %d of %d."), FMath::RoundToInt32(RoundSeconds), Fuses, FusesTotal));
	EndLines.Add(FString::Printf(TEXT("Weapons discharged: %d.   Monsters harmed: 0.   Screams: %d."),
		Player ? Player->Discharges : 0, Player ? Player->Screams : 0));
	HG::Play(this, TEXT("win"), 0.9f);
	UE_LOG(LogHG, Log, TEXT("HGTEST WIN fuses=%d/%d seconds=%d"), Fuses, FusesTotal, FMath::RoundToInt32(RoundSeconds));
}

void AHGGameMode::Travel(EHGVariant To)
{
	// The game mode is named in the options because a ready-made map may ask for its own.
	UGameplayStatics::OpenLevel(this, FName(HG::MapFor(To)), true, FString::Printf(TEXT("Variant=%s?game=/Script/HorrorGame.HGGameMode"), HG::VariantKey(To)));
}

void AHGGameMode::Finish(int32 ExitCode)
{
	if (SecondsSeen > 0.5)
	{
		UE_LOG(LogHG, Log, TEXT("HGTEST FPS variant=%s average=%d frames=%d seconds=%d"), HG::VariantKey(Variant),
			FMath::RoundToInt32(FramesSeen / SecondsSeen), FramesSeen, FMath::RoundToInt32(SecondsSeen));
	}
	UE_LOG(LogHG, Log, TEXT("HGTEST DONE code=%d"), ExitCode);
	bBot = false;
	bShots = false;
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}

void AHGGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AHGPlayer* Player = Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Level || !Player)
	{
		return;
	}

	// Until everything on screen is ready the round does not start: nothing hunts you and no clock runs.
	LoadingLeft = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#if WITH_EDITOR
	LoadingLeft += FAssetCompilingManager::Get().GetNumRemainingAssets();
#endif
	if (IsLoading())
	{
		ShotTimer = 0.f;
		return;
	}

	++FramesSeen;
	SecondsSeen += FApp::GetDeltaTime();

	if (Variant == EHGVariant::Ward)
	{
		// The story's first scene has its own small script and none of the fuse-hunt rules.
		if (IsCutscene())
		{
			TickCutscene(DeltaSeconds, Player);
			return;
		}
		if (!bOver)
		{
			RoundSeconds += DeltaSeconds;
			TickWard(DeltaSeconds, Player);
		}
		if (bBot)
		{
			TickWardBot(DeltaSeconds, Player);
		}
		if (bShots)
		{
			ShotTimer += DeltaSeconds;
			if (ShotStep == 0 && ShotTimer > 7.f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-1-start.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-1-start"));
				Player->SetActorLocation(FVector(190, 150, 60));
				Player->LookAt(FVector(-100, -120, 40));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 1 && ShotTimer > 2.5f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-2-room.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-2-room"));
				Player->SetActorLocation(FVector(-150, -200, 60));
				Player->LookAt(MomSpot + FVector(0, 0, 90));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 2 && ShotTimer > 2.5f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-3-bed.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-3-bed"));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 3 && WardMom && MomShot == 0 && ShotTimer > 0.5f)
			{
				// Mom, from where he looks up at her.
				Player->SetActorLocation(MomSpot + FVector(95, -95, 60));
				Player->LookAt(MomSpot + FVector(0, 0, 138));
				MomShot = 1;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 3 && MomShot == 1 && ShotTimer > 2.5f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-3-mom.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-3-mom"));
				MomShot = 2;
				ShotTimer = 0.f;
				// And the same view while she glances at the door: her head goes, her body stays.
				MomGlance = 4.f;
			}
			else if (ShotStep == 3 && MomShot == 2 && ShotTimer > 1.8f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-3-glance.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-3-glance"));
				MomShot = 3;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 3 && (!WardMom || MomShot == 3) && ShotTimer > 0.5f)
			{
				// The door wall, with the clock, then jump the clock to a few seconds before ten.
				Player->SetActorLocation(FVector(-60, -40, 60));
				Player->LookAt(FVector(260, 60, 150));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 4 && ShotTimer > 2.5f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-4-clock.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-4-clock"));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 5 && ShotTimer > 0.5f)
			{
				// A picture is taken a frame after it is asked for, so only now move on: to the door, seconds before ten.
				RoundSeconds = 119.f;
				Player->SetActorLocation(FVector(40, 150, 60));
				Player->LookAt(FVector(300, 150, 95));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 6 && ShotTimer > 4.5f)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-5-door.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-5-door"));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 7 && WardDoctors.Num() > 0 && ShotTimer > 5.5f)
			{
				// The doctors, stopped in the doorway, from where he sits on the floor.
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / TEXT("ward-6-doctors.png"), true, false);
				UE_LOG(LogHG, Log, TEXT("HGTEST SHOT ward-6-doctors"));
				++ShotStep;
				ShotTimer = 0.f;
			}
			else if (ShotStep == 8 && WardMom && WardDoctors.Num() == 2)
			{
				// The handover takes its own pictures. He starts it where he would be: on the floor by the toys.
				if (!bShotPlaced)
				{
					bShotPlaced = true;
					Player->SetActorLocation(FVector(60, -70, 60));
					Player->LookAt(FVector(300, 150, 120));
				}
				if (!bOver)
				{
					ShotTimer = 0.f;
				}
				else if (ShotTimer > 1.5f)
				{
					Finish(0);
				}
			}
			else if ((ShotStep == 8 || (ShotStep == 7 && WardDoctors.Num() == 0)) && ShotTimer > 1.5f)
			{
				Finish(0);
			}
		}
		return;
	}

	if (!bOver)
	{
		RoundSeconds += DeltaSeconds;
		const bool bAtExit = Level->bWorld
			? (RoundSeconds > 5.f && Fuses >= FusesTotal && FVector::Dist(Player->GetActorLocation(), Level->ExitSpot + FVector(0, 0, 88)) < 220.f)
			: Level->ToTile(Player->GetActorLocation()) == Level->ExitTile;
		if (bAtExit)
		{
			if (Fuses >= FusesTotal)
			{
				Win();
			}
			else if (GetWorld()->GetTimeSeconds() > MessageUntil)
			{
				Say(FString::Printf(TEXT("The shutter is dead. It needs %d more fuses."), FusesTotal - Fuses), 2.5f);
			}
		}
	}

	if (!Monster)
	{
		return;
	}
	if (Level->bWorld)
	{
		// The self-play checks know the text-built places only; a ready-made map just gets its pictures taken.
		if (bShots) { TickWorldShots(DeltaSeconds, Player); }
		return;
	}
	if (bBot && bCatch) { TickCatchBot(DeltaSeconds, Player); }
	else if (bBot) { TickBot(DeltaSeconds, Player); }
	if (bShots) { TickShots(DeltaSeconds, Player); }
}

void AHGGameMode::TickBot(float Dt, AHGPlayer* Player)
{
	// Plays the round with teleports instead of legs, and checks the rules hold:
	// a weapon lures the monster, every fuse can be collected, the exit ends the round,
	// and (second half) being next to the monster ends it the other way.
	// First, real legs for two seconds: the player must be standing on the floor and able to walk.
	if (WalkSeconds < 2.f)
	{
		if (WalkSeconds == 0.f)
		{
			TArray<FIntPoint> Out;
			Level->FindPath(Level->PlayerTile, Level->ExitTile, Out);
			WalkFrom = Player->GetActorLocation();
			WalkDirection = Out.Num() > 0 ? (Level->ToWorld(Out[0]) - Level->ToWorld(Level->PlayerTile)).GetSafeNormal() : FVector::ForwardVector;
			// Hold W the way a keyboard would, so this checks the keys reach the player and not just that legs work.
			Player->LookAt(Player->EyeLocation() + WalkDirection * 500.f);
			if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
			{
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.f));
			}
		}
		Monster->Stun(0.5f);
		WalkSeconds += Dt;
		if (WalkSeconds >= 2.f)
		{
			if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
			{
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.f));
			}
			const FVector Now = Player->GetActorLocation();
			const float Walked = FVector::Dist2D(Now, WalkFrom);
			const bool bStanding = FMath::Abs(Now.Z - 88.f) < 25.f;
			UE_LOG(LogHG, Log, TEXT("HGTEST WALK %s walked=%d z=%d"), (Walked > 150.f && bStanding) ? TEXT("PASS") : TEXT("FAIL"),
				FMath::RoundToInt32(Walked), FMath::RoundToInt32(Now.Z));
			if (Walked <= 150.f || !bStanding)
			{
				UE_LOG(LogHG, Log, TEXT("HGTEST RESULT FAIL walk"));
				Finish(1);
			}
		}
		return;
	}

	BotTimer += Dt;
	if (BotTimer < 0.6f)
	{
		return;
	}
	BotTimer = 0.f;
	const int32 Step = BotStep++;

	if (Step == 0)
	{
		Player->GiveItem(EHGItem::Pistol);
		// Stand a few tiles from the monster, out of sight is not required: the pistol is loud.
		TArray<FIntPoint> Tiles;
		Level->FindPath(Level->ToTile(Monster->GetActorLocation()), Level->PlayerTile, Tiles);
		const FIntPoint Spot = Tiles.Num() > 3 ? Tiles[3] : Level->PlayerTile;
		Player->SetActorLocation(Level->ToWorld(Spot, 100.f));
		Player->LookAt(Player->GetActorLocation() + (Player->GetActorLocation() - Monster->GetActorLocation()));
		return;
	}
	if (Step == 1)
	{
		Player->UseItem();
		return;
	}
	if (Step == 2)
	{
		bLured = NoisesHeard > 0 && Monster->Mood() == TEXT("investigating");
		UE_LOG(LogHG, Log, TEXT("HGTEST LURE %s mood=%s heard=%d"), bLured ? TEXT("PASS") : TEXT("FAIL"), *Monster->Mood(), NoisesHeard);
		Monster->Stun(10000.f);
		return;
	}

	const int32 FuseIndex = Step - 3;
	if (Level->FuseTiles.IsValidIndex(FuseIndex))
	{
		Player->SetActorLocation(Level->ToWorld(Level->FuseTiles[FuseIndex], 100.f));
		return;
	}
	if (FuseIndex == Level->FuseTiles.Num())
	{
		UE_LOG(LogHG, Log, TEXT("HGTEST FUSES %d/%d"), Fuses, FusesTotal);
		Player->SetActorLocation(Level->ToWorld(Level->ExitTile, 100.f));
		return;
	}
	if (FuseIndex == Level->FuseTiles.Num() + 1)
	{
		const bool bGood = bWon && Fuses == FusesTotal && bLured && bAssetsComplete;
		UE_LOG(LogHG, Log, TEXT("HGTEST RESULT %s won=%d fuses=%d/%d lured=%d assets=%d"), bGood ? TEXT("PASS") : TEXT("FAIL"),
			bWon ? 1 : 0, Fuses, FusesTotal, bLured ? 1 : 0, bAssetsComplete ? 1 : 0);
		Finish(bGood ? 0 : 1);
	}
}

void AHGGameMode::TickCatchBot(float Dt, AHGPlayer* Player)
{
	// Stands two tiles from the monster with its back turned, makes a noise, and waits to die.
	BotTimer += Dt;
	if (BotStep == 0)
	{
		TArray<FIntPoint> Tiles;
		Level->FindPath(Level->ToTile(Monster->GetActorLocation()), Level->PlayerTile, Tiles);
		const FIntPoint Spot = Tiles.Num() > 1 ? Tiles[1] : Level->PlayerTile;
		Player->GiveItem(EHGItem::Pistol);
		Player->SetActorLocation(Level->ToWorld(Spot, 100.f));
		Player->LookAt(Player->EyeLocation() + (Player->GetActorLocation() - Monster->GetActorLocation()));
		BotStep = 1;
		BotTimer = 0.f;
	}
	else if (BotStep == 1 && BotTimer > 0.5f)
	{
		Player->UseItem();
		BotStep = 2;
		BotTimer = 0.f;
	}
	else if (BotStep == 2 && (bOver || BotTimer > 20.f))
	{
		const bool bGood = bOver && !bWon;
		UE_LOG(LogHG, Log, TEXT("HGTEST RESULT %s caught=%d after=%ds"), bGood ? TEXT("PASS") : TEXT("FAIL"), bGood ? 1 : 0, FMath::RoundToInt32(BotTimer));
		Finish(bGood ? 0 : 1);
	}
}

void AHGGameMode::TickWorldShots(float Dt, AHGPlayer* Player)
{
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
	{
		ShotTimer = 0.f;
		return;
	}
	ShotTimer += Dt;
	Player->Composure = 1.f;
	// Held still for the pictures.
	Monster->Stun(0.5f);
	auto Shoot = [this](const TCHAR* Name)
	{
		const FString File = FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("%s-%s.png"), HG::VariantKey(Variant), Name);
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), *File);
	};
	switch (ShotStep)
	{
	case 0:
		if (ShotTimer > 6.f) { Shoot(TEXT("1-start")); ++ShotStep; ShotTimer = 0.f; }
		break;
	case 1:
		if (ShotTimer > 1.f)
		{
			// A few steps along the monster's own route to the player, looking back at it.
			TArray<FVector> Route;
			Level->FindWorldPath(Monster->GetActorLocation(), Player->GetActorLocation() - FVector(0, 0, 88), Route);
			if (Route.Num() > 0)
			{
				Player->SetActorLocation(Route[FMath::Min(4, Route.Num() - 1)] + FVector(0, 0, 100));
			}
			++ShotStep;
			ShotTimer = 0.f;
		}
		break;
	case 2:
		Player->LookAt(Monster->Chest());
		if (ShotTimer > 2.5f) { Shoot(TEXT("2-monster")); ++ShotStep; ShotTimer = 0.f; }
		break;
	default:
		if (ShotTimer > 1.5f) { Finish(0); }
		break;
	}
}

void AHGGameMode::TickShots(float Dt, AHGPlayer* Player)
{
	// Waits for shaders, then walks through a few staged moments and saves a picture of each.
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
	{
		ShotTimer = 0.f;
		return;
	}
	ShotTimer += Dt;
	Player->Composure = 1.f; // no blinking or screaming while posing for pictures

	auto Shoot = [this](const TCHAR* Name)
	{
		const FString File = FPaths::ProjectSavedDir() / TEXT("Shots") / FString::Printf(TEXT("%s-%s.png"), HG::VariantKey(Variant), Name);
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), *File);
	};
	auto Next = [this]() { ++ShotStep; ShotTimer = 0.f; };

	switch (ShotStep)
	{
	case 0:
		if (ShotTimer > 5.f) { Shoot(TEXT("1-start")); Next(); }
		break;
	case 1:
		if (ShotTimer > 1.f)
		{
			// Two tiles from the monster, looking straight at it.
			TArray<FIntPoint> Tiles;
			Level->FindPath(Level->ToTile(Monster->GetActorLocation()), Level->PlayerTile, Tiles);
			const FIntPoint Spot = Tiles.Num() > 1 ? Tiles[1] : Level->PlayerTile;
			if (Monster->Kind != EHGMonster::Walker)
			{
				Monster->Stun(600.f);
			}
			Player->SetActorLocation(Level->ToWorld(Spot, 100.f));
			Player->LookAt(Monster->Chest());
			Next();
		}
		break;
	case 2:
		Player->LookAt(Monster->Chest());
		if (Monster->Kind != EHGMonster::Walker)
		{
			// Held still for the picture, so turn it to face the camera.
			Monster->SetActorRotation(FRotator(0.f, (Player->GetActorLocation() - Monster->GetActorLocation()).Rotation().Yaw, 0.f));
		}
		if (ShotTimer > 2.f) { Shoot(TEXT("2-monster")); Next(); }
		break;
	case 3:
		if (ShotTimer > 1.f) { Player->GiveItem(EHGItem::Pistol); Player->UseItem(); Next(); }
		break;
	case 4:
		if (ShotTimer > 0.8f) { Shoot(TEXT("3-pistol")); Next(); }
		break;
	case 5:
		if (ShotTimer > 1.f) { Player->GiveItem(EHGItem::Shotgun); Player->UseItem(); Next(); }
		break;
	case 6:
		if (ShotTimer > 0.35f) { Shoot(TEXT("4-confetti")); Next(); }
		break;
	case 7:
		if (ShotTimer > 1.f) { PlayerCaught(Monster); Next(); }
		break;
	case 8:
		if (ShotTimer > 2.f) { Shoot(TEXT("5-death")); Next(); }
		break;
	default:
		if (ShotTimer > 1.5f) { Finish(0); }
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// HUD

namespace
{
	const FName FaceLight(TEXT("Light"));

	/**
	 * The engine font at the size it will be seen. The stock HUD call rasterises it at 9 points and
	 * stretches the result, which is what made every line look like a low-resolution bitmap.
	 */
	FSlateFontInfo HudFont(float Scale, FName Face)
	{
		const UFont* Font = GEngine->GetLargeFont();
		const float Base = Font ? float(Font->LegacyFontSize) : 9.f;
		return FSlateFontInfo(Font, FMath::Max(4.f, Base * Scale), Face);
	}

	/** The engine keeps the text item's own measuring function protected; this opens it. */
	struct FHGTextItem : FCanvasTextItem
	{
		using FCanvasTextItem::FCanvasTextItem;
		using FCanvasTextItem::GetTextSize;
	};
}

float AHGHUD::Measure(const FString& Text, float Scale, FName Face) const
{
	if (!Canvas || !Canvas->Canvas || Text.IsEmpty())
	{
		return 0.f;
	}
	const float Dpi = FMath::Max(Canvas->Canvas->GetDPIScale(), 0.01f);
	const FHGTextItem Item(FVector2D::ZeroVector, FText::FromString(Text), HudFont(Scale, Face), FLinearColor::White);
	return Item.GetTextSize(Dpi).X / Dpi;
}

void AHGHUD::Write(const FString& Text, const FLinearColor& Color, float X, float Y, float Scale, FName Face)
{
	if (!Canvas || Text.IsEmpty())
	{
		return;
	}
	FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y)), FText::FromString(Text), HudFont(Scale, Face), Color);
	Item.BlendMode = SE_BLEND_Translucent;

	// A soft dark copy underneath keeps white text readable over the bright ward.
	const float Drop = FMath::Max(1.f, FMath::RoundToFloat(Scale * 0.7f));
	Item.Position += FVector2D(Drop, Drop);
	Item.SetColor(FLinearColor(0.f, 0.f, 0.f, Color.A * 0.75f));
	Canvas->DrawItem(Item);
	Item.Position -= FVector2D(Drop, Drop);
	Item.SetColor(Color);
	Canvas->DrawItem(Item);
}

void AHGHUD::Centered(const FString& Text, float Y, float Scale, const FLinearColor& Color, FName Face)
{
	Write(Text, Color, (Canvas->ClipX - Measure(Text, Scale, Face)) * 0.5f, Y, Scale, Face);
}

void AHGHUD::DrawHUD()
{
	Super::DrawHUD();

	const AHGGameMode* Game = GetWorld()->GetAuthGameMode<AHGGameMode>();
	const AHGPlayer* Player = Cast<AHGPlayer>(GetOwningPawn());
	if (!Game || !Canvas)
	{
		return;
	}

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float S = H / 720.f;
	const float Now = GetWorld()->GetTimeSeconds();
	const FLinearColor Amber(1.f, 0.78f, 0.25f);
	const FLinearColor Dim(0.75f, 0.75f, 0.72f);

	if (Player && Player->IsBlinking())
	{
		DrawRect(FLinearColor::Black, 0.f, 0.f, W, H);
	}

	if (Game->IsLoading())
	{
		DrawRect(FLinearColor::Black, 0.f, 0.f, W, H);
		Centered(TEXT("LOADING"), H * 0.42f, 2.6f * S, FLinearColor::White, FaceLight);
		Centered(FString::Printf(TEXT("%d things left to prepare. The first start of a new place takes a while."), Game->LoadingLeft), H * 0.54f, 1.1f * S, Dim);
		return;
	}

	const bool bStory = Game->GetVariant() == EHGVariant::Ward;
	if (bStory)
	{
		// Film bars, top and bottom.
		DrawRect(FLinearColor::Black, 0.f, 0.f, W, H * 0.09f);
		DrawRect(FLinearColor::Black, 0.f, H * 0.91f, W, H * 0.09f);
	}

	if (Game->IsOver())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, bStory ? 0.92f : 0.72f), 0.f, 0.f, W, H);
		Centered(Game->EndTitle, H * 0.22f, 4.2f * S, bStory ? FLinearColor::White : (Game->HasWon() ? FLinearColor(0.5f, 1.f, 0.6f) : FLinearColor(0.95f, 0.1f, 0.08f)), FaceLight);
		float Y = H * 0.42f;
		for (const FString& Line : Game->EndLines)
		{
			Centered(Line, Y, 1.5f * S, FLinearColor::White);
			Y += 40.f * S;
		}
		Centered(TEXT("R  try again      F1  clinic      F2  office      F3  mine      F4  hospital      Cmd+Q  quit"), H * 0.8f, 1.2f * S, Dim);
		return;
	}

	if (bStory)
	{
		// No meters, no inventory. A dot to aim with, a title card, and the odd line of text.
		if (Game->IsCutscene())
		{
			// The title sits over the first shot and is gone before the second.
			const float T = Game->CineElapsed();
			const float Alpha = FMath::Clamp(FMath::Min(T - 0.6f, 4.2f - T), 0.f, 1.f);
			Centered(TEXT("RUSSIA, 1997"), H * 0.40f, 3.4f * S, FLinearColor(1.f, 1.f, 1.f, Alpha), FaceLight);
			Centered(TEXT("Inspired in part by a true story."), H * 0.52f, 1.3f * S, FLinearColor(1.f, 1.f, 1.f, Alpha * 0.85f));
			return;
		}
		if (!Player || !Player->IsCarried())
		{
			// The aiming dot goes when his hands do.
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.55f), W * 0.5f - 1.5f, H * 0.5f - 1.5f, 3.f, 3.f);
		}
		if (Game->RoundSeconds < 12.f)
		{
			const float Alpha = FMath::Clamp(12.f - Game->RoundSeconds, 0.f, 1.f);
			Centered(TEXT("WASD move      Mouse look      Click to pick up and put down"), H * 0.845f, 1.05f * S, FLinearColor(Dim.R, Dim.G, Dim.B, Alpha));
		}
		if (Now < Game->MessageUntil)
		{
			const float Alpha = FMath::Clamp((Game->MessageUntil - Now) * 2.f, 0.f, 1.f);
			Centered(Game->Message, H * 0.78f, 1.6f * S, FLinearColor(1.f, 1.f, 1.f, Alpha));
		}
		return;
	}

	// Fuses, top left.
	Write(FString::Printf(TEXT("FUSES  %d / %d"), Game->Fuses, Game->FusesTotal), Amber, 26.f * S, 20.f * S, 1.6f * S);

	if (Player)
	{
		// Item slots, bottom left.
		float X = 26.f * S;
		const float Y = H - 52.f * S;
		if (Player->Slots.Num() == 0)
		{
			Write(TEXT("Empty hands"), Dim, X, Y, 1.2f * S);
		}
		for (int32 i = 0; i < Player->Slots.Num(); ++i)
		{
			const FString Label = FString::Printf(TEXT("[%d] %s"), i + 1, HG::ItemName(Player->Slots[i]));
			float TextW = 0.f;
			TextW = Measure(Label, 1.2f * S);
			Write(Label, i == Player->Selected ? Amber : Dim, X, Y, 1.2f * S);
			X += TextW + 26.f * S;
		}

		// Flashlight and nerves, bottom right.
		const FString Light = Player->IsFlashlightOn() ? TEXT("FLASHLIGHT  ok") : TEXT("FLASHLIGHT  dead   [F] smack it");
		float LightW = 0.f;
		LightW = Measure(Light, 1.2f * S);
		Write(Light, Player->IsFlashlightOn() ? Dim : FLinearColor(1.f, 0.3f, 0.2f), W - LightW - 26.f * S, H - 52.f * S, 1.2f * S);

		const float BarW = 180.f * S;
		const float BarX = W - BarW - 26.f * S;
		const float BarY = H - 76.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), BarX, BarY, BarW, 8.f * S);
		DrawRect(FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.15f, 0.1f), FLinearColor(0.6f, 0.9f, 0.7f), Player->Composure), BarX, BarY, BarW * Player->Composure, 8.f * S);
		Write(TEXT("NERVES"), Dim, BarX - 74.f * S, BarY - 7.f * S, 0.9f * S);
	}

	// Crosshair.
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.6f), W * 0.5f - 1.5f, H * 0.5f - 1.5f, 3.f, 3.f);

	// Opening card.
	if (Game->RoundSeconds < 7.f)
	{
		const float Alpha = FMath::Clamp(7.f - Game->RoundSeconds, 0.f, 1.f);
		Centered(HG::VariantTitle(Game->GetVariant()), H * 0.16f, 2.4f * S, FLinearColor(1.f, 1.f, 1.f, Alpha), FaceLight);
		Centered(TEXT("WASD move    Shift sprint (loud)    Ctrl tiptoe (silent)    Mouse use    1-4 choose    F smack flashlight"),
			H * 0.25f, 1.05f * S, FLinearColor(Dim.R, Dim.G, Dim.B, Alpha));
	}

	if (Now < Game->MessageUntil)
	{
		const float Alpha = FMath::Clamp((Game->MessageUntil - Now) * 2.f, 0.f, 1.f);
		Centered(Game->Message, H * 0.68f, 1.6f * S, FLinearColor(1.f, 1.f, 1.f, Alpha));
	}
}
