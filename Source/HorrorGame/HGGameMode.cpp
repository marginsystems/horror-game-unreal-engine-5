#include "HGGameMode.h"

#include "Components/AudioComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/ExponentialHeightFog.h"
#include "GameFramework/PlayerStart.h"
#include "HGActors.h"
#include "HGLevel.h"
#include "HGMonster.h"
#include "HGPlayer.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
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
	Say(FString::Printf(TEXT("Find %d fuses. Power the exit shutter. Leave."), FusesTotal), 7.f);

	// The room tone for this place, looping under everything else.
	const FString Ambience = FString::Printf(TEXT("amb_%s"), HG::VariantKey(Variant));
	if (USoundBase* Found = HG::Sound(*Ambience))
	{
		UGameplayStatics::SpawnSound2D(this, Found, 0.55f);
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
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, FString::Printf(TEXT("Variant=%s"), HG::VariantKey(To)));
}

void AHGGameMode::Finish(int32 ExitCode)
{
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

	if (!bOver)
	{
		RoundSeconds += DeltaSeconds;
		if (Level->ToTile(Player->GetActorLocation()) == Level->ExitTile)
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

void AHGHUD::Centered(const FString& Text, float Y, float Scale, const FLinearColor& Color)
{
	UFont* Font = GEngine->GetLargeFont();
	float TextW = 0.f;
	float TextH = 0.f;
	GetTextSize(Text, TextW, TextH, Font, Scale);
	DrawText(Text, FLinearColor(0, 0, 0, Color.A * 0.8f), (Canvas->ClipX - TextW) * 0.5f + 2.f, Y + 2.f, Font, Scale);
	DrawText(Text, Color, (Canvas->ClipX - TextW) * 0.5f, Y, Font, Scale);
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
	UFont* Font = GEngine->GetLargeFont();
	const FLinearColor Amber(1.f, 0.78f, 0.25f);
	const FLinearColor Dim(0.75f, 0.75f, 0.72f);

	if (Player && Player->IsBlinking())
	{
		DrawRect(FLinearColor::Black, 0.f, 0.f, W, H);
	}

	if (Game->IsOver())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f), 0.f, 0.f, W, H);
		Centered(Game->EndTitle, H * 0.22f, 4.2f * S, Game->HasWon() ? FLinearColor(0.5f, 1.f, 0.6f) : FLinearColor(0.95f, 0.1f, 0.08f));
		float Y = H * 0.42f;
		for (const FString& Line : Game->EndLines)
		{
			Centered(Line, Y, 1.5f * S, FLinearColor::White);
			Y += 40.f * S;
		}
		Centered(TEXT("R  try again      F1  clinic      F2  office      F3  mine      Cmd+Q  quit"), H * 0.8f, 1.2f * S, Dim);
		return;
	}

	// Fuses, top left.
	DrawText(FString::Printf(TEXT("FUSES  %d / %d"), Game->Fuses, Game->FusesTotal), Amber, 26.f * S, 20.f * S, Font, 1.6f * S);

	if (Player)
	{
		// Item slots, bottom left.
		float X = 26.f * S;
		const float Y = H - 52.f * S;
		if (Player->Slots.Num() == 0)
		{
			DrawText(TEXT("Empty hands"), Dim, X, Y, Font, 1.2f * S);
		}
		for (int32 i = 0; i < Player->Slots.Num(); ++i)
		{
			const FString Label = FString::Printf(TEXT("[%d] %s"), i + 1, HG::ItemName(Player->Slots[i]));
			float TextW = 0.f;
			float TextH = 0.f;
			GetTextSize(Label, TextW, TextH, Font, 1.2f * S);
			DrawText(Label, i == Player->Selected ? Amber : Dim, X, Y, Font, 1.2f * S);
			X += TextW + 26.f * S;
		}

		// Flashlight and nerves, bottom right.
		const FString Light = Player->IsFlashlightOn() ? TEXT("FLASHLIGHT  ok") : TEXT("FLASHLIGHT  dead   [F] smack it");
		float LightW = 0.f;
		float LightH = 0.f;
		GetTextSize(Light, LightW, LightH, Font, 1.2f * S);
		DrawText(Light, Player->IsFlashlightOn() ? Dim : FLinearColor(1.f, 0.3f, 0.2f), W - LightW - 26.f * S, H - 52.f * S, Font, 1.2f * S);

		const float BarW = 180.f * S;
		const float BarX = W - BarW - 26.f * S;
		const float BarY = H - 76.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), BarX, BarY, BarW, 8.f * S);
		DrawRect(FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.15f, 0.1f), FLinearColor(0.6f, 0.9f, 0.7f), Player->Composure), BarX, BarY, BarW * Player->Composure, 8.f * S);
		DrawText(TEXT("NERVES"), Dim, BarX - 74.f * S, BarY - 7.f * S, Font, 0.9f * S);
	}

	// Crosshair.
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.6f), W * 0.5f - 1.5f, H * 0.5f - 1.5f, 3.f, 3.f);

	// Opening card.
	if (Game->RoundSeconds < 7.f)
	{
		const float Alpha = FMath::Clamp(7.f - Game->RoundSeconds, 0.f, 1.f);
		Centered(HG::VariantTitle(Game->GetVariant()), H * 0.16f, 2.4f * S, FLinearColor(1.f, 1.f, 1.f, Alpha));
		Centered(TEXT("WASD move    Shift sprint (loud)    Ctrl tiptoe (silent)    Mouse use    1-4 choose    F smack flashlight"),
			H * 0.25f, 1.05f * S, FLinearColor(Dim.R, Dim.G, Dim.B, Alpha));
	}

	if (Now < Game->MessageUntil)
	{
		const float Alpha = FMath::Clamp((Game->MessageUntil - Now) * 2.f, 0.f, 1.f);
		Centered(Game->Message, H * 0.68f, 1.6f * S, FLinearColor(1.f, 1.f, 1.f, Alpha));
	}
}
