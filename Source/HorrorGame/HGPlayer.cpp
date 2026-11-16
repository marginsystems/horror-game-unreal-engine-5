#include "HGPlayer.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HGActors.h"
#include "HGGameMode.h"
#include "HGLevel.h"
#include "HGMonster.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	const float WalkSpeed = 330.f;
	const float SprintSpeed = 560.f;
	const float TiptoeSpeed = 160.f;
	const float LookDegreesPerCount = 0.15f;
	const float FlashlightIntensity = 16000.f;
	const FVector HandRest(46.f, 19.f, -19.f);

	const TCHAR* WarrantyLine(EHGItem Item)
	{
		switch (Item)
		{
		case EHGItem::Pistol: return TEXT("Warranty card: \"Not for use against the undead, the living, or at all.\"");
		case EHGItem::Shotgun: return TEXT("Warranty card: \"Contents may celebrate under pressure.\"");
		case EHGItem::Chicken: return TEXT("Warranty card: \"Squeaks. That is the feature.\"");
		case EHGItem::Grenade: return TEXT("Warranty card: \"Detonation time is an estimate.\"");
		default: return TEXT("");
		}
	}
}

AHGPlayer::AHGPlayer()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(32.f, 88.f);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(92.f);

	// A fixed exposure keeps the dark dark: the eye must not adapt until every corridor looks lit.
	FPostProcessSettings& PP = Camera->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = AEM_Manual;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = 11.6f;
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.55f;
	PP.bOverride_FilmGrainIntensity = true;
	PP.FilmGrainIntensity = 0.5f;
	Camera->PostProcessBlendWeight = 1.f;

	Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(Camera);
	Flashlight->SetRelativeLocation(FVector(12.f, 0.f, -6.f));
	Flashlight->SetIntensityUnits(ELightUnits::Unitless);
	Flashlight->SetIntensity(FlashlightIntensity);
	Flashlight->SetLightColor(FLinearColor(1.f, 0.95f, 0.82f));
	Flashlight->SetAttenuationRadius(2600.f);
	Flashlight->SetInnerConeAngle(15.f);
	Flashlight->SetOuterConeAngle(31.f);
	Flashlight->SetCastShadows(true);

	Hand = CreateDefaultSubobject<USceneComponent>(TEXT("Hand"));
	Hand->SetupAttachment(Camera);
	Hand->SetRelativeLocation(HandRest);

	// The held item sits on its own lighting channel with its own small lamp. Otherwise the
	// flashlight, a hand's width away, burns it out to white.
	HandLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HandLight"));
	HandLight->SetupAttachment(Camera);
	HandLight->SetRelativeLocation(FVector(18.f, -6.f, 8.f));
	HandLight->SetIntensityUnits(ELightUnits::Unitless);
	HandLight->SetIntensity(7.f);
	HandLight->SetLightColor(FLinearColor(1.f, 0.93f, 0.8f));
	HandLight->SetAttenuationRadius(160.f);
	HandLight->SetCastShadows(false);
	HandLight->SetLightingChannels(false, true, false);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = WalkSpeed;
	Move->BrakingDecelerationWalking = 1800.f;
	Move->MaxAcceleration = 2400.f;
	Move->AirControl = 0.2f;
}

