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
#include "Engine/Texture2D.h"
#include "TextureResource.h"
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
#include "HGFaceAnim.h"
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
	bLessBlood = FParse::Param(FCommandLine::Get(), TEXT("HGLessBlood"));
	FParse::Value(FCommandLine::Get(), TEXT("HGScene="), StartScene);
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
		for (const TPair<const TCHAR*, float>& Bed : { TPair<const TCHAR*, float>(TEXT("ward_room"), 0.7f), TPair<const TCHAR*, float>(TEXT("ward_birds"), 0.5f) })
		{
			if (UAudioComponent* Sound = HG::ExtraBed(this, Bed.Key, Bed.Value))
			{
				WardBeds.Add(Sound);
				WardBedFull.Add(Bed.Value);
			}
		}
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
		UAudioComponent* Tone = UGameplayStatics::SpawnSound2D(this, Found, 0.55f);
		if (Tone && Variant == EHGVariant::Ward)
		{
			WardBeds.Add(Tone);
			WardBedFull.Add(0.55f);
		}
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
	UHGFaceAnim* Face = nullptr;
	WardMom = SpawnPerson(TEXT("/HorrorPrivate/MetaHumans/Mom/BP_Mom.BP_Mom_C"), MomSpot, (FVector(60, -70, 0) - MomSpot).Rotation().Yaw, Anim, Face);
	MomAnim = Anim;
	MomFaceAnim = Face;
	if (WardMom)
	{
		if (MomFaceAnim)
		{
			// Watching him play, she is happy.
			MomFaceAnim->SetWarm(0.7f);
		}
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
		TEXT("/HorrorPrivate/MetaHumans/DoctorSecond/BP_DoctorSecond.BP_DoctorSecond_C") };
	static const FVector DoctorStarts[] = { FVector(430, 790, 0), FVector(480, 880, 0) };
	for (int32 i = 0; i < 2; ++i)
	{
		UHGPersonAnim* DoctorAnim = nullptr;
		UHGFaceAnim* DoctorFace = nullptr;
		if (AActor* Doctor = SpawnPerson(DoctorClasses[i], DoctorStarts[i], -90.f, DoctorAnim, DoctorFace))
		{
			if (DoctorFace)
			{
				// Polite, the way people are with a child they are about to upset.
				DoctorFace->SetWarm(0.35f);
			}
			DoctorFaces.Add(DoctorFace);
			if (DoctorAnim)
			{
				DoctorAnim->SetClip(PersonIdle);
			}
			WardDoctors.Add(Doctor);
			DoctorAnims.Add(DoctorAnim);
		}
	}
	UE_LOG(LogHG, Log, TEXT("HGTEST DOCTORS spawned=%d"), WardDoctors.Num());

	// Scene three: two more people stand in the corridor, one by the left wall and one by the right.
	// They are the same two doctors over again, which is how he remembers every adult in that building.
	if (WardDoctors.Num() == 2)
	{
		// Both on the left: he is on the doctor's left shoulder, and her head hides whatever is on the right.
		static const FVector StaffSpots[] = { FVector(548, 2500, 0), FVector(548, 3500, 0) };
		static const float StaffFacing[] = { 180.f, 180.f };
		for (int32 i = 0; i < 2; ++i)
		{
			UHGPersonAnim* StaffAnim = nullptr;
			UHGFaceAnim* StaffFace = nullptr;
			if (AActor* Person = SpawnPerson(DoctorClasses[i], StaffSpots[i], StaffFacing[i], StaffAnim, StaffFace))
			{
				if (StaffAnim)
				{
					StaffAnim->SetClip(PersonIdle);
				}
				// Nothing of them is drawn or moved until he is carried into the corridor.
				Person->SetActorHiddenInGame(true);
				StaffLights.Add(AddFaceLight(Person));
				Staff.Add(Person);
				StaffAnims.Add(StaffAnim);
				StaffFaces.Add(StaffFace);
			}
		}
		UE_LOG(LogHG, Log, TEXT("HGTEST STAFF spawned=%d"), Staff.Num());
	}

	// Scene five's ward of beds, the bed he wakes on, and a rubber ball that squeaks.
	if (WardDoctors.Num() == 2)
	{
		for (const TCHAR* Part : { TEXT("bed_bed_0"), TEXT("blanket_blanket_0"), TEXT("mattress_mattress_0") })
		{
			const FString Path = FString::Printf(TEXT("/HorrorPrivate/Ward/Bed/old_hospital_bed_pbrgr/StaticMeshes/%s.%s"), Part, Part);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				BedParts.Add(Mesh);
			}
		}
		const FVector Corner = Level->EscapeOrigin();
		AddBed(Corner + FVector(110, 290, 0), 0.f);
		for (int32 i = 0; i < 6; ++i)
		{
			AddBed(Corner + FVector(592, 560.f + 230.f * i, 0), 90.f);
			AddBed(Corner + FVector(-92, 560.f + 230.f * i, 0), 90.f);
		}
		// Under the nurses' counter, from one end to the other.
		HideBoxes.Add(FBox(Corner + FVector(900, 2164, 0), Corner + FVector(1300, 2240, 70)));
		if (UStaticMesh* Ball = LoadObject<UStaticMesh>(nullptr, TEXT("/HorrorPrivate/Ward/Toys/SM_Ball.SM_Ball"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			EscapeToy = World->SpawnActor<AHGToy>(Corner + FVector(330, 520, 1), FRotator::ZeroRotator, Always);
			EscapeToy->SetupMesh(FName(TEXT("ball")), Ball, 1.f);
		}
		UE_LOG(LogHG, Log, TEXT("HGTEST ESCAPE beds=%d hides=%d toy=%d"), BedParts.Num() > 0 ? 13 : 0, HideBoxes.Num(), EscapeToy ? 1 : 0);
	}

	// The lines spoken in scenes one and two. They live with the people, in the private plugin.
	static const TCHAR* Lines[] = { TEXT("mom_shapes"), TEXT("mom_good"), TEXT("mom_rabbit"), TEXT("mom_thanks"), TEXT("mom_doctors"),
		TEXT("doc_ready"), TEXT("mom_lift"), TEXT("doc_take"), TEXT("doc_hush"), TEXT("doc_nearly"), TEXT("doc_here"),
		TEXT("chair_sit"), TEXT("chair_cover"), TEXT("chair_still"), TEXT("chair_hold"), TEXT("chair_shh"), TEXT("chair_done"),
		TEXT("esc_other_where"), TEXT("esc_other_come"), TEXT("esc_other_there"), TEXT("esc_woman_time"), TEXT("esc_woman_hear"), TEXT("esc_woman_there"),
		TEXT("esc_caught"), TEXT("mom_where"), TEXT("mom_found") };
	for (const TCHAR* Line : Lines)
	{
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Audio/V_%s.V_%s"), Line, Line);
		if (USoundBase* Found = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			KeptSounds.Add(Found);
			++VoicesFound;
		}
		else if (WardMom)
		{
			UE_LOG(LogHG, Warning, TEXT("HGTEST MISSING voice %s"), Line);
		}
	}
	UE_LOG(LogHG, Log, TEXT("HGTEST VOICES found=%d/%d"), VoicesFound, static_cast<int32>(UE_ARRAY_COUNT(Lines)));

	// The corridor's own sounds: her shoes, the rumble, his heart, voices behind a door, a cart, the last door.
	static const TCHAR* CorridorSounds[] = { TEXT("step_hard_1"), TEXT("step_hard_2"), TEXT("hall_low"), TEXT("heart"), TEXT("hall_voices"), TEXT("cart"), TEXT("op_door"),
		TEXT("tray"), TEXT("glove"), TEXT("cloth"), TEXT("instrument"), TEXT("chair_creak"), TEXT("ring"),
		TEXT("escape_drone"), TEXT("spotted"), TEXT("caught") };
	for (const TCHAR* Name : CorridorSounds)
	{
		const FString Path = FString::Printf(TEXT("/HorrorPrivate/Audio/X_%s.X_%s"), Name, Name);
		if (USoundBase* Found = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			KeptSounds.Add(Found);
			++CorridorSoundsFound;
		}
		else if (WardMom)
		{
			UE_LOG(LogHG, Warning, TEXT("HGTEST MISSING corridor sound %s"), Name);
		}
	}
	UE_LOG(LogHG, Log, TEXT("HGTEST CORRIDOR SOUNDS found=%d/%d"), CorridorSoundsFound, static_cast<int32>(UE_ARRAY_COUNT(CorridorSounds)));
}

