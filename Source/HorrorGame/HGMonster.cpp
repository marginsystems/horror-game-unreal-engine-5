#include "HGMonster.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "HGGameMode.h"
#include "HGLevel.h"
#include "HGPlayer.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const float CatchDistance = 150.f;
	const float WalkerSpeed = 430.f;
	const float LibrarianStroll = 170.f;
	const float LibrarianRun = 560.f;
	const float MouthCreep = 240.f;
	const float MouthCharge = 1150.f;
}

AHGMonster::AHGMonster()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

const TCHAR* AHGMonster::DisplayName() const
{
	switch (Kind)
	{
	case EHGMonster::Librarian: return TEXT("The Librarian");
	case EHGMonster::Mouth: return TEXT("The Mouth");
	default: return TEXT("Gary");
	}
}

const TCHAR* AHGMonster::CatchLine() const
{
	switch (Kind)
	{
	case EHGMonster::Librarian: return TEXT("The Librarian has asked you to be quiet. Permanently.");
	case EHGMonster::Mouth: return TEXT("The Mouth did the one thing it is good at.");
	default: return TEXT("You looked away from Gary.");
	}
}

FString AHGMonster::Mood() const
{
	if (StunLeft > 0.f) { return TEXT("stunned"); }
	if (Kind == EHGMonster::Walker && bWasObserved) { return TEXT("frozen"); }
	switch (State)
	{
	case EState::Investigate: return TEXT("investigating");
	case EState::Linger: return TEXT("lingering");
	case EState::Hunt: return TEXT("hunting");
	case EState::WindUp: return TEXT("winding up");
	case EState::Charge: return TEXT("charging");
	default: return TEXT("patrolling");
	}
}

void AHGMonster::Setup(EHGMonster InKind, AHGLevel* InLevel)
{
	Kind = InKind;
	Level = InLevel;
	Body = HG::AddPivot(this, Root, FVector::ZeroVector);

	switch (Kind)
	{
	case EHGMonster::Librarian: BuildLibrarian(); break;
	case EHGMonster::Mouth: BuildMouth(); break;
	default: BuildWalker(); break;
	}

	Bubble = NewObject<UTextRenderComponent>(this);
	Bubble->SetMobility(EComponentMobility::Movable);
	Bubble->SetupAttachment(Root);
	Bubble->SetRelativeLocation(FVector(0, 0, 305));
	Bubble->SetHorizontalAlignment(EHTA_Center);
	Bubble->SetWorldSize(46.f);
	Bubble->SetVisibility(false);
	Bubble->RegisterComponent();
	AddInstanceComponent(Bubble);

	SetState(Kind == EHGMonster::Librarian ? EState::Patrol : EState::Hunt);
	PickPatrolGoal();
}

void AHGMonster::MakeGlow(const FVector& RelLocation, const FLinearColor& Color, float Intensity)
{
	Glow = NewObject<UPointLightComponent>(this);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetupAttachment(Body);
	Glow->SetRelativeLocation(RelLocation);
	Glow->SetIntensityUnits(ELightUnits::Unitless);
	Glow->SetIntensity(Intensity);
	Glow->SetLightColor(Color);
	Glow->SetAttenuationRadius(420.f);
	Glow->SetCastShadows(false);
	Glow->RegisterComponent();
	AddInstanceComponent(Glow);
}

void AHGMonster::BuildWalker()
{
	const FLinearColor Skin(0.012f, 0.012f, 0.016f);
	for (const float Side : { -1.f, 1.f })
	{
		HG::AddShape(this, Body, TEXT("Cylinder"), FVector(0, Side * 13, 66), FVector(0.13f, 0.13f, 1.32f), Skin);
		USceneComponent* Shoulder = HG::AddPivot(this, Body, FVector(0, Side * 23, 216));
		HG::AddShape(this, Shoulder, TEXT("Cylinder"), FVector(0, 0, -72), FVector(0.085f, 0.085f, 1.44f), Skin);
		HG::AddShape(this, Shoulder, TEXT("Sphere"), FVector(0, 0, -148), FVector(0.16f, 0.1f, 0.22f), Skin);
		Arms.Add(Shoulder);
		HG::AddShape(this, Body, TEXT("Sphere"), FVector(14.5f, Side * 6.5f, 251), FVector(0.045f), FLinearColor::White, 60.f);
	}
	HG::AddShape(this, Body, TEXT("Cylinder"), FVector(0, 0, 176), FVector(0.3f, 0.2f, 0.9f), Skin);
	HG::AddShape(this, Body, TEXT("Sphere"), FVector(0, 0, 247), FVector(0.3f, 0.28f, 0.36f), Skin);

	MakeGlow(FVector(40, 0, 250), FLinearColor(0.8f, 0.9f, 1.f), 40.f);
}

