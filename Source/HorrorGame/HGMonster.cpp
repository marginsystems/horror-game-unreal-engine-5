#include "HGMonster.h"

#include "Components/AudioComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "HGGameMode.h"
#include "HGLevel.h"
#include "HGPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const float CatchDistance = 150.f;
	const float WalkerSpeed = 430.f;
	const float LibrarianStroll = 170.f;
	const float LibrarianRun = 560.f;
	const float MouthCreep = 240.f;
	const float MouthCharge = 1150.f;
	/** The Mouth's body rides this far above where the model was built, slung between its legs. */
	const float MouthLift = 40.f;
	/** Turns the imported human to face the way the monster walks. */
	const float HumanYaw = -90.f;
}

AHGMonster::AHGMonster()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

FVector AHGMonster::Chest() const
{
	// Follows the posed body, so it is right when Gary is on a wall or the ceiling.
	return Body ? Body->GetComponentTransform().TransformPosition(FVector(0, 0, 160)) : GetActorLocation() + FVector(0, 0, 160);
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

bool AHGMonster::BuildCreature(const TCHAR* MeshPath, const TCHAR* InClipPrefix, float Scale)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh)
	{
		return false;
	}
	Rig = NewObject<USkeletalMeshComponent>(this);
	Rig->SetMobility(EComponentMobility::Movable);
	Rig->SetupAttachment(Body);
	Rig->SetSkeletalMeshAsset(Mesh);
	Rig->SetRelativeScale3D(FVector(Scale));
	Rig->SetRelativeRotation(FRotator(0.f, HumanYaw, 0.f));
	Rig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rig->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Rig->RegisterComponent();
	AddInstanceComponent(Rig);
	ClipPrefix = InClipPrefix;
	return true;
}