AActor* AHGGameMode::SpawnPerson(const TCHAR* ClassPath, const FVector& At, float FacingYaw, UHGPersonAnim*& OutAnim, UHGFaceAnim*& OutFace)
{
	OutAnim = nullptr;
	OutFace = nullptr;
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
		if (Mesh->GetName() == TEXT("Body"))
		{
			Mesh->SetAnimInstanceClass(UHGPersonAnim::StaticClass());
			OutAnim = Cast<UHGPersonAnim>(Mesh->GetAnimInstance());
		}
		else if (Mesh->GetName() == TEXT("Face"))
		{
			// The head's own mesh: it follows the body and carries the expression (see HGFaceAnim.h).
			Mesh->SetAnimInstanceClass(UHGFaceAnim::StaticClass());
			OutFace = Cast<UHGFaceAnim>(Mesh->GetAnimInstance());
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
	if (bEscape)
	{
		TickEscape(Dt, Player);
		return;
	}
	if (bChair)
	{
		TickChair(Dt, Player);
		return;
	}
	AActor* Woman = WardDoctors[0];
	AActor* Second = WardDoctors[1];
	UHGPersonAnim* WomanAnim = DoctorAnims[0];
	UHGPersonAnim* SecondAnim = DoctorAnims[1];
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
	const FVector SecondFace = PersonPoint(Second, 9.f, 0.f, 166.f);
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
			if (MomFaceAnim)
			{
				// The small sad smile he remembers.
				MomFaceAnim->SetWarm(0.6f);
				MomFaceAnim->SetSad(0.85f);
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
			if (MomFaceAnim)
			{
				MomFaceAnim->SetWarm(0.3f);
				MomFaceAnim->SetSad(1.f);
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
		if (WalkPerson(Second, SecondAnim, FVector(505, 300, 0), 70.f, 0.6f, Dt))
		{
			FacePerson(Second, Eyes, Dt, 2.5f);
		}
		PersonLooks(Second, SecondAnim, Eyes, 0.6f);
		if (HandTimer > 1.5f && !bCried)
		{
			// He cries for her. The sound is only there once a recording of it is in the private plugin.
			bCried = true;
			HG::ExtraBed(this, TEXT("child_cry"), 0.7f);
		}
		if (HandTimer > 3.9f) { Shot(2, TEXT("ward-9-handover.png")); }
		if (HandTimer > 5.f)
		{
			Next();
		}
		break;
	}
	default:
	{
		// She turns and carries him out and along the hall to the right. Mom comes as far as the
		// doorway and no further. The other doctor falls in behind, where he cannot help seeing him.
		// The walk does not stop at the end of the scene: it runs on into the corridor, to a door on the left.
		const FVector Door = Level->OpDoorSpot();
		const FVector WomanPath[] = { FVector(402, 430, 0), FVector(430, 1340, 0), FVector(430, Door.Y - 30.f, 0), FVector(Door.X + 230.f, Door.Y, 0) };
		const bool bStopped = WalkPerson(Woman, WomanAnim, WomanPath[HandLeg], 92.f, 0.72f, Dt);
		if (bStopped && HandLeg < 3)
		{
			++HandLeg;
		}
		if (WomanAnim)
		{
			WomanAnim->LookTowards(0.f, 0.f);
		}
		Player->CarryEyesTo(DoctorHold + FVector(0.f, 0.f, FMath::Sin(HandTimer * 6.f) * 1.3f));

		// Her shoes on the hall floor, each with its echo coming back off the far end.
		StepWait -= Dt;
		if (!bStopped && StepWait <= 0.f)
		{
			StepWait = 0.62f;
			EchoWait = 0.24f;
			if (!HG::Extra(this, (StepCount++ % 2) ? TEXT("step_hard_2") : TEXT("step_hard_1"), WomanAt, 0.6f, FMath::FRandRange(0.92f, 1.f)))
			{
				HG::Play(this, TEXT("step_1"), 0.4f, 0.8f);
			}
		}
		if (EchoWait > 0.f && (EchoWait -= Dt) <= 0.f)
		{
			if (!HG::Extra(this, (StepCount % 2) ? TEXT("step_hard_1") : TEXT("step_hard_2"), WomanAt + FVector(0, 500, 0), 0.5f, 0.84f))
			{
				HG::Play(this, TEXT("step_1"), 0.12f, 0.7f);
			}
		}

		if (WalkPerson(WardMom, MomAnim, FVector(296, 150, 0), 66.f, 0.55f, Dt))
		{
			FacePerson(WardMom, Eyes, Dt, 2.f);
		}
		PersonLooks(WardMom, MomAnim, Eyes, 0.5f);

		// How far he has been taken from her. The colour is gone by the end of the first hall; the
		// doctors go on changing for most of the corridor after it.
		const float Away = FMath::Clamp((WomanAt.Y - 152.f) / 950.f, 0.f, 1.f);
		Player->SetSaturation(FMath::Lerp(1.3f, 0.12f, FMath::SmoothStep(0.f, 1.f, Away * 1.1f)));
		// The birds and the quiet of the ward go with the colour.
		SetWardSound(1.f - FMath::SmoothStep(0.f, 1.f, Away * 1.6f));
		const float Wrong = FMath::SmoothStep(0.05f, 1.f, FMath::Clamp(static_cast<float>(WomanAt.Y - 152.0) / 2300.f, 0.f, 1.f));
		if (WomanAnim) { WomanAnim->SetWrong(Wrong); }
		if (SecondAnim) { SecondAnim->SetWrong(Wrong); }
		for (UHGFaceAnim* DoctorFace : DoctorFaces)
		{
			if (DoctorFace) { DoctorFace->SetWrong(Wrong); }
		}
		// The one behind also grows: too tall for the hall by the end of it.
		Second->SetActorScale3D(FVector(1.f, 1.f, 1.f + 0.13f * Wrong));
		if (!FollowLight)
		{
			FollowLight = AddFaceLight(Second);
		}
		// The hall lamps are in the thousands but metres away; this one is a hand's width from her face.
		FollowLight->SetIntensity(110.f * Wrong);

		if (HandTimer > 6.2f && !bHushed)
		{
			bHushed = true;
			Say(TEXT("\"Shh. Nobody is going to hurt you.\""), 4.5f);
			HG::Voice(this, TEXT("doc_hush"), SecondFace);
		}

		if (!bCorridor)
		{
			Player->SetLens(FMath::Lerp(82.f, 88.f, FMath::Clamp(HandTimer / 3.f, 0.f, 1.f)));
			// The other doctor lets her past and then follows, a few steps behind, all the way.
			bSecondFollows = bSecondFollows || WomanAt.Y > Second->GetActorLocation().Y + 150.f;
			if (bSecondFollows)
			{
				const FVector SecondPath[] = { FVector(446, 500, 0), FVector(432, Door.Y - 195.f, 0) };
				if (WalkPerson(Second, SecondAnim, SecondPath[SecondLeg], 92.f, 0.72f, Dt) && SecondLeg < 1)
				{
					++SecondLeg;
				}
			}
			else if (WalkPerson(Second, SecondAnim, FVector(505, 300, 0), 70.f, 0.6f, Dt))
			{
				FacePerson(Second, Eyes, Dt, 2.f);
			}
			PersonLooks(Second, SecondAnim, Eyes, 0.7f);

			if (bShots)
			{
				// Pictures: mom left in the doorway, the one following, the one carrying, and the far end.
				Player->TurnTowards(HandTimer < 4.4f ? MomFace : ((HandTimer > 8.4f && HandTimer < 10.2f) ? WomanFace : SecondFace), Dt, 2.5f);
				if (HandTimer > 4.2f) { Shot(3, TEXT("ward-10-doorway.png")); }
				if (HandTimer > 8.2f) { Shot(4, TEXT("ward-11-following.png")); }
				if (HandTimer > 10.f) { Shot(5, TEXT("ward-12-carrier.png")); }
				if (HandTimer > 12.4f) { Shot(6, TEXT("ward-13-far.png")); }
			}
			else if (HandTimer > 0.3f && HandTimer < 2.6f)
			{
				Player->TurnTowards(MomFace, Dt, 1.4f);
			}

			if (HandLeg >= 2)
			{
				// End of the second scene, and no break in the walk: a card comes up over it.
				bHandedOver = true;
				bCorridor = true;
				CardText = TEXT("THE CORRIDOR");
				CardStart = GetWorld()->GetTimeSeconds();
				UE_LOG(LogHG, Log, TEXT("HGTEST HANDOVER done seconds=%d wrong=%.2f"), FMath::RoundToInt32(RoundSeconds), Wrong);
			}
		}
		else
		{
			TickCorridor(Dt, Player, Wrong);
		}
		break;
	}
	}
}

UPointLightComponent* AHGGameMode::AddFaceLight(AActor* Person)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(Person);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetupAttachment(Person->GetRootComponent());
	Light->SetRelativeLocation(FVector(0, 62, 156));
	Light->SetIntensityUnits(ELightUnits::Unitless);
	Light->SetIntensity(0.f);
	Light->SetLightColor(FLinearColor(0.72f, 0.82f, 1.f));
	Light->SetAttenuationRadius(120.f);
	Light->SetCastShadows(false);
	Light->RegisterComponent();
	Person->AddInstanceComponent(Light);
	return Light;
}

