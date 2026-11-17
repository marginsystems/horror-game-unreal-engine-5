#include "HGMonster.h"

#include "Components/AudioComponent.h"
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

	LastSpot = GetActorLocation();
	if (Kind == EHGMonster::Mouth)
	{
		Voice = HG::LoopOn(Root, TEXT("mouth_loop"), 0.45f);
	}

	SetState(Kind == EHGMonster::Librarian ? EState::Patrol : EState::Hunt);
	PickPatrolGoal();
}

void AHGMonster::Footsteps()
{
	// Each monster is heard before it is seen, and each sounds like itself.
	const FVector Here = GetActorLocation();
	const float Moved = FVector::Dist2D(Here, LastSpot);
	LastSpot = Here;
	if (Moved <= 0.f || Moved > 300.f)
	{
		return;
	}
	StepBank += Moved;
	const float Stride = Kind == EHGMonster::Walker ? 165.f : (Kind == EHGMonster::Librarian ? (State == EState::Investigate ? 190.f : 115.f) : 150.f);
	if (StepBank < Stride)
	{
		return;
	}
	StepBank = 0.f;
	const float Pitch = FMath::FRandRange(0.92f, 1.08f);
	switch (Kind)
	{
	case EHGMonster::Librarian: HG::PlayAt(this, TEXT("heels"), Here, State == EState::Investigate ? 1.f : 0.6f, Pitch); break;
	case EHGMonster::Mouth: HG::PlayAt(this, TEXT("squelch"), Here, State == EState::Charge ? 1.2f : 0.7f, Pitch * (State == EState::Charge ? 1.3f : 1.f)); break;
	default: HG::PlayAt(this, TEXT("gary_step"), Here, 0.9f, Pitch); break;
	}
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

void AHGMonster::BuildStandIn(const FLinearColor& Color, float Height)
{
	// Only seen when Content/Models did not come down with the checkout (Git LFS not installed).
	HG::AddShape(this, Body, TEXT("Cylinder"), FVector(0, 0, Height * 0.5f), FVector(0.5f, 0.5f, Height / 100.f), Color);
}

void AHGMonster::BuildWalker()
{
	// A shop mannequin, modelled in Scripts/make_models.py. The pivots below match PIVOTS there.
	if (!HG::AddModel(this, Body, TEXT("Gary_Body")))
	{
		BuildStandIn(FLinearColor(0.012f, 0.012f, 0.016f), 250.f);
	}
	Head = HG::AddPivot(this, Body, FVector(0, 0, 226));
	HG::AddModel(this, Head, TEXT("Gary_Head"));
	for (const float Side : { -1.f, 1.f })
	{
		USceneComponent* Shoulder = HG::AddPivot(this, Body, FVector(0, Side * 27, 202));
		// One arm mesh, flipped for the other side so both thumbs point in.
		HG::AddModel(this, Shoulder, TEXT("Gary_Arm"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f, Side, 1.f));
		Arms.Add(Shoulder);
	}

	MakeGlow(FVector(40, 0, 250), FLinearColor(0.8f, 0.9f, 1.f), 40.f);
}

void AHGMonster::BuildLibrarian()
{
	if (!HG::AddModel(this, Body, TEXT("Librarian_Body")))
	{
		BuildStandIn(FLinearColor(0.06f, 0.09f, 0.07f), 240.f);
	}
	// Her head is an open book with reading glasses on it.
	Head = HG::AddPivot(this, Body, FVector(0, 0, 220));
	HG::AddModel(this, Head, TEXT("Librarian_Head"));
	for (const float Side : { -1.f, 1.f })
	{
		USceneComponent* Shoulder = HG::AddPivot(this, Body, FVector(0, Side * 24, 193));
		// One arm hangs. The other is modelled with a finger already on its way to where her lips would be.
		HG::AddModel(this, Shoulder, Side < 0.f ? TEXT("Librarian_Arm") : TEXT("Librarian_Shush"));
		Arms.Add(Shoulder);
	}

	MakeGlow(FVector(50, 0, 240), FLinearColor(0.7f, 1.f, 0.6f), 70.f);
}

void AHGMonster::BuildMouth()
{
	if (!HG::AddModel(this, Body, TEXT("Mouth_Bottom")))
	{
		BuildStandIn(FLinearColor(0.28f, 0.02f, 0.03f), 190.f);
	}
	// The top jaw hinges at the back so it can chew.
	Jaw = HG::AddPivot(this, Body, FVector(-60, 0, 100));
	HG::AddModel(this, Jaw, TEXT("Mouth_Top"));

	MakeGlow(FVector(130, 0, 110), FLinearColor(1.f, 0.1f, 0.05f), 260.f);
}

void AHGMonster::Animate(float Dt)
{
	if (Jaw)
	{
		// Breathing when idle, wide open for the wind-up, chewing on the way, slack when it has hit a wall.
		float Want = 16.f + 8.f * FMath::Sin(Age * 2.4f);
		if (StunLeft > 0.f) { Want = 27.f + 3.f * FMath::Sin(Age * 5.f); }
		else if (State == EState::WindUp) { Want = 58.f; }
		else if (State == EState::Charge) { Want = 32.f + 26.f * FMath::Sin(Age * 20.f); }
		JawAngle = FMath::FInterpTo(JawAngle, Want, Dt, 14.f);
		Jaw->SetRelativeRotation(FRotator(JawAngle, 0.f, 0.f));
	}
	if (Head && Kind == EHGMonster::Librarian)
	{
		// She listens by turning the book from side to side, and leans in when she has heard something.
		const bool bChasing = State == EState::Investigate;
		const FRotator Want(bChasing ? -14.f : 0.f, bChasing ? 0.f : FMath::Sin(Age * 0.9f) * 24.f, 0.f);
		Head->SetRelativeRotation(FMath::RInterpTo(Head->GetRelativeRotation(), Want, Dt, 5.f));
		if (Arms.Num() > 0)
		{
			Arms[0]->SetRelativeRotation(FRotator(FMath::Sin(Age * (bChasing ? 12.f : 3.f)) * (bChasing ? 30.f : 8.f), 0.f, 0.f));
		}
	}
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
		// Anywhere from hanging behind him to reaching straight up, by way of reaching for you.
		Arm->SetRelativeRotation(FRotator(FMath::FRandRange(-50.f, 150.f), 0.f, FMath::FRandRange(-40.f, 40.f)));
	}
	if (Head)
	{
		Head->SetRelativeRotation(FRotator(FMath::FRandRange(-8.f, 12.f), FMath::FRandRange(-35.f, 35.f), FMath::FRandRange(-28.f, 28.f)));
	}
	Body->SetRelativeRotation(FRotator(FMath::FRandRange(-6.f, 10.f), 0.f, FMath::FRandRange(-12.f, 12.f)));
	HG::PlayAt(this, TEXT("gary_freeze"), GetActorLocation(), 0.8f, FMath::FRandRange(0.9f, 1.15f));
}