void AHGMonster::BuildLibrarian()
{
	const FLinearColor Dress(0.06f, 0.09f, 0.07f);
	const FLinearColor Paper(0.85f, 0.8f, 0.65f);
	HG::AddShape(this, Body, TEXT("Cone"), FVector(0, 0, 88), FVector(1.15f, 1.15f, 1.76f), Dress);
	HG::AddShape(this, Body, TEXT("Cylinder"), FVector(0, 0, 188), FVector(0.3f, 0.36f, 0.5f), Dress);
	// Her head is an open book, spine towards you.
	HG::AddShape(this, Body, TEXT("Cube"), FVector(4, -15, 243), FVector(0.1f, 0.34f, 0.5f), Paper, 0.f, false, FRotator(0.f, -22.f, 0.f));
	HG::AddShape(this, Body, TEXT("Cube"), FVector(4, 15, 243), FVector(0.1f, 0.34f, 0.5f), Paper, 0.f, false, FRotator(0.f, 22.f, 0.f));
	HG::AddShape(this, Body, TEXT("Cube"), FVector(-6, 0, 243), FVector(0.06f, 0.1f, 0.56f), FLinearColor(0.3f, 0.04f, 0.04f));
	for (const float Side : { -1.f, 1.f })
	{
		USceneComponent* Shoulder = HG::AddPivot(this, Body, FVector(0, Side * 24, 205));
		HG::AddShape(this, Shoulder, TEXT("Cylinder"), FVector(0, 0, -45), FVector(0.07f, 0.07f, 0.9f), Paper * 0.6f);
		Arms.Add(Shoulder);
	}
	// One finger permanently on its way to where her lips would be.
	Arms[1]->SetRelativeRotation(FRotator(-150.f, 0.f, -12.f));

	MakeGlow(FVector(50, 0, 240), FLinearColor(0.7f, 1.f, 0.6f), 70.f);
}

void AHGMonster::BuildMouth()
{
	const FLinearColor Flesh(0.28f, 0.02f, 0.03f);
	HG::AddShape(this, Body, TEXT("Sphere"), FVector(0, 0, 100), FVector(1.9f, 1.8f, 1.7f), Flesh);
	HG::AddShape(this, Body, TEXT("Sphere"), FVector(52, 0, 100), FVector(1.25f, 1.45f, 1.3f), FLinearColor(0.005f, 0.f, 0.f));
	const int32 Teeth = 14;
	for (int32 i = 0; i < Teeth; ++i)
	{
		const float Angle = 2.f * PI * i / Teeth;
		const FVector At(98.f, FMath::Cos(Angle) * 62.f, 100.f + FMath::Sin(Angle) * 56.f);
		const FRotator Inward(FMath::RadiansToDegrees(Angle) + 90.f, 90.f, 0.f);
		HG::AddShape(this, Body, TEXT("Cone"), At, FVector(0.16f, 0.16f, 0.34f), FLinearColor(0.9f, 0.88f, 0.75f), 1.f, false, Inward);
	}
	for (const float Side : { -1.f, 1.f })
	{
		HG::AddShape(this, Body, TEXT("Cylinder"), FVector(-10, Side * 45, 12), FVector(0.3f, 0.3f, 0.25f), Flesh * 0.6f);
	}

	MakeGlow(FVector(130, 0, 110), FLinearColor(1.f, 0.1f, 0.05f), 260.f);
}