void AHGGameMode::TickCorridor(float Dt, AHGPlayer* Player, float Wrong)
{
	AActor* Woman = WardDoctors[0];
	AActor* Second = WardDoctors[1];
	UHGPersonAnim* SecondAnim = DoctorAnims[1];
	const FVector Eyes = Player->EyeLocation();
	const FVector WomanAt = Woman->GetActorLocation();
	const FVector Door = Level->OpDoorSpot();
	const FVector SecondFace = PersonPoint(Second, 9.f, 0.f, 166.f);
	const bool bFirst = CorridorTimer <= 0.f;
	CorridorTimer += Dt;
	// How far along the corridor he is, from its start to the door.
	const float Along = FMath::Clamp(static_cast<float>((WomanAt.Y - 1340.0) / FMath::Max(Door.Y - 1340.0, 1.0)), 0.f, 1.f);
	auto Shot = [this](int32 Bit, const TCHAR* Name)
	{
		if (bShots && !(HandShots & (1 << Bit)))
		{
			HandShots |= 1 << Bit;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / Name, true, false);
			UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), Name);
		}
	};

	if (bFirst)
	{
		// A low rumble under everything, his own heart, and people talking behind a door somewhere.
		HG::ExtraBed(this, TEXT("hall_low"), 0.5f);
		HeartBed = HG::ExtraBed(this, TEXT("heart"), 0.06f);
		HG::ExtraAt(this, TEXT("hall_voices"), FVector(300, 2150, 120), 0.8f);
		for (AActor* Person : Staff)
		{
			Person->SetActorHiddenInGame(false);
		}
		LampLevel.Init(1.f, Level->HallLampAlong().Num());
	}
	if (HeartBed)
	{
		// Louder and quicker the nearer the door.
		HeartBed->SetVolumeMultiplier(0.06f + 0.9f * Along * Along);
		HeartBed->SetPitchMultiplier(1.f + 0.22f * Along);
	}
	// The hall opens out round him as it goes: the lens widens with the ceiling.
	Player->SetLens(FMath::Lerp(88.f, 101.f, FMath::SmoothStep(0.f, 1.f, Along)));

	// The lamps go out behind him, one at a time, each a little after he has passed it. The way back goes dark.
	const TArray<float>& LampAlong = Level->HallLampAlong();
	LampWait -= Dt;
	if (LampsOut < LampAlong.Num() && LampWait <= 0.f && LampAlong[LampsOut] < WomanAt.Y - 560.f)
	{
		++LampsOut;
		LampWait = 0.85f;
		HG::Play(this, TEXT("tick"), 0.55f, 0.55f);
	}
	for (int32 i = 0; i < LampsOut; ++i)
	{
		if (LampLevel[i] > 0.f)
		{
			// It stutters on the way out.
			LampLevel[i] = FMath::FInterpConstantTo(LampLevel[i], 0.f, Dt, 3.2f);
			Level->SetHallLamp(i, LampLevel[i] * (FMath::FRand() < 0.35f ? 0.15f : 1.f));
		}
	}

	// The people standing in the corridor watch him come. Once he has been carried past, each one
	// turns and follows, in step, with the same face as the doctor already behind him.
	static const float StaffLane[] = { 498.f, 366.f };
	static const float StaffGap[] = { 255.f, 335.f };
	for (int32 i = 0; i < Staff.Num(); ++i)
	{
		AActor* Person = Staff[i];
		UHGPersonAnim* Anim = StaffAnims.IsValidIndex(i) ? StaffAnims[i].Get() : nullptr;
		if (StaffFaces.IsValidIndex(i) && StaffFaces[i])
		{
			StaffFaces[i]->SetWrong(Wrong);
		}
		if (Anim)
		{
			Anim->SetWrong(Wrong);
		}
		Person->SetActorScale3D(FVector(1.f, 1.f, 1.f + 0.13f * Wrong));
		if (StaffLights.IsValidIndex(i) && StaffLights[i])
		{
			StaffLights[i]->SetIntensity(90.f * Wrong);
		}
		const FVector At = Person->GetActorLocation();
		const float Behind = WomanAt.Y - At.Y;
		if (!(StaffJoined & (1 << i)) && Behind > 140.f)
		{
			StaffJoined |= 1 << i;
		}
		if ((StaffJoined & (1 << i)) && Behind >= StaffGap[i])
		{
			StaffWalking |= 1 << i;
		}
		if (StaffWalking & (1 << i))
		{
			// They stop short of the door and stand there, looking in after him.
			if (WalkPerson(Person, Anim, FVector(StaffLane[i], Door.Y - 150.f - (StaffGap[i] - 165.f) * 0.5f, 0), 92.f, 0.72f, Dt))
			{
				FacePerson(Person, Eyes, Dt, 2.f);
			}
		}
		else if (StaffJoined & (1 << i))
		{
			FacePerson(Person, Eyes, Dt, 1.6f);
		}
		PersonLooks(Person, Anim, Eyes, 0.7f);
	}

	// The doctor behind keeps his place until the door is near, then goes past on the left to open it.
	if (!bOvertaking && WomanAt.Y > Door.Y - 1300.f)
	{
		bOvertaking = true;
		OvertakeVia = FVector(548, Second->GetActorLocation().Y + 260.f, 0);
	}
	bool bSecondAtDoor = false;
	if (!bOvertaking)
	{
		WalkPerson(Second, SecondAnim, FVector(432, Door.Y - 195.f, 0), 92.f, 0.72f, Dt);
	}
	else
	{
		const FVector Legs[] = { OvertakeVia, FVector(548, Door.Y - 80.f, 0) };
		const bool bThere = WalkPerson(Second, SecondAnim, Legs[OvertakeLeg], 165.f, 1.25f, Dt);
		if (bThere && OvertakeLeg < 1)
		{
			++OvertakeLeg;
		}
		bSecondAtDoor = bThere && OvertakeLeg == 1;
		if (bSecondAtDoor)
		{
			// She holds it open and watches him brought in.
			FacePerson(Second, Eyes, Dt, 2.5f);
		}
	}
	PersonLooks(Second, SecondAnim, Eyes, 0.7f);

	if (bSecondAtDoor && !bOpDoor && WomanAt.Y > Door.Y - 520.f)
	{
		bOpDoor = true;
		HG::Extra(this, TEXT("door_handle"), Door + FVector(0, 0, 100), 1.f);
		HG::Extra(this, TEXT("op_door"), Door + FVector(0, 0, 100), 1.f);
	}
	if (bOpDoor)
	{
		OpDoorOpen = FMath::FInterpConstantTo(OpDoorOpen, 1.f, Dt, 0.5f);
		Level->SetOpDoorOpen(FMath::SmoothStep(0.f, 1.f, OpDoorOpen));
		if (!bShots && OpDoorOpen < 0.8f)
		{
			// The light pulls his eyes to it.
			Player->TurnTowards(Door + FVector(0, 0, 140), Dt, 1.3f);
		}
	}

	if (!bNearlySaid && WomanAt.Y > 2760.f)
	{
		bNearlySaid = true;
		Say(TEXT("\"Nearly there now. You're being such a brave boy.\""), 5.f);
		HG::Voice(this, TEXT("doc_nearly"), PersonPoint(Woman, 9.f, 0.f, 160.f));
	}
	if (!bCartHeard && WomanAt.Y > 3050.f)
	{
		bCartHeard = true;
		HG::Extra(this, TEXT("cart"), FVector(600, 3600, 60), 0.9f);
	}
	if (!bHereSaid && bOpDoor && WomanAt.Y > Door.Y - 260.f)
	{
		bHereSaid = true;
		Say(TEXT("\"Here we are. In you go.\""), 4.f);
		HG::Voice(this, TEXT("doc_here"), SecondFace);
	}

	// Through the door the picture burns out to white, and that is where the scene ends.
	// He is let see the room, and the chair in it, for a moment first.
	const FVector Chair(Door.X + 260.f, Door.Y, 60.f);
	WhiteOut = FMath::Clamp(static_cast<float>(WomanAt.X - (Door.X + 70.0)) / 150.f, 0.f, 1.f);
	if (HandLeg >= 3 && !bShots && WhiteOut <= 0.f)
	{
		Player->TurnTowards(Chair, Dt, 1.2f);
	}
	if (WhiteOut >= 1.f)
	{
		WhiteHeld += Dt;
	}

	if (bShots)
	{
		const float Y = static_cast<float>(WomanAt.Y);
		const FVector Ahead(470, Y + 900.f, 170);
		FVector Look = SecondFace;
		if (Y > 1750.f && Y < 2250.f) { Look = Ahead; }
		else if (Y >= 2250.f && Y < 2640.f && Staff.Num() > 0) { Look = PersonPoint(Staff[0], 9.f, 0.f, 165.f); }
		else if (Y >= 3260.f && Y < 3640.f && Staff.Num() > 1) { Look = PersonPoint(Staff[1], 9.f, 0.f, 170.f); }
		else if (HandLeg >= 3) { Look = Chair; }
		else if (Y >= 4330.f) { Look = Door + FVector(-40, 0, 150); }
		Player->TurnTowards(Look, Dt, 2.5f);
		if (CorridorTimer > 2.6f) { Shot(7, TEXT("ward-14-corridor.png")); }
		if (Y > 2060.f) { Shot(8, TEXT("ward-15-ahead.png")); }
		if (Y > 2400.f) { Shot(9, TEXT("ward-16-waiting.png")); }
		if (Y > 3160.f) { Shot(10, TEXT("ward-17-dark-behind.png")); }
		if (Y > 3420.f) { Shot(11, TEXT("ward-18-second.png")); }
		if (Y > 4100.f) { Shot(12, TEXT("ward-19-three.png")); }
		if (Y > Door.Y - 150.f) { Shot(13, TEXT("ward-20-door.png")); }
		if (WomanAt.X > Door.X + 40.f) { Shot(14, TEXT("ward-21-room.png")); }
	}

	if (WhiteHeld > 1.6f && !bChair)
	{
		// End of the third scene. Behind the white he is turned round and sat down.
		bCorridorDone = true;
		bChair = true;
		UE_LOG(LogHG, Log, TEXT("HGTEST CORRIDOR done seconds=%d lamps=%d/%d staff=%d"), FMath::RoundToInt32(RoundSeconds), LampsOut, LampAlong.Num(), StaffWalking);
	}
}