void AHGPlayer::BeginPlay()
{
	Super::BeginPlay();
	LightDiesAt = GetWorld()->GetTimeSeconds() + FMath::FRandRange(22.f, 34.f);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

FVector AHGPlayer::EyeLocation() const
{
	return Camera->GetComponentLocation();
}

FVector AHGPlayer::EyeForward() const
{
	return Camera->GetForwardVector();
}

bool AHGPlayer::CanObserve(const FVector& Target) const
{
	if (IsBlinking() || bDead)
	{
		return false;
	}
	const AHGGameMode* Game = GetWorld()->GetAuthGameMode<AHGGameMode>();
	const AHGLevel* Level = Game ? Game->GetLevel() : nullptr;
	const FVector Eye = EyeLocation();
	const FVector To = Target - Eye;
	const float Distance = To.Size();
	if (Distance < 1.f)
	{
		return true;
	}
	if (FVector::DotProduct(EyeForward(), To / Distance) < 0.52f)
	{
		return false;
	}
	// You cannot stare down what you cannot see: it takes the flashlight, or being close.
	if (!bLightOn && Distance > 900.f)
	{
		return false;
	}
	return !Level || Level->HasLineOfSight(Eye, Target);
}

void AHGPlayer::LookAt(const FVector& Target)
{
	if (AController* C = GetController())
	{
		C->SetControlRotation((Target - EyeLocation()).Rotation());
	}
}

void AHGPlayer::FallOver()
{
	bDead = true;
	GetCharacterMovement()->DisableMovement();
}

bool AHGPlayer::GiveItem(EHGItem Item)
{
	if (Item == EHGItem::None || Item == EHGItem::Fuse || Slots.Num() >= 4 || Slots.Contains(Item))
	{
		return false;
	}
	Slots.Add(Item);
	Selected = Slots.Num() - 1;
	bPistolFlagOut = false;
	RebuildHeld();
	if (AHGGameMode* Game = GetWorld()->GetAuthGameMode<AHGGameMode>())
	{
		Game->Say(FString::Printf(TEXT("%s.  %s"), HG::ItemName(Item), WarrantyLine(Item)), 5.f);
	}
	return true;
}

void AHGPlayer::SelectSlot(int32 Index)
{
	if (Slots.IsValidIndex(Index) && Index != Selected)
	{
		Selected = Index;
		bPistolFlagOut = false;
		RebuildHeld();
	}
}

void AHGPlayer::RebuildHeld()
{
	for (USceneComponent* Part : HeldParts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	HeldParts.Reset();

	const EHGItem Item = HeldItem();
	TArray<USceneComponent*> Parts;
	AHGPickup::BuildItem(this, Hand, Item, Parts);

	if (Item == EHGItem::Pistol && bPistolFlagOut)
	{
		Parts.Add(HG::AddShape(this, Hand, TEXT("Cylinder"), FVector(19, 0, 17), FVector(0.012f, 0.012f, 0.24f), FLinearColor(0.7f, 0.7f, 0.7f)));
		Parts.Add(HG::AddShape(this, Hand, TEXT("Cube"), FVector(19, -10, 23), FVector(0.004f, 0.19f, 0.1f), FLinearColor(0.9f, 0.05f, 0.05f), 1.5f));
		UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this);
		Text->SetMobility(EComponentMobility::Movable);
		Text->SetupAttachment(Hand);
		Text->SetRelativeLocation(FVector(18.4f, -10.f, 23.f));
		Text->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetWorldSize(5.2f);
		Text->SetText(FText::FromString(TEXT("BANG!")));
		Text->SetTextRenderColor(FColor::White);
		Text->RegisterComponent();
		AddInstanceComponent(Text);
		Parts.Add(Text);
	}

	for (USceneComponent* Part : Parts)
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Part))
		{
			Prim->SetCastShadow(false);
			Prim->SetLightingChannels(false, true, false);
		}
		HeldParts.Add(Part);
	}
}

void AHGPlayer::UseItem()
{
	AHGGameMode* Game = GetWorld()->GetAuthGameMode<AHGGameMode>();
	if (!Game)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Eye = EyeLocation();
	const FVector Forward = EyeForward();
	const EHGItem Item = HeldItem();

	switch (Item)
	{
	case EHGItem::Pistol:
		bPistolFlagOut = !bPistolFlagOut;
		RebuildHeld();
		Game->ReportNoise(GetActorLocation(), 2400.f, 2.5f);
		Game->Say(bPistolFlagOut ? TEXT("BANG!   (it is a flag)") : TEXT("You wind the flag back in. Click."), 2.5f);
		LastCause = TEXT("trusted the pistol");
		break;

	case EHGItem::Shotgun:
	{
		if (Now < ShotgunReadyAt)
		{
			Game->Say(TEXT("Reloading party supplies..."), 1.2f);
			return;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(FName(TEXT("HGShotgun")), false, this);
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Forward * 1100.f, ECC_Visibility, Params);
		const FVector Landing = bHit ? Hit.ImpactPoint - Forward * 60.f : Eye + Forward * 800.f;
		if (AHGConfetti* Confetti = GetWorld()->SpawnActor<AHGConfetti>(Eye + Forward * 70.f - FVector(0, 0, 12.f), FRotator::ZeroRotator))
		{
			Confetti->Burst(Forward);
		}
		// The bang is heard where the confetti lands, which is where the monster goes to stare at it.
		Game->ReportNoise(Landing, 3000.f, 5.f);
		Game->Say(TEXT("POP!   Confetti. And a small paper horn noise."), 2.5f);
		ShotgunReadyAt = Now + 4.f;
		LastCause = TEXT("brought confetti to a monster fight");
		break;
	}

	case EHGItem::Chicken:
	case EHGItem::Grenade:
	{
		if (AHGThrown* Thrown = GetWorld()->SpawnActor<AHGThrown>(Eye + Forward * 75.f, FRotator::ZeroRotator))
		{
			const float Power = Item == EHGItem::Chicken ? 1250.f : 1000.f;
			Thrown->Launch(Item, Forward * Power + FVector(0, 0, 280.f) + GetVelocity());
		}
		Game->Say(Item == EHGItem::Chicken ? TEXT("You throw the chicken.") : TEXT("You pull the pin and throw.  ...  Nothing happens."), 3.f);
		LastCause = Item == EHGItem::Chicken ? TEXT("threw away their only chicken") : TEXT("was still waiting for the grenade");
		Slots.RemoveAt(Selected);
		Selected = Slots.Num() > 0 ? FMath::Min(Selected, Slots.Num() - 1) : INDEX_NONE;
		RebuildHeld();
		break;
	}

	default:
		Game->Say(TEXT("You have nothing. You make finger guns. It does not help."), 2.5f);
		Game->ReportNoise(GetActorLocation(), 500.f, 1.f);
		return;
	}

	++Discharges;
	UseKick = 1.f;
}