void AHGMonster::SetState(EState NewState)
{
	State = NewState;
	StateTime = 0.f;
}

void AHGMonster::Shout(const TCHAR* Text, float Seconds, const FColor& Color)
{
	if (Bubble)
	{
		Bubble->SetText(FText::FromString(Text));
		Bubble->SetTextRenderColor(Color);
		Bubble->SetVisibility(true);
		BubbleLeft = Seconds;
	}
}

void AHGMonster::StrikeAPose()
{
	// Caught mid-step. Whatever his arms were doing, they are doing it now.
	for (USceneComponent* Arm : Arms)
	{
		Arm->SetRelativeRotation(FRotator(FMath::FRandRange(-175.f, 50.f), 0.f, FMath::FRandRange(-55.f, 55.f)));
	}
	Body->SetRelativeRotation(FRotator(FMath::FRandRange(-6.f, 10.f), 0.f, FMath::FRandRange(-12.f, 12.f)));
}

void AHGMonster::Stun(float Seconds)
{
	StunLeft = FMath::Max(StunLeft, Seconds);
	Shout(Kind == EHGMonster::Mouth ? TEXT("bonk") : TEXT("?!"), FMath::Min(Seconds, 2.5f), FColor::White);
}

void AHGMonster::HearNoise(const FVector& Location, float HoldSeconds, bool bFootstep)
{
	if (StunLeft > 0.f || State == EState::Charge || State == EState::WindUp)
	{
		return;
	}
	// Only the Librarian cares about footsteps. The others need something more interesting.
	if (bFootstep && Kind != EHGMonster::Librarian)
	{
		return;
	}

	NoiseLocation = Location;
	LingerSeconds = HoldSeconds;
	const bool bAlreadyOnIt = State == EState::Investigate;
	if (!GoTo(Location))
	{
		return;
	}
	if (!bAlreadyOnIt)
	{
		SetState(EState::Investigate);
		if (Kind == EHGMonster::Librarian)
		{
			Shout(TEXT("SHHH!"), 1.4f, FColor(200, 255, 170));
		}
		else
		{
			Shout(TEXT("?"), 1.2f, FColor::White);
		}
	}
}

bool AHGMonster::GoTo(const FVector& Target)
{
	if (!Level)
	{
		return false;
	}
	const FVector Here = GetActorLocation();
	const FIntPoint From = Level->NearestOpenTile(Level->ToTile(Here));
	const FIntPoint To = Level->NearestOpenTile(Level->ToTile(Target));
	TArray<FIntPoint> Tiles;
	if (!Level->FindPath(From, To, Tiles))
	{
		return false;
	}

	Path.Reset();
	PathIndex = 0;
	// Step back onto the tile centre first when we are well off it, so we never cut a wall corner.
	const FVector FromCentre = Level->ToWorld(From);
	if (FVector::Dist2D(Here, FromCentre) > 70.f && Tiles.Num() > 0)
	{
		const FVector Next = Level->ToWorld(Tiles[0]);
		const bool bOnTheWay = FVector::DotProduct((Next - Here).GetSafeNormal2D(), (Next - FromCentre).GetSafeNormal2D()) > 0.95f;
		if (!bOnTheWay)
		{
			Path.Add(FromCentre);
		}
	}
	for (const FIntPoint& T : Tiles)
	{
		Path.Add(Level->ToWorld(T));
	}
	if (Level->IsOpen(Level->ToTile(Target)))
	{
		Path.Add(FVector(Target.X, Target.Y, 0.f));
	}
	return true;
}

bool AHGMonster::Advance(float Speed, float Dt)
{
	float Budget = Speed * Dt;
	while (PathIndex < Path.Num() && Budget > 0.f)
	{
		const FVector Here = GetActorLocation();
		FVector To = Path[PathIndex] - Here;
		To.Z = 0.f;
		const float Distance = To.Size();
		if (Distance <= Budget)
		{
			SetActorLocation(FVector(Path[PathIndex].X, Path[PathIndex].Y, 0.f));
			Budget -= Distance;
			++PathIndex;
			continue;
		}
		const FVector Direction = To / Distance;
		SetActorLocation(Here + Direction * Budget);
		Face(Direction, Dt);
		Budget = 0.f;
	}
	return PathIndex >= Path.Num();
}