void AHGGameMode::TickChair(float Dt, AHGPlayer* Player)
{
	AActor* Woman = WardDoctors[0];
	AActor* Second = WardDoctors[1];
	UHGPersonAnim* WomanAnim = DoctorAnims[0];
	UHGPersonAnim* SecondAnim = DoctorAnims[1];
	const FVector Chair = Level->ChairSpot();
	const FVector Door = Level->OpDoorSpot();
	const bool bFirst = ChairTimer <= 0.f;
	ChairTimer += Dt;
	const float T = ChairTimer;
	auto Once = [this](int32 Bit)
	{
		const bool bNew = !(ChairCues & (1 << Bit));
		ChairCues |= 1 << Bit;
		return bNew;
	};
	auto Shot = [this](int32 Bit, const TCHAR* Name)
	{
		if (bShots && !(HandShots & (1 << Bit)))
		{
			HandShots |= 1 << Bit;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / Name, true, false);
			UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), Name);
		}
	};
	// Under the cloth every voice and every sound in the room is dull, as if through a wall.
	auto Dull = [](UAudioComponent* Sound)
	{
		if (Sound)
		{
			Sound->SetLowPassFilterEnabled(true);
			Sound->SetLowPassFilterFrequency(620.f);
		}
	};

	if (bFirst)
	{
		// She sits in the chair, facing the door, with him on her lap and her arms round him.
		Woman->SetActorLocationAndRotation(FVector(Chair.X + 4.f, Chair.Y, -38.f), FRotator(0.f, 180.f + MomYawOffset, 0.f));
		if (WomanAnim)
		{
			WomanAnim->SetClip(PersonIdle);
			WomanAnim->SetSit(1.f);
			WomanAnim->SetHold(1.f);
			WomanAnim->LookTowards(0.f, -8.f);
		}
		// The other doctor is at the table, with her back half turned, busy with what is on it.
		Second->SetActorLocationAndRotation(FVector(Chair.X - 105.f, Chair.Y + 64.f, 0.f), FRotator(0.f, 90.f + MomYawOffset, 0.f));
		if (SecondAnim)
		{
			SecondAnim->SetClip(PersonIdle);
		}
		// The two who followed stand just inside the door and watch.
		static const FVector Inside[] = { FVector(58, -30, 0), FVector(50, 32, 0) };
		for (int32 i = 0; i < Staff.Num(); ++i)
		{
			Staff[i]->SetActorLocationAndRotation(FVector(Door.X, Door.Y, 0.f) + Inside[i], FRotator(0.f, MomYawOffset, 0.f));
			if (StaffAnims.IsValidIndex(i) && StaffAnims[i])
			{
				StaffAnims[i]->SetClip(PersonIdle);
			}
		}
		// Everything the walk here had done to them, in case the story was started at this scene.
		EveryoneWrong(Player);
		for (int32 i = 0; i < Staff.Num(); ++i)
		{
			Staff[i]->SetActorHiddenInGame(false);
			if (StaffLights.IsValidIndex(i) && StaffLights[i]) { StaffLights[i]->SetIntensity(90.f); }
		}
		SetWardSound(0.f);
		if (!HeartBed)
		{
			HeartBed = HG::ExtraBed(this, TEXT("heart"), 0.45f);
		}
		Level->SetOpDoorOpen(1.f);
		CardText = TEXT("THE CHAIR");
		CardStart = GetWorld()->GetTimeSeconds();
		Player->SetLens(84.f);
		Player->CarryEyesTo(PersonPoint(Woman, 38.f, 0.f, 136.f));
		Player->LookAt(FVector(Door.X, Door.Y, 105.f));
		if (HeartBed)
		{
			HeartBed->SetVolumeMultiplier(0.45f);
			HeartBed->SetPitchMultiplier(1.15f);
		}
	}
	Player->CarryEyesTo(PersonPoint(Woman, 38.f, 0.f, 136.f));
	const FVector Eyes = Player->EyeLocation();
	const FVector SecondFace = PersonPoint(Second, 9.f, 0.f, 185.f);
	for (int32 i = 0; i < Staff.Num(); ++i)
	{
		PersonLooks(Staff[i], StaffAnims.IsValidIndex(i) ? StaffAnims[i].Get() : nullptr, Eyes, 0.6f);
	}

	// The white clears and he sees where he is.
	WhiteOut = 1.f - FMath::SmoothStep(1.4f, 4.4f, T);

	if (T > 4.6f && Once(0))
	{
		Say(TEXT("\"There now. You sit with me. That's it.\""), 4.5f);
		HG::Voice(this, TEXT("chair_sit"), PersonPoint(Woman, 9.f, 0.f, 150.f));
		HG::Extra(this, TEXT("chair_creak"), Chair + FVector(0, 0, 50), 0.8f);
	}
	if (T > 6.2f && Once(1))
	{
		HG::Extra(this, TEXT("tray"), Chair + FVector(-105, 118, 75), 0.9f);
	}
	if (T > 7.8f && Once(2))
	{
		HG::Extra(this, TEXT("glove"), SecondFace - FVector(0, 0, 60), 0.9f);
	}
	if (T > 8.8f)
	{
		// She turns from the table and comes to stand over him.
		if (WalkPerson(Second, SecondAnim, FVector(Chair.X - 78.f, Chair.Y + 12.f, 0.f), 55.f, 0.5f, Dt))
		{
			FacePerson(Second, Eyes, Dt, 3.f);
		}
		PersonLooks(Second, SecondAnim, Eyes, 0.9f);
	}
	else if (SecondAnim)
	{
		SecondAnim->LookTowards(0.f, -14.f);
	}
	if (T > 10.2f && Once(3))
	{
		Say(TEXT("\"We'll just cover your eyes. There.\""), 4.f);
		HG::Voice(this, TEXT("chair_cover"), SecondFace);
		HG::Extra(this, TEXT("cloth"), Eyes, 1.f);
	}

	// The cloth comes down from above, and from here on it is all he has.
	const float ClothOn = 12.2f;
	ClothDown = FMath::SmoothStep(11.f, ClothOn, T);
	if (T < 11.f)
	{
		ChairLook = Player->GetControlRotation();
	}
	const float C = T - ClothOn;
	if (C > 0.f)
	{
		// Turning his head moves the light and the shadows about on the cloth; the stain stays where it is.
		const FRotator Turned = (Player->GetControlRotation() - ChairLook).GetNormalized();
		ClothShift = FVector2D(FMath::Clamp(Turned.Yaw / 50.f, -1.f, 1.f), FMath::Clamp(Turned.Pitch / 40.f, -1.f, 1.f));
		ClothBusy = FMath::SmoothStep(0.5f, 3.f, C) * (1.f - FMath::SmoothStep(15.f, 22.f, C));
		ClothLoom = FMath::SmoothStep(1.5f, 6.2f, C) * (1.f - 0.7f * FMath::SmoothStep(17.f, 24.f, C));

		if (C > 2.f && Once(4))
		{
			Say(TEXT("\"Hold still, sweetheart.\""), 3.5f);
			Dull(HG::VoiceBed(this, TEXT("chair_still"), 0.9f));
		}
		if (C > 3.8f && Once(5))
		{
			Dull(HG::ExtraBed(this, TEXT("instrument"), 0.5f));
			Dull(HG::ExtraBed(this, TEXT("chair_creak"), 0.45f));
		}
		if (C > 5.2f && Once(6))
		{
			Say(TEXT("\"Hold him.\""), 3.f);
			Dull(HG::VoiceBed(this, TEXT("chair_hold"), 0.9f));
		}
		// The moment itself is the quietest: his heart drops away, a thin ringing comes up, and the red starts.
		if (C > 6.5f && Once(7))
		{
			RingBed = HG::ExtraBed(this, TEXT("ring"), 0.02f);
			Dull(HG::ExtraBed(this, TEXT("instrument"), 0.35f));
		}
		ClothRed = FMath::Clamp((C - 7.f) / 15.5f, 0.f, 1.15f);
		if (HeartBed)
		{
			const float Before = FMath::Lerp(0.45f, 0.95f, FMath::SmoothStep(0.f, 6.f, C));
			const float After = FMath::Lerp(0.1f, 0.55f, FMath::SmoothStep(16.f, 23.f, C));
			HeartBed->SetVolumeMultiplier(FMath::Lerp(Before, After, FMath::SmoothStep(6.3f, 7.3f, C)));
			HeartBed->SetPitchMultiplier(FMath::Lerp(1.15f, 1.3f, FMath::SmoothStep(0.f, 6.f, C)) - 0.35f * FMath::SmoothStep(6.3f, 9.f, C));
		}
		if (RingBed)
		{
			RingBed->SetVolumeMultiplier(0.02f + 0.33f * FMath::SmoothStep(6.5f, 9.5f, C) * (1.f - FMath::SmoothStep(17.f, 22.f, C)));
		}
		if (C > 12.5f && Once(8))
		{
			Say(TEXT("\"Shh. Shh. Nearly done.\""), 4.f);
			Dull(HG::VoiceBed(this, TEXT("chair_shh"), 0.8f));
		}
		if (C > 21.5f && Once(9))
		{
			Say(TEXT("\"There. All finished. Good boy.\""), 4.5f);
			Dull(HG::VoiceBed(this, TEXT("chair_done"), 0.9f));
		}
		BlackOut = FMath::SmoothStep(24.5f, 27.5f, C);
	}

	if (bShots)
	{
		// Pictures: the room from her lap, her arms round him, the doctor over him, then the cloth four times.
		if (T < 5.8f) { Player->TurnTowards(FVector(Door.X, Door.Y + 40.f, 110.f), Dt, 3.f); }
		else if (T < 8.f) { Player->TurnTowards(PersonPoint(Woman, 70.f, 0.f, 62.f), Dt, 3.f); }
		else { Player->TurnTowards(SecondFace, Dt, 3.f); }
		if (T > 5.6f) { Shot(15, TEXT("ward-22-lap.png")); }
		if (T > 7.8f) { Shot(16, TEXT("ward-23-held.png")); }
		if (T > 10.8f) { Shot(17, TEXT("ward-24-over-him.png")); }
		if (C > 3.2f) { Shot(18, TEXT("ward-25-cloth.png")); }
		if (C > 6.3f) { Shot(19, TEXT("ward-26-shadow.png")); }
		if (C > 12.f) { Shot(20, TEXT("ward-27-red.png")); }
		if (C > 22.5f) { Shot(21, TEXT("ward-28-soaked.png")); }
	}

	if (C > 28.5f && !bEscape)
	{
		// End of the fourth scene. In the dark he is put somewhere to sleep it off.
		bChairDone = true;
		ChairCues |= (ClothDown >= 1.f && ClothRed >= 1.f) ? 1024 : 0;
		bEscape = true;
		UE_LOG(LogHG, Log, TEXT("HGTEST CHAIR done seconds=%d red=%.2f cues=%d"), FMath::RoundToInt32(RoundSeconds), ClothRed, ChairCues);
	}
}

void AHGGameMode::EveryoneWrong(AHGPlayer* Player)
{
	Player->SetSaturation(0.12f);
	for (UHGPersonAnim* Anim : DoctorAnims) { if (Anim) { Anim->SetWrong(1.f); } }
	for (UHGPersonAnim* Anim : StaffAnims) { if (Anim) { Anim->SetWrong(1.f); } }
	for (UHGFaceAnim* Face : DoctorFaces) { if (Face) { Face->SetWrong(1.f); } }
	for (UHGFaceAnim* Face : StaffFaces) { if (Face) { Face->SetWrong(1.f); } }
	WardDoctors[1]->SetActorScale3D(FVector(1.f, 1.f, 1.13f));
	for (AActor* Person : Staff)
	{
		Person->SetActorScale3D(FVector(1.f, 1.f, 1.13f));
	}
	if (!FollowLight)
	{
		FollowLight = AddFaceLight(WardDoctors[1]);
	}
	FollowLight->SetIntensity(110.f);
}

void AHGGameMode::SetWardSound(float Level01)
{
	for (int32 i = 0; i < WardBeds.Num(); ++i)
	{
		if (WardBeds[i])
		{
			WardBeds[i]->SetVolumeMultiplier(WardBedFull[i] * FMath::Clamp(Level01, 0.f, 1.f));
		}
	}
}

void AHGGameMode::AddBed(const FVector& Centre, float Yaw)
{
	// The pack's bed is three pieces a hundred times too big, lying along its own Y with its origin at one end.
	const FRotator Turn(0.f, Yaw, 0.f);
	USceneComponent* Room = Level->GetRootComponent();
	for (UStaticMesh* Mesh : BedParts)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Level);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetStaticMesh(Mesh);
		Piece->SetupAttachment(Room);
		Piece->SetRelativeLocation(Centre - Turn.RotateVector(FVector(0, 100, 0)) + FVector(0, 0, 3));
		Piece->SetRelativeRotation(Turn);
		Piece->SetRelativeScale3D(FVector(0.01f));
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->RegisterComponent();
		Level->AddInstanceComponent(Piece);
	}
	// Solid from the mattress up, so nobody walks through it; open underneath, so a child on hands and knees fits.
	HG::AddShape(Level, Room, TEXT("Cube"), Centre + FVector(0, 0, 58), FVector(1.3f, 2.1f, 0.1f), FLinearColor::Black, 0.f, true, Turn)->SetVisibility(false);
	const bool bAcross = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Yaw))) > 0.5f;
	const FVector Half = bAcross ? FVector(105, 65, 30) : FVector(65, 105, 30);
	HideBoxes.Add(FBox(Centre + FVector(0, 0, 30) - Half, Centre + FVector(0, 0, 30) + Half));
}

void AHGGameMode::ToyLanded(const FVector& At)
{
	if (!bEscape || bEnding)
	{
		return;
	}
	// Whoever has that stretch of floor goes to see what squeaked.
	const FVector2D Where(At.X, At.Y);
	for (FHGSearcher& S : Searchers)
	{
		if (S.State < 4 && S.Zone.ExpandBy(260.f).IsInside(Where))
		{
			S.State = 2;
			S.Target = FVector2D(FMath::Clamp(Where.X, S.Zone.Min.X, S.Zone.Max.X), FMath::Clamp(Where.Y, S.Zone.Min.Y, S.Zone.Max.Y));
		}
	}
}