void AHGPlayer::SmackFlashlight()
{
	AHGGameMode* Game = GetWorld()->GetAuthGameMode<AHGGameMode>();
	if (!Game)
	{
		return;
	}
	Game->ReportNoise(GetActorLocation(), 1700.f, 1.5f);
	UseKick = 0.6f;
	if (bLightOn)
	{
		Game->Say(TEXT("You smack a working flashlight. It is unimpressed."), 2.f);
		return;
	}
	LastCause = TEXT("attempted percussive maintenance");
	if (FMath::FRand() < 0.6f)
	{
		bLightOn = true;
		LightDiesAt = GetWorld()->GetTimeSeconds() + FMath::FRandRange(18.f, 36.f);
		Game->Say(TEXT("It works again. For now."), 2.f);
	}
	else
	{
		Game->Say(TEXT("Nothing. Smack it again."), 1.5f);
	}
}

void AHGPlayer::HandleKeys(APlayerController* PC, AHGGameMode* Game)
{
	if (PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
		return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::F1)) { Game->Travel(EHGVariant::Clinic); return; }
	if (PC->WasInputKeyJustPressed(EKeys::F2)) { Game->Travel(EHGVariant::Office); return; }
	if (PC->WasInputKeyJustPressed(EKeys::F3)) { Game->Travel(EHGVariant::Mine); return; }
	if (PC->WasInputKeyJustPressed(EKeys::R) && Game->IsOver())
	{
		Game->Travel(Game->GetVariant());
		return;
	}
	if (Game->IsOver())
	{
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)) { UseItem(); }
	if (PC->WasInputKeyJustPressed(EKeys::F)) { SmackFlashlight(); }
	if (PC->WasInputKeyJustPressed(EKeys::One)) { SelectSlot(0); }
	if (PC->WasInputKeyJustPressed(EKeys::Two)) { SelectSlot(1); }
	if (PC->WasInputKeyJustPressed(EKeys::Three)) { SelectSlot(2); }
	if (PC->WasInputKeyJustPressed(EKeys::Four)) { SelectSlot(3); }
	if (Slots.Num() > 1)
	{
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) { SelectSlot((Selected + 1) % Slots.Num()); }
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) { SelectSlot((Selected + Slots.Num() - 1) % Slots.Num()); }
	}
}

void AHGPlayer::UpdateFlashlight(float Dt, AHGGameMode* Game)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (bLightOn && Now >= LightDiesAt)
	{
		bLightOn = false;
		Game->Say(TEXT("Your flashlight died.  Press F to smack it."), 4.f);
	}

	float Intensity = bLightOn ? FlashlightIntensity : 0.f;
	if (bLightOn && LightDiesAt - Now < 2.f)
	{
		// The last two seconds are a warning, of the kind nobody acts on.
		Intensity *= FMath::FRand() < 0.35f ? 0.08f : 0.8f;
	}
	Flashlight->SetIntensity(Intensity);
}