void AHGMonster::Face(const FVector& Direction, float Dt, float Rate)
{
	if (Direction.IsNearlyZero())
	{
		return;
	}
	const FRotator Want(0.f, Direction.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, Dt, Rate));
}

void AHGMonster::PickPatrolGoal()
{
	if (Level)
	{
		GoTo(Level->ToWorld(Level->RandomOpenTile()));
	}
}

void AHGMonster::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AHGGameMode* Game = GetWorld()->GetAuthGameMode<AHGGameMode>();
	AHGPlayer* Player = Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Level || !Game || !Player)
	{
		return;
	}

	Age += DeltaSeconds;
	StateTime += DeltaSeconds;

	if (Bubble && BubbleLeft > 0.f)
	{
		BubbleLeft -= DeltaSeconds;
		const FVector ToEye = Player->EyeLocation() - Bubble->GetComponentLocation();
		Bubble->SetWorldRotation(ToEye.Rotation());
		if (BubbleLeft <= 0.f)
		{
			Bubble->SetVisibility(false);
		}
	}

	if (Game->IsOver())
	{
		return;
	}

	if (StunLeft > 0.f)
	{
		StunLeft -= DeltaSeconds;
		Body->SetRelativeRotation(FRotator(FMath::Sin(Age * 9.f) * 9.f, 0.f, FMath::Cos(Age * 7.f) * 14.f));
		if (StunLeft <= 0.f)
		{
			Body->SetRelativeRotation(FRotator::ZeroRotator);
			SetState(Kind == EHGMonster::Librarian ? EState::Patrol : EState::Hunt);
			RepathIn = 0.f;
		}
		return;
	}

	switch (Kind)
	{
	case EHGMonster::Librarian: TickLibrarian(DeltaSeconds, Player); break;
	case EHGMonster::Mouth: TickMouth(DeltaSeconds, Player); break;
	default: TickWalker(DeltaSeconds, Player); break;
	}

	const FVector Here = GetActorLocation();
	const FVector There = Player->GetActorLocation();
	const bool bFrozen = Kind == EHGMonster::Walker && bWasObserved;
	if (!bFrozen && StunLeft <= 0.f && FVector::Dist2D(Here, There) < CatchDistance && Level->HasLineOfSight(Here, There))
	{
		Game->PlayerCaught(this);
	}
}

void AHGMonster::TickWalker(float Dt, AHGPlayer* Player)
{
	const FVector Chest = GetActorLocation() + FVector(0, 0, 160);
	if (Player->CanObserve(Chest))
	{
		if (!bWasObserved)
		{
			StrikeAPose();
			bWasObserved = true;
		}
		return;
	}
	bWasObserved = false;

	if (State == EState::Linger)
	{
		if (StateTime >= LingerSeconds)
		{
			SetState(EState::Hunt);
			RepathIn = 0.f;
		}
		return;
	}

	const FVector Target = State == EState::Investigate ? NoiseLocation : Player->GetActorLocation();
	RepathIn -= Dt;
	if (RepathIn <= 0.f)
	{
		GoTo(Target);
		RepathIn = 0.3f;
	}

	// He leans into it while he moves.
	Body->SetRelativeRotation(FRotator(-8.f, 0.f, FMath::Sin(Age * 11.f) * 5.f));
	for (int32 i = 0; i < Arms.Num(); ++i)
	{
		Arms[i]->SetRelativeRotation(FRotator(FMath::Sin(Age * 9.f + i * PI) * 35.f, 0.f, 0.f));
	}

	const bool bArrived = Advance(WalkerSpeed, Dt);
	if (State == EState::Investigate && bArrived)
	{
		SetState(EState::Linger);
	}
}