void AHGGameMode::BeginEscape(AHGPlayer* Player)
{
	bEscapeBegun = true;
	const FVector Corner = Level->EscapeOrigin();
	const FVector2D Corner2(Corner.X, Corner.Y);
	EveryoneWrong(Player);
	SetWardSound(0.f);

	// The cloth and the chair are over.
	ClothDown = ClothRed = ClothLoom = ClothBusy = 0.f;
	WhiteOut = 0.f;
	BlackOut = 1.f;
	Level->SetOpDoorOpen(0.f);
	if (RingBed)
	{
		RingBed->Stop();
	}
	if (!HeartBed)
	{
		HeartBed = HG::ExtraBed(this, TEXT("heart"), 0.08f);
	}
	DroneBed = HG::ExtraBed(this, TEXT("escape_drone"), 0.4f);
	for (int32 i = 0; i < Staff.Num(); ++i)
	{
		Staff[i]->SetActorHiddenInGame(true);
		if (StaffLights.IsValidIndex(i) && StaffLights[i]) { StaffLights[i]->SetIntensity(0.f); }
	}

	// The two doctors, on their feet and too tall, each with a stretch of floor to walk.
	AActor* Woman = WardDoctors[0];
	if (UHGPersonAnim* WomanAnim = DoctorAnims[0])
	{
		WomanAnim->SetSit(0.f);
		WomanAnim->SetHold(0.f);
	}
	Woman->SetActorScale3D(FVector(1.f, 1.f, 1.13f));
	StaffLights.Add(AddFaceLight(Woman));
	StaffLights.Last()->SetIntensity(80.f);
	FollowLight->SetIntensity(80.f);

	FHGSearcher InWard;
	InWard.Doctor = 1;
	InWard.Zone = FBox2D(Corner2 + FVector2D(30, 470), Corner2 + FVector2D(470, 1880));
	InWard.Ends[0] = Corner2 + FVector2D(250, 560);
	InWard.Ends[1] = Corner2 + FVector2D(250, 1790);
	FHGSearcher InCorridor;
	InCorridor.Doctor = 0;
	InCorridor.Zone = FBox2D(Corner2 + FVector2D(660, 1990), Corner2 + FVector2D(1600, 2130));
	InCorridor.Ends[0] = Corner2 + FVector2D(760, 2060);
	InCorridor.Ends[1] = Corner2 + FVector2D(1540, 2060);
	InCorridor.CallWait = 14.f;
	Searchers = { InWard, InCorridor };
	for (const FHGSearcher& S : Searchers)
	{
		const FVector2D Way = S.Ends[0] - S.Ends[1];
		WardDoctors[S.Doctor]->SetActorLocationAndRotation(FVector(S.Ends[1].X, S.Ends[1].Y, 0.f), FRotator(0.f, FVector(Way.X, Way.Y, 0.f).Rotation().Yaw + MomYawOffset, 0.f));
	}

	// His mother, in the last room, by the window, not knowing where he is.
	WardMom->SetActorLocationAndRotation(Corner + FVector(2110, 2090, 0), FRotator(0.f, 180.f + MomYawOffset, 0.f));
	if (MomAnim)
	{
		MomAnim->SetClip(PersonIdle);
		MomAnim->SetHold(0.f);
		MomAnim->LookTowards(0.f, 0.f);
	}
	if (MomFaceAnim)
	{
		MomFaceAnim->SetWarm(0.1f);
		MomFaceAnim->SetSad(0.85f);
	}

	// And him, standing by the bed he woke on, on his own feet again.
	Player->SetCarried(false);
	Player->SetCrawlAllowed(true);
	Player->SetActorLocation(Corner + FVector(250, 190, 60));
	Player->LookAt(Corner + FVector(415, 400, 30));
	Player->SetLens(92.f);
	Player->SetSaturation(0.18f);
	CardText = TEXT("THE ESCAPE");
	CardStart = GetWorld()->GetTimeSeconds() + 1.f;
	Checkpoint = 0;
}

void AHGGameMode::TickSearcher(FHGSearcher& S, float Dt, AHGPlayer* Player)
{
	AActor* Doctor = WardDoctors[S.Doctor];
	UHGPersonAnim* Anim = DoctorAnims[S.Doctor];
	const bool bSecond = S.Doctor == 1;
	const FVector At = Doctor->GetActorLocation();
	const FVector2D At2(At.X, At.Y);
	const FVector Head = At + FVector(0, 0, 186);
	const FVector Eyes = Player->EyeLocation();
	const FVector2D Him(Eyes.X, Eyes.Y);
	const float Apart = FVector2D::Distance(At2, Him);
	const bool bBusy = CaughtTimer >= 0.f || bEnding;
	auto Kept = [&S](const FVector2D& Point)
	{
		return FVector2D(FMath::Clamp(Point.X, S.Zone.Min.X, S.Zone.Max.X), FMath::Clamp(Point.Y, S.Zone.Min.Y, S.Zone.Max.Y));
	};

	// Can this one see him? Not under a bed or the counter, not far off in the dark, not behind them, not through a wall.
	bool bSees = false;
	if (!bHidden && !bBusy)
	{
		const float Range = Player->IsCrawling() ? 520.f : (Player->NoiseRadius() > 500.f ? 1250.f : 950.f);
		if (Apart < Range)
		{
			const float Facing = Doctor->GetActorRotation().Yaw - MomYawOffset + (Anim ? Anim->HeadYaw() : 0.f);
			const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees(Facing, (Eyes - Head).Rotation().Yaw));
			if (Off < 80.f || Apart < 110.f)
			{
				FCollisionQueryParams Params(FName(TEXT("HGSight")), false, Player);
				for (AActor* Person : WardDoctors) { Params.AddIgnoredActor(Person); }
				Params.AddIgnoredActor(WardMom);
				if (EscapeToy) { Params.AddIgnoredActor(EscapeToy); }
				FHitResult Hit;
				bSees = !GetWorld()->LineTraceSingleByChannel(Hit, Head, Eyes, ECC_Visibility, Params);
			}
		}
	}
	if (bSees && !bShots)
	{
		// Being sure takes a moment: less of one close up, more of one if he is down on the floor.
		const float Rate = (Apart < 300.f ? 1.7f : (Apart < 600.f ? 1.f : 0.55f)) * (Player->IsCrawling() ? 0.6f : 1.f);
		S.Suspicion = FMath::Min(1.2f, S.Suspicion + Rate * Dt);
		S.SinceSeen = 0.f;
	}
	else
	{
		S.Suspicion = FMath::Max(0.f, S.Suspicion - 0.3f * Dt);
		S.SinceSeen += Dt;
	}

	// Feet on a hard floor carry. They go to where the sound was.
	const float Noise = Player->NoiseRadius();
	if (!bBusy && Noise > 0.f && Apart < Noise && S.State < 4)
	{
		if (S.State != 2 && S.CallWait < 100.f)
		{
			if (!bSecond)
			{
				HG::Voice(this, TEXT("esc_woman_hear"), Head);
				Say(TEXT("\"I can hear you.\""), 3.f);
			}
			S.CallWait = FMath::FRandRange(13.f, 20.f);
		}
		S.State = 2;
		S.Target = Kept(Him);
	}
	if (S.Suspicion >= 1.f && S.State != 4 && !bBusy)
	{
		S.State = 4;
		S.Target = Kept(Him);
		HG::ExtraBed(this, TEXT("spotted"), 0.7f);
		HG::Voice(this, bSecond ? TEXT("esc_other_there") : TEXT("esc_woman_there"), Head);
		Say(bSecond ? TEXT("\"There he is.\"") : TEXT("\"There you are.\""), 3.f);
	}

	FVector2D Goal = At2;
	float Speed = 1.f, Rate = 1.f;
	switch (S.State)
	{
	case 0:
		Goal = S.Ends[S.Heading];
		Speed = 78.f;
		Rate = 0.62f;
		break;
	case 1:
		S.Timer -= Dt;
		if (S.Timer <= 0.f)
		{
			S.Heading = 1 - S.Heading;
			S.State = 0;
		}
		break;
	case 2:
		Goal = S.Target;
		Speed = 104.f;
		Rate = 0.82f;
		break;
	case 3:
		S.Timer -= Dt;
		if (S.Timer <= 0.f)
		{
			S.State = 0;
		}
		break;
	default:
		// Coming for him: a fast walk, never a run. They go to where they last saw him, or, if they
		// watched him get under something, to that.
		if (bSees || (bHidden && S.bSawHide))
		{
			S.Target = Kept(Him);
		}
		Goal = S.Target;
		Speed = 150.f;
		Rate = 1.18f;
		if (!bSees && !(bHidden && S.bSawHide) && S.SinceSeen > 5.f)
		{
			S.State = 3;
			S.Timer = 4.f;
			S.Suspicion = 0.4f;
		}
		break;
	}
	const bool bArrived = WalkPerson(Doctor, Anim, FVector(Goal.X, Goal.Y, 0.f), Speed, Rate, Dt);
	if (bArrived && S.State == 0)
	{
		S.State = 1;
		S.Timer = 2.6f;
	}
	else if (bArrived && S.State == 2)
	{
		S.State = 3;
		S.Timer = 5.f;
	}

	if (bSees || S.State == 4)
	{
		PersonLooks(Doctor, Anim, Eyes, 0.8f);
	}
	else if (Anim)
	{
		// Otherwise the head goes slowly from side to side, looking.
		Anim->LookTowards(55.f * FMath::Sin(GetWorld()->GetTimeSeconds() * 0.7f + S.Doctor * 2.f), -6.f);
	}

	// Their shoes say where they are, which is most of what he has to go on.
	if (!bArrived)
	{
		S.StepWait -= Dt;
		if (S.StepWait <= 0.f)
		{
			S.StepWait = FMath::GetMappedRangeValueClamped(FVector2D(78.f, 150.f), FVector2D(0.66f, 0.42f), Speed);
			if (!HG::Extra(this, (S.Heading + FMath::RandHelper(2)) % 2 ? TEXT("step_hard_2") : TEXT("step_hard_1"), At, 0.95f, FMath::FRandRange(0.88f, 0.96f)))
			{
				HG::PlayAt(this, TEXT("step_1"), At, 0.6f, 0.8f);
			}
		}
	}
	// And now and then they call for him, gently.
	if (S.State <= 1 && !bBusy)
	{
		S.CallWait -= Dt;
		if (S.CallWait <= 0.f)
		{
			S.CallWait = FMath::FRandRange(14.f, 21.f);
			const bool bFirst = FMath::RandBool();
			const TCHAR* Line = bSecond ? (bFirst ? TEXT("esc_other_where") : TEXT("esc_other_come")) : TEXT("esc_woman_time");
			HG::Voice(this, Line, Head);
			if (Apart < 1000.f)
			{
				Say(bSecond ? (bFirst ? TEXT("\"Where are you, little one?\"") : TEXT("\"Come out now. We're not finished.\"")) : TEXT("\"Sweetheart? It's time to come back.\""), 4.f);
			}
		}
	}

	// Caught: close enough to be picked up, or seen going under something and reached there.
	const bool bMayCatch = !bBusy && !bShots && !(bBot && EscBot >= 5);
	if (S.State == 4 && bMayCatch && ((bSees && Apart < 88.f) || (bHidden && S.bSawHide && Apart < 165.f)))
	{
		CaughtTimer = 0.f;
		bCaughtMoved = false;
		bCaughtFreed = false;
		++Catches;
		Player->SetCarried(true);
		Say(TEXT("\"Got you. Back we go.\""), 3.5f);
		HG::VoiceBed(this, TEXT("esc_caught"), 1.f);
		HG::ExtraBed(this, TEXT("caught"), 0.8f);
		UE_LOG(LogHG, Log, TEXT("HGTEST CAUGHT by=%s catches=%d checkpoint=%d hidden=%d"), bSecond ? TEXT("second") : TEXT("first"), Catches, Checkpoint, bHidden ? 1 : 0);
	}
}