bool AHGMonster::BuildHuman(float Scale, const FLinearColor& Tint)
{
	// A rigged body and an animation library that were made for each other (Quaternius), kept in the
	// private assets plugin. The library's own skeleton plays the clips; the body copies it by bone name.
	const TCHAR* RigPath = TEXT("/HorrorPrivate/Characters/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_Standard.UAL1_Standard");
	const TCHAR* SkinFolder = TEXT("/HorrorPrivate/Characters/Human/Superhero_Male_FullBody/SkeletalMeshes");
	USkeletalMesh* RigMesh = LoadObject<USkeletalMesh>(nullptr, RigPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!RigMesh)
	{
		return false;
	}

	Rig = NewObject<USkeletalMeshComponent>(this);
	Rig->SetMobility(EComponentMobility::Movable);
	Rig->SetupAttachment(Body);
	Rig->SetSkeletalMeshAsset(RigMesh);
	Rig->SetRelativeScale3D(FVector(Scale));
	Rig->SetRelativeRotation(FRotator(0.f, HumanYaw, 0.f));
	Rig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rig->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Rig->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Rig->SetVisibility(false);
	Rig->RegisterComponent();
	AddInstanceComponent(Rig);

	int32 Skins = 0;
	UTexture* PaleSkin = LoadObject<UTexture>(nullptr, TEXT("/HorrorPrivate/Characters/Human/Skins/T_Superhero_Male_Ligh.T_Superhero_Male_Ligh"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	// No eyebrows, and eyes with nothing in them.
	for (const TCHAR* Part : { TEXT("SuperHero_Male"), TEXT("Eyes") })
	{
		const bool bEyes = FCString::Strcmp(Part, TEXT("Eyes")) == 0;
		const FString AssetPath = FString::Printf(TEXT("%s/%s.%s"), SkinFolder, Part, Part);
		USkeletalMesh* SkinMesh = LoadObject<USkeletalMesh>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!SkinMesh)
		{
			continue;
		}
		USkeletalMeshComponent* Skin = NewObject<USkeletalMeshComponent>(this);
		Skin->SetMobility(EComponentMobility::Movable);
		Skin->SetupAttachment(Rig);
		Skin->SetSkeletalMeshAsset(SkinMesh);
		Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Skin->SetLeaderPoseComponent(Rig);
		Skin->RegisterComponent();
		AddInstanceComponent(Skin);
		for (int32 i = 0; i < Skin->GetNumMaterials(); ++i)
		{
			if (UMaterialInstanceDynamic* Material = Skin->CreateDynamicMaterialInstance(i))
			{
				Material->SetVectorParameterValue(TEXT("BaseColorFactor"), bEyes ? FLinearColor(0.f, 0.f, 0.f) : Tint);
				if (PaleSkin && !bEyes)
				{
					Material->SetTextureParameterValue(TEXT("BaseColorTexture"), PaleSkin);
				}
			}
		}
		++Skins;
	}
	if (Skins == 0)
	{
		// No body to wear: show the library's own mannequin instead.
		Rig->SetVisibility(true);
	}
	ClipPrefix = TEXT("/HorrorPrivate/Characters/UAL1/UAL1_Standard/SkeletalMeshes/UAL1_Standard");
	PlayClip(TEXT("Idle_Loop"));
	return true;
}

void AHGMonster::PlayClip(const TCHAR* Name, float Rate, bool bLoop)
{
	if (!Rig)
	{
		return;
	}
	if (Clip != FName(Name))
	{
		const FString AssetPath = FString::Printf(TEXT("%s%s.%s%s"), *ClipPrefix, Name, *FPaths::GetCleanFilename(ClipPrefix), Name);
		if (UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Rig->PlayAnimation(Sequence, bLoop);
			Clip = FName(Name);
		}
	}
	Rig->SetPlayRate(Rate);
}

void AHGMonster::BuildWalker()
{
	// First choice: The Hollow Walker from Fab, a faceless thing with claws to its knees.
	if (BuildCreature(TEXT("/HorrorPrivate/Monsters/HollowWalker/HollowWalker/SkeletalMeshes/HollowWalker.HollowWalker"),
		TEXT("/HorrorPrivate/Monsters/HollowWalker/HollowWalker/SkeletalMeshes/HollowWalker"), 1.5f))
	{
		RunClip = TEXT("Running");
		PlayClip(TEXT("Walking"), 0.f);
		MakeGlow(FVector(60, 0, 190), FLinearColor(0.55f, 0.7f, 1.f), 120.f);
		return;
	}
	if (BuildHuman(1.38f, FLinearColor(0.62f, 0.74f, 0.82f)))
	{
		MakeGlow(FVector(40, 0, 250), FLinearColor(0.8f, 0.9f, 1.f), 40.f);
		return;
	}
	// A shop mannequin, modelled in Scripts/make_models.py. The pivots below match PIVOTS there.
	if (!HG::AddModel(this, Body, TEXT("Gary_Body")))
	{
		BuildStandIn(FLinearColor(0.012f, 0.012f, 0.016f), 250.f);
	}
	Head = HG::AddPivot(this, Body, FVector(0, 0, 226));
	HG::AddModel(this, Head, TEXT("Gary_Head"));
	for (const float Side : { -1.f, 1.f })
	{
		USceneComponent* Shoulder = HG::AddPivot(this, Body, FVector(0, Side * 25, 202));
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
	if (!HG::AddModel(this, Body, TEXT("Mouth_Bottom"), FVector(0, 0, MouthLift)))
	{
		BuildStandIn(FLinearColor(0.28f, 0.02f, 0.03f), 190.f);
	}
	// The top jaw hinges at the back so it can chew.
	Jaw = HG::AddPivot(this, Body, FVector(-60, 0, 100 + MouthLift));
	HG::AddModel(this, Jaw, TEXT("Mouth_Top"));

	// Six legs, built here because they are solved every frame. The sockets match LEG_SOCKETS in make_models.py.
	static const FVector Sockets[3] = { FVector(45, 74, 80), FVector(0, 86, 80), FVector(-45, 74, 80) };
	static const float Reach[3] = { 170.f, 20.f, -140.f };
	const FLinearColor Skin = FLinearColor::FromSRGBColor(FColor(0xA8, 0x8E, 0x84));
	for (const float Side : { -1.f, 1.f })
	{
		for (int32 i = 0; i < 3; ++i)
		{
			FLeg Leg;
			Leg.Hip = FVector(Sockets[i].X, Side * Sockets[i].Y, Sockets[i].Z + MouthLift);
			Leg.Side = Side;
			Leg.Along = Reach[i];
			Leg.Group = (i + (Side > 0.f ? 1 : 0)) % 2;
			Leg.Upper = HG::AddShape(this, Root, TEXT("Cylinder"), FVector::ZeroVector, FVector(0.15f, 0.15f, 1.f), Skin);
			Leg.Lower = HG::AddShape(this, Root, TEXT("Cone"), FVector::ZeroVector, FVector(0.14f, 0.14f, 1.f), Skin * 0.75f);
			Leg.Knee = HG::AddShape(this, Root, TEXT("Sphere"), FVector::ZeroVector, FVector(0.2f), Skin);
			for (UStaticMeshComponent* Part : { Leg.Upper, Leg.Lower, Leg.Knee })
			{
				Part->SetUsingAbsoluteLocation(true);
				Part->SetUsingAbsoluteRotation(true);
				Part->SetUsingAbsoluteScale(true);
				HG::MarkMoving(Part);
			}
			Legs.Add(Leg);
		}
	}

	MakeGlow(FVector(130, 0, 110 + MouthLift), FLinearColor(1.f, 0.1f, 0.05f), 260.f);
}

void AHGMonster::UpdateLegs(float Dt)
{
	if (Legs.Num() == 0 || !Level)
	{
		return;
	}
	const float UpperLength = 190.f;
	const float LowerLength = 230.f;
	const FVector Here = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const bool bFast = State == EState::Charge;
	const float Stride = bFast ? 230.f : 120.f;
	const float StepSeconds = bFast ? 0.07f : 0.16f;

	int32 Stepping[2] = { 0, 0 };
	for (const FLeg& Leg : Legs)
	{
		if (Leg.Step < 1.f) { ++Stepping[Leg.Group]; }
	}

	auto Place = [](UStaticMeshComponent* Part, const FVector& A, const FVector& B, float Thickness)
	{
		const FVector Span = B - A;
		Part->SetWorldLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(Span).Rotator());
		Part->SetWorldScale3D(FVector(Thickness, Thickness, FMath::Max(Span.Size(), 1.f) / 100.f));
	};

	for (FLeg& Leg : Legs)
	{
		// Where this foot would like to be: on the wall beside it if there is one in reach, else on the floor.
		const FVector Out = Right * Leg.Side;
		FVector Base = Here + Forward * (Leg.Along + (bFast ? 140.f : 40.f));
		if (!Level->IsOpen(Level->ToTile(Base)))
		{
			Base = Here + Forward * (Leg.Along * 0.2f);
		}
		const float Wall = Level->WallDistance(Base, Out, 330.f);
		const FVector Want = Wall >= 0.f
			? Base + Out * (Wall - 6.f) + FVector(0.f, 0.f, 140.f + 55.f * Leg.Group + 0.25f * FMath::Abs(Leg.Along))
			: Base + Out * 230.f;

		if (!Leg.bPlaced || FVector::Dist(Leg.Foot, Want) > 900.f)
		{
			Leg.Foot = Want;
			Leg.Step = 1.f;
			Leg.bPlaced = true;
		}
		else if (Leg.Step < 1.f)
		{
			// Mid step: swing in an arc, away from the wall and up.
			Leg.Step = FMath::Min(1.f, Leg.Step + Dt / StepSeconds);
			const float Eased = FMath::SmoothStep(0.f, 1.f, Leg.Step);
			Leg.Foot = FMath::Lerp(Leg.From, Want, Eased) + (FVector(0.f, 0.f, 55.f) - Out * 45.f) * FMath::Sin(PI * Leg.Step);
		}
		else
		{
			// Planted. It lets go once the body has left it behind, three legs at a time.
			const float Behind = FVector::Dist(Leg.Foot, Want);
			if ((Behind > Stride && Stepping[1 - Leg.Group] == 0) || Behind > Stride * 2.2f)
			{
				Leg.From = Leg.Foot;
				Leg.Step = 0.f;
				++Stepping[Leg.Group];
			}
		}

		// Two bones from hip to foot, knee pushed up and out.
		const FVector Hip = Body->GetComponentTransform().TransformPosition(Leg.Hip);
		const FVector ToFoot = Leg.Foot - Hip;
		const FVector Direction = ToFoot.GetSafeNormal(1.f, Out);
		const float Distance = FMath::Clamp(ToFoot.Size(), 60.f, UpperLength + LowerLength - 4.f);
		const FVector Foot = Hip + Direction * Distance;
		const float CosHip = FMath::Clamp((UpperLength * UpperLength + Distance * Distance - LowerLength * LowerLength) / (2.f * UpperLength * Distance), -1.f, 1.f);
		FVector Pole = FVector(0.f, 0.f, 1.f) + Out * 0.35f;
		Pole = (Pole - Direction * FVector::DotProduct(Pole, Direction)).GetSafeNormal(1.f, FVector::UpVector);
		FVector Knee = Hip + Direction * (UpperLength * CosHip) + Pole * (UpperLength * FMath::Sqrt(1.f - CosHip * CosHip));
		Knee.Z = FMath::Min(Knee.Z, AHGLevel::WallHeight - 14.f);

		Place(Leg.Upper, Hip, Knee, 0.15f);
		Place(Leg.Lower, Knee, Foot, 0.14f);
		Leg.Knee->SetWorldLocation(Knee);
	}
}

void AHGMonster::Animate(float Dt, AHGPlayer* Player)
{
	UpdateLegs(Dt);
	if (Head && Kind == EHGMonster::Walker)
	{
		// Whatever the rest of him is doing, and whichever way up, his face stays on you.
		const FRotator Want = (Player->EyeLocation() - Head->GetComponentLocation()).Rotation();
		Head->SetWorldRotation(FMath::RInterpTo(Head->GetComponentRotation(), Want, Dt, bWasObserved ? 3.f : 12.f));
	}
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
		// Every few seconds the book shakes too fast to follow, then goes back to listening.
		TwitchIn -= Dt;
		if (TwitchIn <= 0.f)
		{
			TwitchLeft = FMath::FRandRange(0.25f, 0.6f);
			TwitchIn = FMath::FRandRange(1.5f, 5.f);
		}
		if (TwitchLeft > 0.f && StunLeft <= 0.f)
		{
			TwitchLeft -= Dt;
			Head->SetRelativeRotation(FRotator(FMath::FRandRange(-14.f, 14.f), FMath::FRandRange(-45.f, 45.f), FMath::FRandRange(-32.f, 32.f)));
		}
		else
		{
			Head->SetRelativeRotation(FMath::RInterpTo(Head->GetRelativeRotation(), Want, Dt, 5.f));
		}
		if (Arms.Num() > 0)
		{
			// Her free arm trails behind her when she runs.
			Arms[0]->SetRelativeRotation(FRotator((bChasing ? -55.f : 0.f) + FMath::Sin(Age * (bChasing ? 12.f : 3.f)) * (bChasing ? 22.f : 8.f), 0.f, 0.f));
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
	// Caught. Wherever he had got to is where he stays: upright, down on his knuckles,
	// head-down on the nearest wall, or hanging from the ceiling.
	const FVector Here = GetActorLocation();
	const AHGPlayer* Player = Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
	FVector ToPlayer = Player ? (Player->GetActorLocation() - Here).GetSafeNormal2D() : GetActorForwardVector();
	if (ToPlayer.IsNearlyZero()) { ToPlayer = GetActorForwardVector(); }

	FVector WallDirection = FVector::ZeroVector;
	float WallAt = -1.f;
	for (const FVector& Direction : { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0) })
	{
		const float Distance = Level ? Level->WallDistance(Here, Direction, 260.f) : -1.f;
		if (Distance >= 0.f && (WallAt < 0.f || Distance < WallAt))
		{
			WallAt = Distance;
			WallDirection = Direction;
		}
	}

	const float Choice = FMath::FRand();
	FVector2D ArmPitch(-50.f, 150.f);
	float ArmSpread = 40.f;
	// Only the shape-built Gary clings to walls and ceilings. A rigged body frozen in a running
	// pose just looks like it is floating, so it stays on its feet.
	if (!Rig && Choice < 0.35f && WallAt >= 0.f)
	{
		// Belly to the wall, head towards the floor, like something that came down it.
		const FVector Spine = FVector(0, 0, -1).RotateAngleAxis(FMath::FRandRange(-70.f, 70.f), WallDirection);
		const FVector Middle = Here + WallDirection * (WallAt - 20.f) + FVector(0, 0, 185.f);
		Body->SetWorldLocationAndRotation(Middle - Spine * 130.f, FRotationMatrix::MakeFromXZ(WallDirection, Spine).Rotator());
		ArmPitch = FVector2D(140.f, 178.f);
		ArmSpread = 50.f;
	}
	else if (!Rig && Choice < 0.55f)
	{
		// Standing on the ceiling, arms hanging down at you.
		Body->SetWorldLocationAndRotation(FVector(Here.X, Here.Y, AHGLevel::WallHeight - 3.f), FRotationMatrix::MakeFromXZ(ToPlayer, FVector(0, 0, -1)).Rotator());
		ArmPitch = FVector2D(105.f, 150.f);
		ArmSpread = 30.f;
	}
	else if (Choice < 0.78f && !Rig)
	{
		// Down on his knuckles.
		Body->SetRelativeLocationAndRotation(FVector(-60.f, 0.f, 5.f), FRotator(-55.f, 0.f, FMath::FRandRange(-10.f, 10.f)));
		ArmPitch = FVector2D(40.f, 70.f);
		ArmSpread = 25.f;
	}
	else
	{
		Body->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(FMath::FRandRange(-6.f, 10.f), 0.f, FMath::FRandRange(-12.f, 12.f)));
	}
	for (USceneComponent* Arm : Arms)
	{
		Arm->SetRelativeRotation(FRotator(FMath::FRandRange(ArmPitch.X, ArmPitch.Y), 0.f, FMath::FRandRange(-ArmSpread, ArmSpread)));
	}
	if (Rig)
	{
		// Stopped dead, mid-stride, exactly as he was.
		Rig->SetPlayRate(0.f);
	}
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
			TwitchLeft = 0.5f;
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
	if (Level->bWorld)
	{
		// A ready-made map: route over the floor the level walked when it loaded.
		PathIndex = 0;
		return Level->FindWorldPath(Here, Target - FVector(0, 0, 88), Path);
	}
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
		// Text-built places are flat at height zero; a ready-made map has stairs, so keep the route's height.
		const bool bFlat = !Level || !Level->bWorld;
		FVector To = Path[PathIndex] - Here;
		if (bFlat) { To.Z = 0.f; }
		const float Distance = To.Size();
		if (Distance <= Budget)
		{
			SetActorLocation(FVector(Path[PathIndex].X, Path[PathIndex].Y, bFlat ? 0.f : Path[PathIndex].Z));
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
		GoTo(Level->bWorld ? Level->RandomSpot() + FVector(0, 0, 88) : Level->ToWorld(Level->RandomOpenTile()));
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
	Animate(DeltaSeconds, Player);

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

	if (Game->IsOver() || Game->IsLoading())
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
	// On a map with storeys, being directly above or below does not count as being caught.
	const bool bSameStorey = !Level->bWorld || FMath::Abs((There.Z - 88.f) - Here.Z) < 150.f;
	if (!bFrozen && StunLeft <= 0.f && bSameStorey && FVector::Dist2D(Here, There) < CatchDistance && Level->HasLineOfSight(Here + FVector(0, 0, 100), There))
	{
		Game->PlayerCaught(this);
	}
}

void AHGMonster::TickWalker(float Dt, AHGPlayer* Player)
{
	if (Player->CanObserve(Chest()))
	{
		if (!bWasObserved)
		{
			StrikeAPose();
			bWasObserved = true;
		}
		return;
	}
	bWasObserved = false;

	// In a ready-made map he is never far away, so he gives you a head start before he comes.
	if (Level->bWorld && Age < 14.f)
	{
		return;
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

	const FVector Target = State == EState::Investigate ? NoiseLocation : Player->GetActorLocation();
	RepathIn -= Dt;
	if (RepathIn <= 0.f)
	{
		GoTo(Target);
		RepathIn = 0.3f;
	}

	if (Rig)
	{
		// Nobody is looking, so he runs.
		Body->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		PlayClip(RunClip, 1.15f);
		const bool bArrivedRunning = Advance(WalkerSpeed, Dt);
		if (State == EState::Investigate && bArrivedRunning)
		{
			SetState(EState::Linger);
		}
		return;
	}
	// Nobody is looking, so he drops forward and scuttles, knuckles on the floor.
	Body->SetRelativeLocationAndRotation(FVector(-60.f, 0.f, 5.f), FRotator(-55.f + FMath::Sin(Age * 22.f) * 5.f, 0.f, FMath::Sin(Age * 11.f) * 8.f));
	for (int32 i = 0; i < Arms.Num(); ++i)
	{
		Arms[i]->SetRelativeRotation(FRotator(55.f + FMath::Sin(Age * 18.f + i * PI) * 42.f, 0.f, 0.f));
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
		Body->SetRelativeRotation(FRotator(-26.f, 0.f, FMath::Sin(Age * 14.f) * 7.f));
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