void AHGMonster::Stun(float Seconds)
{
	if (StunLeft <= 0.f)
	{
		HG::PlayAt(this, Kind == EHGMonster::Mouth ? TEXT("bonk") : TEXT("question"), GetActorLocation(), 1.2f, Kind == EHGMonster::Mouth ? 1.f : 1.35f);
	}
	StunLeft = FMath::Max(StunLeft, Seconds);
	Shout(Kind == EHGMonster::Mouth ? TEXT("bonk") : TEXT("?!"), FMath::Min(Seconds, 2.5f), FColor::White);
}

bool AHGMonster::HearNoise(const FVector& Location, float HoldSeconds, bool bFootstep)
{
	if (StunLeft > 0.f || State == EState::Charge || State == EState::WindUp)
	{
		return false;
	}
	// Only the Librarian cares about footsteps. The others need something more interesting.
	if (bFootstep && Kind != EHGMonster::Librarian)
	{
		return false;
	}

	NoiseLocation = Location;
	LingerSeconds = HoldSeconds;
	const bool bAlreadyOnIt = State == EState::Investigate;
	if (!GoTo(Location))
	{
		return false;
	}
	if (!bAlreadyOnIt)
	{
		SetState(EState::Investigate);
		if (Kind == EHGMonster::Librarian)
		{
			Shout(TEXT("SHHH!"), 1.4f, FColor(200, 255, 170));
			HG::PlayAt(this, TEXT("shush"), GetActorLocation(), 1.3f);
		}
		else
		{
			Shout(TEXT("?"), 1.2f, FColor::White);
			HG::PlayAt(this, TEXT("question"), GetActorLocation(), 1.f, Kind == EHGMonster::Mouth ? 0.6f : 1.f);
		}
	}
	return true;
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
	Animate(DeltaSeconds);

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
	Footsteps();

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
	if (Head)
	{
		Head->SetRelativeRotation(FRotator(8.f, 0.f, 0.f));
	}
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
			HG::PlayAt(this, TEXT("mouth_roar"), Centre, 1.3f);
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