void AHGGameMode::TickEscape(float Dt, AHGPlayer* Player)
{
	if (!bEscapeBegun)
	{
		BeginEscape(Player);
	}
	if (bEnding)
	{
		TickEnding(Dt, Player);
		return;
	}
	const float Before = EscapeTimer;
	EscapeTimer += Dt;
	const float T = EscapeTimer;
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Corner = Level->EscapeOrigin();
	const FVector At = Player->GetActorLocation();
	const FVector Local = At - Corner;
	static const FVector Spots[] = { FVector(250, 190, 60), FVector(415, 470, 60), FVector(605, 1995, 60) };

	// He comes round in the dark.
	if (CaughtTimer < 0.f)
	{
		BlackOut = 1.f - FMath::SmoothStep(1.f, 4.2f, T);
	}
	if (Before <= 4.5f && T > 4.5f)
	{
		Hint = TEXT("Ctrl  crawl      Shift  run, and they hear it      Click  pick up      Right click  throw");
		HintUntil = Now + 16.f;
	}

	// Two of the lamps are on their way out.
	FlickWait -= Dt;
	if (FlickWait <= 0.f)
	{
		FlickWait = 0.09f;
		Level->SetEscapeLamp(1, FMath::FRand() < 0.12f ? 0.15f : 1.f);
		Level->SetEscapeLamp(4, FMath::FRand() < 0.2f ? 0.1f : 1.f);
	}

	// How far he has got: caught, he is carried back only as far as the last of these.
	if (Local.Y > 450.f && Checkpoint < 1) { Checkpoint = 1; }
	if (Local.Y > 1960.f && Checkpoint < 2) { Checkpoint = 2; }

	// Hidden means on his hands and knees under a bed or the counter.
	const bool bWasHidden = bHidden;
	bHidden = false;
	if (Player->IsCrawling())
	{
		for (const FBox& Box : HideBoxes)
		{
			bHidden = bHidden || Box.IsInsideXY(At);
		}
	}
	for (FHGSearcher& S : Searchers)
	{
		if (bHidden && !bWasHidden)
		{
			S.bSawHide = S.State == 4 && S.SinceSeen < 1.f;
		}
		else if (!bHidden)
		{
			S.bSawHide = false;
		}
	}
	if (!bToyHint && EscapeToy && Player->HeldToy() == EscapeToy)
	{
		bToyHint = true;
		Hint = TEXT("Right click  throw it. They go and look where it lands.");
		HintUntil = Now + 9.f;
	}

	float Worst = 0.f;
	for (FHGSearcher& S : Searchers)
	{
		TickSearcher(S, Dt, Player);
		Worst = FMath::Max(Worst, S.State == 4 ? 1.f : FMath::Min(S.Suspicion, 1.f));
	}
	if (HeartBed)
	{
		// His heart is the only meter: it climbs as one of them grows sure.
		HeartBed->SetVolumeMultiplier(0.08f + 0.85f * Worst);
		HeartBed->SetPitchMultiplier(1.f + 0.3f * Worst);
	}

	// Caught: the dark, and he is back where he had got to, with them gone back to their rounds.
	if (CaughtTimer >= 0.f)
	{
		CaughtTimer += Dt;
		BlackOut = CaughtTimer < 1.4f ? FMath::SmoothStep(0.f, 0.9f, CaughtTimer) : 1.f - FMath::SmoothStep(2.8f, 4.f, CaughtTimer);
		if (CaughtTimer > 1.4f && !bCaughtMoved)
		{
			bCaughtMoved = true;
			Player->SetActorLocation(Corner + Spots[Checkpoint]);
			Player->LookAt(Corner + Spots[Checkpoint] + FVector(0, 300, -20));
			for (FHGSearcher& S : Searchers)
			{
				S.State = 0;
				S.Heading = 0;
				S.Suspicion = 0.f;
				S.SinceSeen = 100.f;
				S.bSawHide = false;
				WardDoctors[S.Doctor]->SetActorLocation(FVector(S.Ends[1].X, S.Ends[1].Y, 0.f));
			}
			Hint = TEXT("They carried him back.");
			HintUntil = Now + 5.f;
		}
		if (CaughtTimer > 2.8f && !bCaughtFreed)
		{
			bCaughtFreed = true;
			Player->SetCarried(false);
		}
		if (CaughtTimer > 4.f)
		{
			CaughtTimer = -1.f;
		}
	}

	// The last door. She calls from behind it; it opens when he reaches it.
	const FVector DoorAt = Corner + FVector(1700, 2086, 100);
	const float ToDoor = FVector::Dist2D(At, DoorAt);
	if (Checkpoint >= 2)
	{
		MomCallWait -= Dt;
		if (MomCallWait <= 0.f)
		{
			MomCallWait = FMath::FRandRange(11.f, 15.f);
			HG::Voice(this, TEXT("mom_where"), DoorAt + FVector(60, 0, 40));
			if (ToDoor < 950.f)
			{
				Say(TEXT("\"Sweetheart? Where are you?\""), 3.5f);
			}
		}
	}
	if (ToDoor < 170.f)
	{
		if (!bMomDoorSounded)
		{
			bMomDoorSounded = true;
			HG::Extra(this, TEXT("door_handle"), DoorAt, 1.f);
			HG::Extra(this, TEXT("op_door"), DoorAt, 0.9f);
		}
		MomDoorOpen = FMath::FInterpConstantTo(MomDoorOpen, 1.f, Dt, 0.7f);
	}
	Level->SetMomDoorOpen(FMath::SmoothStep(0.f, 1.f, MomDoorOpen));
	if (Local.X > 1750.f && Local.Y > 1840.f && Local.Y < 2340.f && CaughtTimer < 0.f)
	{
		bEnding = true;
		EndingTimer = 0.f;
		UE_LOG(LogHG, Log, TEXT("HGTEST ESCAPE done seconds=%d catches=%d"), FMath::RoundToInt32(RoundSeconds), Catches);
	}

	if (bBot)
	{
		// The self-play check of scene five: each thing he can do, and each thing they do, once.
		EscBotTimer += Dt;
		AActor* Second = WardDoctors[1];
		switch (EscBot)
		{
		case 0:
			if (T > 4.5f) { Player->ForceCrawl(true); EscBot = 1; EscBotTimer = 0.f; }
			break;
		case 1:
			if (EscBotTimer > 1.f)
			{
				EscBotPassed |= Player->IsCrawling() ? 1 : 0;
				Player->SetActorLocation(Corner + FVector(592, 560, 26));
				EscBot = 2;
				EscBotTimer = 0.f;
			}
			break;
		case 2:
			if (EscBotTimer > 1.f)
			{
				EscBotPassed |= bHidden ? 2 : 0;
				Player->SetActorLocation(Corner + FVector(250, 480, 30));
				Player->ForceCrawl(false);
				ToyLanded(Corner + FVector(250, 1000, 0));
				EscBot = 3;
				EscBotTimer = 0.f;
			}
			break;
		case 3:
			if (EscBotTimer > 0.5f)
			{
				EscBotPassed |= Searchers[0].State == 2 ? 4 : 0;
				EscBot = 4;
				EscBotTimer = 0.f;
			}
			break;
		case 4:
			if (Catches > 0)
			{
				EscBotPassed |= 8;
				EscBot = 5;
				EscBotTimer = 0.f;
			}
			else if (CaughtTimer < 0.f && EscBotTimer < 25.f)
			{
				// Stand in the aisle right in front of her.
				const FVector Ahead = FRotator(0.f, Second->GetActorRotation().Yaw - MomYawOffset, 0.f).Vector() * 70.f;
				Player->SetActorLocation(FVector(Second->GetActorLocation().X + Ahead.X, Second->GetActorLocation().Y + Ahead.Y, 60.f));
			}
			else if (EscBotTimer >= 25.f)
			{
				EscBot = 5;
				EscBotTimer = 0.f;
			}
			break;
		case 5:
			if (CaughtTimer < 0.f && EscBotTimer > 1.f)
			{
				EscBotPassed |= FVector::Dist2D(Player->GetActorLocation(), Corner + Spots[Checkpoint]) < 160.f ? 16 : 0;
				Player->SetActorLocation(Corner + FVector(1600, 2086, 60));
				EscBot = 6;
				EscBotTimer = 0.f;
			}
			break;
		case 6:
			if (MomDoorOpen > 0.9f || EscBotTimer > 10.f)
			{
				EscBotPassed |= MomDoorOpen > 0.9f ? 32 : 0;
				Player->SetActorLocation(Corner + FVector(1800, 2086, 60));
				EscBot = 7;
			}
			break;
		default:
			break;
		}
	}

	if (bShots && CaughtTimer < 0.f)
	{
		// Pictures of scene five, with him put where each one is taken from.
		EscShotTimer += Dt;
		auto Place = [&](const FVector& Where, const FVector& Look, bool bDown)
		{
			Player->ForceCrawl(bDown);
			Player->SetActorLocation(Corner + Where);
			Player->LookAt(Corner + Look);
			++EscShot;
			EscShotTimer = 0.f;
		};
		auto Snap = [](const TCHAR* Name)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / Name, true, false);
			UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), Name);
		};
		// A picture is taken a frame after it is asked for, so he is only moved on a moment after each one.
		struct FView { const TCHAR* Name; FVector Where; FVector Look; bool bDown; };
		static const FView Views[] = {
			{ TEXT("ward-29-wakes.png"), FVector(300, 150, 60), FVector(110, 300, 50), false },
			{ TEXT("ward-30-gap.png"), FVector(415, 330, 26), FVector(415, 700, 30), true },
			{ TEXT("ward-31-under-bed.png"), FVector(592, 560, 26), FVector(250, 700, 100), true },
			{ TEXT("ward-32-ward.png"), FVector(250, 470, 60), FVector(250, 1500, 120), false },
			{ TEXT("ward-33-corridor.png"), FVector(640, 2090, 60), FVector(1500, 2070, 120), false },
			{ TEXT("ward-34-under-counter.png"), FVector(960, 2200, 26), FVector(1290, 2200, 24), true },
			{ TEXT("ward-35-door.png"), FVector(1380, 2086, 60), FVector(1700, 2086, 90), false },
			{ TEXT("ward-36-door-open.png"), FVector(1580, 2086, 60), FVector(1900, 2086, 110), false },
		};
		const int32 View = EscShot / 2;
		if (T > 5.f && View < UE_ARRAY_COUNT(Views))
		{
			if (EscShot % 2 == 0 && EscShotTimer > 0.5f)
			{
				if (View == 2)
				{
					// For the picture from under the bed, the doctor is put in the aisle beside it.
					WardDoctors[1]->SetActorLocation(Corner + FVector(250, 700, 0));
				}
				Place(Views[View].Where, Views[View].Look, Views[View].bDown);
			}
			else if (EscShot % 2 == 1 && EscShotTimer > 2.6f)
			{
				Snap(Views[View].Name);
				++EscShot;
				EscShotTimer = 0.f;
			}
		}
		else if (View >= UE_ARRAY_COUNT(Views) && EscShotTimer > 2.6f && !bEnding)
		{
			// And in through the door.
			Player->ForceCrawl(false);
			Player->SetActorLocation(Corner + FVector(1800, 2086, 60));
			Player->LookAt(Corner + FVector(2110, 2090, 150));
		}
	}
}