void AHGMonster::TickLibrarian(float Dt, AHGPlayer* Player)
{
	switch (State)
	{
	case EState::Investigate:
		if (StateTime < 0.8f)
		{
			// Shush first. Manners.
			Face(NoiseLocation - GetActorLocation(), Dt, 6.f);
			return;
		}
		Body->SetRelativeRotation(FRotator(-10.f, 0.f, FMath::Sin(Age * 14.f) * 4.f));
		if (Advance(LibrarianRun, Dt))
		{
			SetState(EState::Linger);
		}
		break;

	case EState::Linger:
		Body->SetRelativeRotation(FRotator::ZeroRotator);
		SetActorRotation(GetActorRotation() + FRotator(0.f, 70.f * Dt * FMath::Sin(Age * 1.3f), 0.f));
		if (StateTime >= FMath::Max(LingerSeconds, 1.5f))
		{
			SetState(EState::Patrol);
			PickPatrolGoal();
		}
		break;

	default:
		Body->SetRelativeRotation(FRotator(0.f, 0.f, FMath::Sin(Age * 3.f) * 2.f));
		if (Advance(LibrarianStroll, Dt))
		{
			PickPatrolGoal();
		}
		break;
	}
}

void AHGMonster::TickMouth(float Dt, AHGPlayer* Player)
{
	const FVector Here = GetActorLocation();
	const FVector There = Player->GetActorLocation();

	if (State == EState::WindUp)
	{
		Body->SetRelativeLocation(FVector(FMath::FRandRange(-5.f, 5.f), FMath::FRandRange(-5.f, 5.f), 0.f));
		Face(ChargeDirection, Dt, 14.f);
		if (StateTime >= 0.75f)
		{
			Body->SetRelativeLocation(FVector::ZeroVector);
			SetState(EState::Charge);
		}
		return;
	}

	if (State == EState::Charge)
	{
		const FVector Next = Here + ChargeDirection * MouthCharge * Dt;
		if (!Level->IsOpen(Level->ToTile(Next + ChargeDirection * 150.f)))
		{
			// It cannot stop and it cannot turn. The wall does both for it.
			Stun(2.6f);
			return;
		}
		SetActorLocation(Next);
		SetActorRotation(FRotator(0.f, ChargeDirection.Rotation().Yaw, 0.f));
		return;
	}

	// A clear straight lane to the player is all it needs.
	const FIntPoint Mine = Level->ToTile(Here);
	const FIntPoint Theirs = Level->ToTile(There);
	const FVector Centre = Level->ToWorld(Mine);
	const bool bSameRow = Mine.Y == Theirs.Y && Mine.X != Theirs.X;
	const bool bSameColumn = Mine.X == Theirs.X && Mine.Y != Theirs.Y;
	if ((bSameRow || bSameColumn) && FVector::Dist2D(Here, Centre) < 90.f && Level->HasLineOfSight(Centre, Level->ToWorld(Theirs)))
	{
		const FIntPoint Step(FMath::Sign(Theirs.X - Mine.X), FMath::Sign(Theirs.Y - Mine.Y));
		if (FMath::Abs(Theirs.X - Mine.X) + FMath::Abs(Theirs.Y - Mine.Y) <= 14)
		{
			SetActorLocation(Centre);
			ChargeDirection = FVector(Step.X, Step.Y, 0.f);
			SetState(EState::WindUp);
			Shout(TEXT("!!"), 0.9f, FColor::Red);
			return;
		}
	}

	if (State == EState::Linger)
	{
		if (StateTime >= LingerSeconds)
		{
			SetState(EState::Hunt);
			RepathIn = 0.f;
		}
		return;
	}

	const FVector Target = State == EState::Investigate ? NoiseLocation : There;
	RepathIn -= Dt;
	if (RepathIn <= 0.f)
	{
		GoTo(Target);
		RepathIn = 0.5f;
	}
	Body->SetRelativeRotation(FRotator(0.f, 0.f, FMath::Sin(Age * 6.f) * 6.f));
	const bool bArrived = Advance(MouthCreep, Dt);
	if (State == EState::Investigate && bArrived)
	{
		SetState(EState::Linger);
	}
}