void AHGPlayer::UpdateComposure(float Dt, AHGGameMode* Game)
{
	if (BlinkLeft > 0.f)
	{
		BlinkLeft -= Dt;
	}

	AHGMonster* Monster = Game->GetMonster();
	const FVector Chest = Monster ? Monster->GetActorLocation() + FVector(0, 0, 160) : FVector::ZeroVector;
	const bool bStaring = Monster && FVector::Dist(EyeLocation(), Chest) < 2400.f && CanObserve(Chest);
	Composure = FMath::Clamp(Composure + (bStaring ? -Dt / 4.f : Dt / 5.f), 0.f, 1.f);
	if (Composure > 0.f)
	{
		return;
	}

	Composure = 1.f;
	if (Monster->Kind == EHGMonster::Walker)
	{
		// Nobody can hold a stare forever.
		BlinkLeft = 0.6f;
		Game->Say(TEXT("You blinked."), 1.5f);
	}
	else
	{
		++Screams;
		Game->ReportNoise(GetActorLocation(), 4000.f, 2.f);
		Game->Say(TEXT("AAAAAAAAAH!     (that was you)"), 2.5f);
		LastCause = TEXT("screamed at it, which it took personally");
	}
}

void AHGPlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = Cast<APlayerController>(GetController());
	AHGGameMode* Game = GetWorld()->GetAuthGameMode<AHGGameMode>();
	if (!PC || !Game)
	{
		return;
	}

	HandleKeys(PC, Game);

	if (bDead)
	{
		// The camera ends up on the floor, on its side, like the rest of you.
		const FVector Want(0.f, 0.f, -62.f);
		Camera->SetRelativeLocation(FMath::VInterpTo(Camera->GetRelativeLocation(), Want, DeltaSeconds, 5.f));
		FRotator Control = PC->GetControlRotation();
		Control.Roll = FMath::FInterpTo(Control.Roll, 75.f, DeltaSeconds, 5.f);
		PC->SetControlRotation(Control);
		return;
	}
	if (Game->IsOver())
	{
		return;
	}

	// Look.
	float MouseX = 0.f;
	float MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	FRotator Control = PC->GetControlRotation();
	Control.Yaw += MouseX * LookDegreesPerCount;
	Control.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Control.Pitch) + MouseY * LookDegreesPerCount, -82.f, 82.f);
	Control.Roll = 0.f;
	PC->SetControlRotation(Control);

	// Move.
	const float Forward = (PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	const float Right = (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	const bool bSprint = PC->IsInputKeyDown(EKeys::LeftShift);
	const bool bTiptoe = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::C);
	GetCharacterMovement()->MaxWalkSpeed = bTiptoe ? TiptoeSpeed : (bSprint ? SprintSpeed : WalkSpeed);
	const FRotationMatrix Facing(FRotator(0.f, Control.Yaw, 0.f));
	AddMovementInput(Facing.GetUnitAxis(EAxis::X), Forward);
	AddMovementInput(Facing.GetUnitAxis(EAxis::Y), Right);

	// Footsteps. Tiptoeing is silent, walking carries a little, sprinting carries a long way.
	const float Speed = GetVelocity().Size2D();
	StepBank += Speed * DeltaSeconds;
	if (StepBank > 170.f)
	{
		StepBank = 0.f;
		if (!bTiptoe)
		{
			Game->ReportNoise(GetActorLocation(), Speed > WalkSpeed + 40.f ? 2000.f : 650.f, 2.f, true);
		}
	}

	UpdateFlashlight(DeltaSeconds, Game);
	UpdateComposure(DeltaSeconds, Game);

	// The held item bobs with the walk and kicks when used.
	Bob += Speed * DeltaSeconds * 0.035f;
	UseKick = FMath::FInterpTo(UseKick, 0.f, DeltaSeconds, 9.f);
	const float Sway = FMath::Min(Speed / WalkSpeed, 1.6f);
	Hand->SetRelativeLocation(HandRest + FVector(-UseKick * 9.f, FMath::Sin(Bob) * 1.4f * Sway, FMath::Abs(FMath::Cos(Bob)) * 1.6f * Sway + UseKick * 3.f));
	Hand->SetRelativeRotation(FRotator(UseKick * 14.f, 0.f, 0.f));
}