void AHGGameMode::TickEnding(float Dt, AHGPlayer* Player)
{
	const bool bFirst = EndingTimer <= 0.f;
	EndingTimer += Dt;
	const float T = EndingTimer;
	const FVector Eyes = Player->EyeLocation();
	const FVector MomHold = PersonPoint(WardMom, 50.f, 8.f, 122.f);
	const FVector MomFace = PersonPoint(WardMom, 9.f, 0.f, 153.f);
	auto Snap = [this](int32 Bit, const TCHAR* Name)
	{
		if (bShots && !(HandShots & (1 << Bit)))
		{
			HandShots |= 1 << Bit;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots") / Name, true, false);
			UE_LOG(LogHG, Log, TEXT("HGTEST SHOT %s"), Name);
		}
	};

	if (bFirst)
	{
		// He has found her. Nothing else in the building matters any more, or is there.
		Player->ForceCrawl(false);
		Player->SetCarried(true);
		Player->SetCrawlAllowed(false);
		for (AActor* Doctor : WardDoctors)
		{
			Doctor->SetActorHiddenInGame(true);
		}
		BlackOut = 0.f;
		HintUntil = 0.f;
		const FVector MomAt = WardMom->GetActorLocation();
		const FVector ChildAt(Player->GetActorLocation().X, Player->GetActorLocation().Y, 0.f);
		FVector Gap = MomAt - ChildAt;
		Gap.Z = 0.f;
		const float Apart = Gap.Size();
		HandMomStop = Apart > 60.f ? ChildAt + Gap / Apart * 52.f : MomAt;
		if (MomFaceAnim)
		{
			MomFaceAnim->SetWarm(1.f);
			MomFaceAnim->SetSad(0.45f);
		}
		Say(TEXT("\"There you are. Oh, there you are. I've got you. I've got you.\""), 8.f);
		HG::Voice(this, TEXT("mom_found"), MomFace);
	}

	// The dark lets go of him: the drone and his heart die away, the colour and the birds come back.
	const float Calm = FMath::SmoothStep(0.f, 4.f, T);
	if (HeartBed) { HeartBed->SetVolumeMultiplier(0.08f * (1.f - Calm)); }
	if (DroneBed) { DroneBed->SetVolumeMultiplier(0.4f * (1.f - Calm)); }
	SetWardSound(FMath::SmoothStep(2.f, 8.f, T));
	Player->SetSaturation(FMath::Lerp(0.18f, 1.3f, FMath::SmoothStep(0.5f, 7.f, T)));

	if (!bEndLifted)
	{
		// She comes to him as fast as she can walk.
		const bool bThere = WalkPerson(WardMom, MomAnim, HandMomStop, 150.f, 1.2f, Dt);
		PersonLooks(WardMom, MomAnim, Eyes, 0.5f);
		Player->TurnTowards(MomFace, Dt, 2.5f);
		EndingFrom = Eyes;
		if (T > 0.9f) { Snap(22, TEXT("ward-37-mom.png")); }
		if (bThere)
		{
			bEndLifted = true;
			EndLift = 0.f;
			if (MomAnim)
			{
				MomAnim->SetHold(1.f);
			}
		}
	}
	else
	{
		// And picks him up, as she did in the ward.
		EndLift += Dt;
		FacePerson(WardMom, EndingFrom, Dt, 3.f);
		const float Lift = FMath::SmoothStep(0.f, 1.f, (EndLift - 0.4f) / 2.1f);
		Player->CarryEyesTo(FMath::Lerp(EndingFrom, MomHold, Lift));
		Player->SetLens(FMath::Lerp(92.f, 72.f, Lift));
		if (EndLift < 4.f || bShots)
		{
			Player->TurnTowards(MomFace, Dt, 2.2f);
		}
		PersonLooks(WardMom, MomAnim, Eyes, 0.35f);
		if (EndLift > 6.f) { Snap(23, TEXT("ward-38-held.png")); }
		WhiteOut = FMath::SmoothStep(9.f, 12.5f, EndLift);
		if (EndLift > 13.5f && ClosingPage < 0)
		{
			ClosingPage = 0;
			ClosingTimer = 0.f;
			bEndingDone = true;
		}
	}

	if (ClosingPage >= 0 && !bOver)
	{
		// What was true, a page at a time. Each stays until it is read or a key sends it on.
		ClosingTimer += Dt;
		const float Stay = (bBot || bShots) ? 5.f : 16.f;
		const float In = ClosingPage == 0 ? 1.4f : 0.2f;
		ClosingCover = ClosingPage == 0 ? FMath::SmoothStep(0.f, 1.4f, ClosingTimer) : 1.f;
		ClosingInk = FMath::SmoothStep(In, In + 1.f, ClosingTimer) * (1.f - FMath::SmoothStep(Stay - 0.8f, Stay, ClosingTimer));
		if (ClosingPage == 0 && ClosingInk > 0.95f) { Snap(24, TEXT("ward-39-closing.png")); }
		const APlayerController* PC = Cast<APlayerController>(Player->GetController());
		if (PC && ClosingTimer > In + 1.2f && ClosingTimer < Stay - 0.8f
			&& (PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)))
		{
			ClosingTimer = Stay - 0.8f;
		}
		if (ClosingTimer > Stay)
		{
			if (ClosingPage < 3)
			{
				++ClosingPage;
				ClosingTimer = 0.f;
			}
			else
			{
				bOver = true;
				bWon = true;
				EndTitle = TEXT("THE END");
				EndLines.Reset();
				EndLines.Add(TEXT("Inspired in part by a true story."));
				EndLines.Add(TEXT("Thank you for playing."));
				UE_LOG(LogHG, Log, TEXT("HGTEST ENDING done seconds=%d catches=%d"), FMath::RoundToInt32(RoundSeconds), Catches);
			}
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
	if (StartScene >= 5 && !bEscape && WardStep < 10 && RoundSeconds > 1.f && WardMom && WardDoctors.Num() == 2)
	{
		// Straight to the escape: as if the chair had just gone to black.
		Player->SetCarried(true);
		WardStep = 10;
		HandStep = 6;
		bChair = true;
		bEscape = true;
		BlackOut = 1.f;
	}
	if (StartScene == 4 && !bChair && WardStep < 10 && RoundSeconds > 1.f && WardMom && WardDoctors.Num() == 2)
	{
		// Straight to the chair: behind the white, as if he had just been carried in.
		Player->SetCarried(true);
		WardStep = 10;
		HandStep = 6;
		bChair = true;
		WhiteOut = 1.f;
	}
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
		if (MomFaceAnim)
		{
			// She knew this was coming. The smile mostly goes.
			MomFaceAnim->SetWarm(0.3f);
			MomFaceAnim->SetSad(0.55f);
		}
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
	else if (After > 3 && (bOver || After > 700))
	{
		// With the people in the game the scene has to run on through the handover.
		const bool bStaged = WardMom && WardDoctors.Num() == 2;
		UE_LOG(LogHG, Log, TEXT("HGTEST HANDOVER %s staged=%d step=%d carried=%d"), (!bStaged || bHandedOver) ? TEXT("PASS") : TEXT("FAIL"), bStaged ? 1 : 0, HandStep, Player->IsCarried() ? 1 : 0);
		// The corridor passes when he reached the white room, every lamp behind him is out and both people fell in.
		const bool bCorridorGood = bCorridorDone && LampsOut >= Level->HallLampAlong().Num() - 2 && StaffWalking == 3;
		UE_LOG(LogHG, Log, TEXT("HGTEST CORRIDOR %s staged=%d lamps=%d staff=%d door=%.2f"), (!bStaged || bCorridorGood) ? TEXT("PASS") : TEXT("FAIL"), bStaged ? 1 : 0, LampsOut, StaffWalking, OpDoorOpen);
		// The chair passes when the cloth came down, the red reached all of it, and every cue in the scene went off.
		const bool bChairGood = bChairDone && ChairCues == 2047;
		UE_LOG(LogHG, Log, TEXT("HGTEST CHAIR %s staged=%d cloth=%.2f red=%.2f cues=%d"), (!bStaged || bChairGood) ? TEXT("PASS") : TEXT("FAIL"), bStaged ? 1 : 0, ClothDown, ClothRed, ChairCues);
		// The escape passes when crawling, hiding, the lure, being caught, being carried back and the door all worked, and he reached her.
		const bool bEscapeGood = EscBotPassed == 63 && bEndingDone && ClosingPage >= 3;
		UE_LOG(LogHG, Log, TEXT("HGTEST ESCAPE %s staged=%d worked=%d catches=%d ending=%d page=%d"), (!bStaged || bEscapeGood) ? TEXT("PASS") : TEXT("FAIL"), bStaged ? 1 : 0, EscBotPassed, Catches, bEndingDone ? 1 : 0, ClosingPage);
		const bool bGood = bOver && bWon && WardSorter->Filled == 3 && bAssetsComplete && (!bStaged || (bHandedOver && bCorridorGood && bChairGood && bEscapeGood && Player->IsCarried() && VoicesFound == 26 && CorridorSoundsFound == 16));
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
		if (bShots && bEscape)
		{
			// Scene five on takes its own pictures; this only waits for the end.
			ShotTimer = bOver ? ShotTimer + DeltaSeconds : 0.f;
			if (ShotTimer > 1.5f)
			{
				Finish(0);
			}
		}
		else if (bShots)
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

namespace
{
	/** The closing text, a page at a time. The first line of a page is its heading and may be empty. */
	const TArray<FString>& ClosingLines(int32 Page)
	{
		static const TArray<FString> Pages[] = {
			{
				TEXT("IT WAS NOT UNUSUAL"),
				TEXT("What he remembers matches how adenoids, and often tonsils, were taken out of children"),
				TEXT("in Soviet and 1990s Russian clinics: awake, held on an adult's lap or tied to the chair."),
				TEXT(""),
				TEXT("A surgeon writes that it used to be done \"without anaesthesia, practically alive\"."),
				TEXT("A Moscow clinic says of the old way: \"A lot of blood, a lot of stress, little effect.\""),
			},
			{
				TEXT(""),
				TEXT("People who went through it remember the same things:"),
				TEXT("a chair, a sheet or a rubber apron, the taste of metal, and a great deal of blood."),
				TEXT(""),
				TEXT("The only reason we found given, and that second-hand: a couple of minutes of pain"),
				TEXT("was held to be safer for a child than the general anaesthesia of that time."),
				TEXT("Today it is done under general anaesthesia."),
			},
			{
				TEXT(""),
				TEXT("This is not told to blame anyone."),
				TEXT("The people in that room were doing what they had been taught was normal."),
				TEXT(""),
				TEXT("It is told because a great many children went through it,"),
				TEXT("and almost nobody talks about it."),
			},
			{
				TEXT("SOURCES"),
				TEXT("SM-Clinic, on Habr:  habr.com/ru/companies/smclinic/articles/990100"),
				TEXT("medicina.ru:  /press-tsentr/statyi/operatsiya-po-udaleniyu-adenoidov-eto-ne-tragediya"),
				TEXT("LiveJournal:  masterok.livejournal.com/6814032.html"),
				TEXT(""),
				TEXT("Inspired in part by a true story. The opening, up to the operation, is the author's own memory."),
				TEXT("Everything after it, the escape through the hospital, is invented."),
			},
		};
		return Pages[FMath::Clamp(Page, 0, 3)];
	}

	/** A fixed random number from 0 to 1 for a point on a grid. */
	float ClothHash(int32 X, int32 Y)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return static_cast<float>((H ^ (H >> 16)) & 0xFFFF) / 65535.f;
	}

	/** Smooth blotchy noise from 0 to 1. */
	float ClothNoise(float X, float Y)
	{
		const int32 IX = FMath::FloorToInt32(X), IY = FMath::FloorToInt32(Y);
		const float FX = FMath::SmoothStep(0.f, 1.f, X - IX), FY = FMath::SmoothStep(0.f, 1.f, Y - IY);
		return FMath::Lerp(FMath::Lerp(ClothHash(IX, IY), ClothHash(IX + 1, IY), FX), FMath::Lerp(ClothHash(IX, IY + 1), ClothHash(IX + 1, IY + 1), FX), FY);
	}

	void UploadPixels(UTexture2D* Texture, const TArray<FColor>& Pixels)
	{
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
	}
}

void AHGHUD::DrawCloth(const AHGGameMode* Game, float W, float H, float Now)
{
	static const int32 CW = 192, CH = 108;
	if (!FApp::CanEverRender())
	{
		return;
	}
	if (!Cloth)
	{
		Cloth = UTexture2D::CreateTransient(CW, CH, PF_B8G8R8A8);
		Cloth->SRGB = true;
		Cloth->Filter = TF_Bilinear;
		Cloth->AddressX = TA_Clamp;
		Cloth->AddressY = TA_Clamp;
		ClothPixels.SetNumUninitialized(CW * CH);
		// Where the red starts and how late it gets everywhere else. It comes up from under the lower
		// edge, in the middle, where the cloth lies against his face, and it does not spread evenly.
		ClothWet.SetNumUninitialized(CW * CH);
		for (int32 Y = 0; Y < CH; ++Y)
		{
			for (int32 X = 0; X < CW; ++X)
			{
				const float U = (X + 0.5f) / CW, V = (Y + 0.5f) / CH;
				const float A = FVector2D((U - 0.46f) * 1.05f, (V - 0.98f) * 0.82f).Size();
				const float B = FVector2D((U - 0.57f) * 1.15f, (V - 0.9f) * 0.9f).Size() + 0.04f;
				// Big uneven patches, smaller ones on those, and threads that draw it upwards faster than their neighbours.
				const float Blotch = 0.26f * (ClothNoise(U * 5.f, V * 5.f * 0.5625f) - 0.5f) + 0.1f * (ClothNoise(U * 15.f + 7.f, V * 15.f * 0.5625f) - 0.5f)
					+ 0.13f * (ClothNoise(U * 34.f + 3.f, V * 5.f) - 0.5f) + 0.05f * (ClothNoise(U * 70.f, V * 40.f) - 0.5f);
				ClothWet[Y * CW + X] = FMath::Max(0.f, FMath::Min(A, B) / 0.82f + Blotch);
			}
		}
		// The weave: darker lines between the threads, a little light on the top of each.
		Weave = UTexture2D::CreateTransient(8, 8, PF_B8G8R8A8);
		Weave->SRGB = true;
		Weave->Filter = TF_Bilinear;
		TArray<FColor> Threads;
		Threads.SetNumUninitialized(64);
		for (int32 Y = 0; Y < 8; ++Y)
		{
			for (int32 X = 0; X < 8; ++X)
			{
				const bool bGap = (X % 4) == 0 || (Y % 4) == 0;
				const bool bOver = ((X / 4) + (Y / 4)) % 2 == 0;
				Threads[Y * 8 + X] = bGap ? FColor(0, 0, 0, 20) : (bOver ? FColor(255, 255, 255, 9) : FColor(0, 0, 0, 5));
			}
		}
		UploadPixels(Weave, Threads);
		ClothNext = 0.f;
	}

	if (Now >= ClothNext)
	{
		ClothNext = Now + 0.05f;
		const float Aspect = 9.f / 16.f;
		const FVector2D Shift = Game->ClothShift;
		// The lamp, high and a little to one side, and the shapes between it and him.
		const FVector2D Lamp(0.52f - 0.12f * Shift.X, 0.26f + 0.12f * Shift.Y);
		const float Loom = Game->ClothLoom, Busy = Game->ClothBusy;
		const FVector2D Bent(0.5f + 0.05f * FMath::Sin(Now * 0.5f) - 0.16f * Shift.X, 0.62f - 0.3f * Loom + 0.14f * Shift.Y);
		const float BentSize = 0.13f + 0.2f * Loom;
		const FVector2D Hand(0.36f + 0.12f * FMath::Sin(Now * 1.7f) - 0.16f * Shift.X, 0.74f + 0.06f * FMath::Cos(Now * 1.3f) + 0.14f * Shift.Y);
		const FVector2D Other(0.7f + 0.07f * FMath::Sin(Now * 0.9f + 2.f) - 0.16f * Shift.X, 0.68f + 0.05f * FMath::Sin(Now * 1.1f) + 0.14f * Shift.Y);
		const float Soak = Game->ClothRed * 1.08f;
		// It comes down from the top with a soft lower edge.
		const float Edge = Game->ClothDown * 1.3f - 0.15f;
		const FLinearColor Fresh = Game->bLessBlood ? FLinearColor(0.3f, 0.3f, 0.32f) : FLinearColor(0.66f, 0.035f, 0.03f);
		const FLinearColor Deep = Game->bLessBlood ? FLinearColor(0.16f, 0.16f, 0.18f) : FLinearColor(0.3f, 0.f, 0.01f);
		for (int32 Y = 0; Y < CH; ++Y)
		{
			for (int32 X = 0; X < CW; ++X)
			{
				const float U = (X + 0.5f) / CW, V = (Y + 0.5f) / CH;
				auto Near = [U, V, Aspect](const FVector2D& Centre, float Size)
				{
					const float DX = U - Centre.X, DY = (V - Centre.Y) * Aspect;
					return FMath::Exp(-(DX * DX + DY * DY) / (Size * Size));
				};
				float Light = 0.74f + 0.25f * Near(Lamp, 0.34f);
				Light *= 1.f - (0.14f + 0.36f * Loom) * Near(Bent, BentSize);
				Light *= 1.f - 0.2f * Busy * Near(Hand, 0.085f);
				Light *= 1.f - 0.15f * Busy * Near(Other, 0.11f);
				FLinearColor Colour(Light, Light * 0.985f, Light * 0.945f);

				const float Wet = ClothWet[Y * CW + X];
				// Nothing at all until it starts.
				const float Stained = (1.f - FMath::SmoothStep(Soak - 0.035f, Soak + 0.01f, Wet)) * FMath::SmoothStep(0.f, 0.04f, Soak);
				if (Stained > 0.f)
				{
					// Fresh at the spreading edge, darker where it has been longest, and the light still shows through it.
					const float Age = FMath::Clamp((Soak - Wet) / 0.55f, 0.f, 1.f);
					const FLinearColor Blood = FMath::Lerp(Fresh, Deep, Age) * (0.5f + 0.6f * Light);
					Colour = FMath::Lerp(Colour, Blood, Stained * 0.95f);
					const float Rim = FMath::Exp(-FMath::Square((Wet - Soak + 0.03f) / 0.025f));
					Colour *= 1.f - 0.3f * Rim;
				}
				const float Alpha = 1.f - FMath::SmoothStep(Edge - 0.15f, Edge, V);
				ClothPixels[Y * CW + X] = FColor(
					static_cast<uint8>(FMath::Clamp(Colour.R, 0.f, 1.f) * 255.f), static_cast<uint8>(FMath::Clamp(Colour.G, 0.f, 1.f) * 255.f),
					static_cast<uint8>(FMath::Clamp(Colour.B, 0.f, 1.f) * 255.f), static_cast<uint8>(Alpha * 255.f));
			}
		}
		UploadPixels(Cloth, ClothPixels);
	}

	FCanvasTileItem Sheet(FVector2D(0.f, 0.f), Cloth->GetResource(), FVector2D(W, H), FLinearColor::White);
	Sheet.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Sheet);
	// Each thread is about three pixels wide, whatever the screen.
	const float Covered = FMath::Clamp(Game->ClothDown * 1.3f - 0.3f, 0.f, 1.f);
	if (Covered > 0.f)
	{
		FCanvasTileItem Threads(FVector2D(0.f, 0.f), Weave->GetResource(), FVector2D(W, H * Covered), FVector2D(0.f, 0.f), FVector2D(W / 6.f, H * Covered / 6.f), FLinearColor::White);
		Threads.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Threads);
	}
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

	if (Game->WhiteOut > 0.f)
	{
		// The operation room's light, taking the whole picture.
		DrawRect(FLinearColor(1.f, 1.f, 1.f, Game->WhiteOut), 0.f, 0.f, W, H);
	}

	if (Game->ClothDown > 0.f)
	{
		DrawCloth(Game, W, H, Now);
	}
	if (Game->BlackOut > 0.f)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, Game->BlackOut), 0.f, 0.f, W, H);
	}

	if (Game->ClosingPage >= 0 && !Game->IsOver())
	{
		// The closing text: white words on black, a page at a time.
		DrawRect(FLinearColor(0.f, 0.f, 0.f, Game->ClosingCover), 0.f, 0.f, W, H);
		const TArray<FString>& Lines = ClosingLines(Game->ClosingPage);
		float Y = H * 0.5f - (Lines.Num() * 38.f * S) * 0.5f;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			const bool bHeading = i == 0;
			if (!Lines[i].IsEmpty())
			{
				Centered(Lines[i], Y, (bHeading ? 2.6f : 1.35f) * S, FLinearColor(1.f, 1.f, 1.f, Game->ClosingInk * (bHeading ? 1.f : 0.92f)), bHeading ? FaceLight : NAME_None);
			}
			Y += (bHeading ? 64.f : 38.f) * S;
		}
		if (Game->ClosingInk > 0.9f)
		{
			Centered(TEXT("Space  next"), H * 0.86f, 1.f * S, FLinearColor(Dim.R, Dim.G, Dim.B, 0.6f));
		}
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
		const float CardAge = Now - Game->CardStart;
		if (CardAge < 5.2f)
		{
			// A chapter card over the picture: it comes up and goes again without stopping anything.
			const float Alpha = FMath::Clamp(FMath::Min(CardAge - 0.5f, 4.6f - CardAge), 0.f, 1.f);
			// White letters, or dark ones while the picture itself is white.
			const float Ink = 1.f - 0.9f * Game->WhiteOut;
			Centered(Game->CardText, H * 0.40f, 3.4f * S, FLinearColor(Ink, Ink, Ink, Alpha), FaceLight);
		}
		if (Game->RoundSeconds < 12.f && Now >= Game->HintUntil)
		{
			const float Alpha = FMath::Clamp(12.f - Game->RoundSeconds, 0.f, 1.f);
			Centered(TEXT("WASD move      Mouse look      Click to pick up and put down"), H * 0.845f, 1.05f * S, FLinearColor(Dim.R, Dim.G, Dim.B, Alpha));
		}
		if (Now < Game->MessageUntil)
		{
			const float Alpha = FMath::Clamp((Game->MessageUntil - Now) * 2.f, 0.f, 1.f);
			Centered(Game->Message, H * 0.78f, 1.6f * S, FLinearColor(1.f, 1.f, 1.f, Alpha));
		}
		if (Now < Game->HintUntil)
		{
			const float Alpha = FMath::Clamp(Game->HintUntil - Now, 0.f, 1.f);
			Centered(Game->Hint, H * 0.845f, 1.05f * S, FLinearColor(Dim.R, Dim.G, Dim.B, Alpha));
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
