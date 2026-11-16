#include "HGActors.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HGGameMode.h"
#include "HGMonster.h"
#include "HGPlayer.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	AHGGameMode* GameOf(const UObject* Context)
	{
		return Context && Context->GetWorld() ? Context->GetWorld()->GetAuthGameMode<AHGGameMode>() : nullptr;
	}

	AHGPlayer* PlayerOf(const UObject* Context)
	{
		return Cast<AHGPlayer>(UGameplayStatics::GetPlayerPawn(Context, 0));
	}
}

// ---------------------------------------------------------------------------------------------
// Pickup

AHGPickup::AHGPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void AHGPickup::BuildItem(AActor* Owner, USceneComponent* Parent, EHGItem Item, TArray<USceneComponent*>& OutParts)
{
	const FLinearColor Metal(0.05f, 0.05f, 0.06f);
	const FLinearColor Wood(0.30f, 0.16f, 0.07f);
	switch (Item)
	{
	case EHGItem::Fuse:
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cylinder"), FVector::ZeroVector, FVector(0.12f, 0.12f, 0.34f), FLinearColor(0.9f, 0.75f, 0.2f), 6.f));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cylinder"), FVector(0, 0, 17), FVector(0.14f, 0.14f, 0.06f), Metal));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cylinder"), FVector(0, 0, -17), FVector(0.14f, 0.14f, 0.06f), Metal));
		break;
	case EHGItem::Pistol:
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cube"), FVector(6, 0, 4), FVector(0.26f, 0.05f, 0.07f), Metal));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cube"), FVector(-4, 0, -5), FVector(0.07f, 0.05f, 0.14f), Wood, 0.f, false, FRotator(-12.f, 0.f, 0.f)));
		break;
	case EHGItem::Shotgun:
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cylinder"), FVector(18, 0, 4), FVector(0.05f, 0.05f, 0.6f), Metal, 0.f, false, FRotator(90.f, 0.f, 0.f)));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cube"), FVector(-18, 0, 0), FVector(0.3f, 0.06f, 0.1f), Wood));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cube"), FVector(10, 0, -2), FVector(0.16f, 0.07f, 0.07f), Wood));
		break;
	case EHGItem::Chicken:
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Sphere"), FVector::ZeroVector, FVector(0.32f, 0.16f, 0.18f), FLinearColor(1.f, 0.8f, 0.1f)));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Sphere"), FVector(18, 0, 6), FVector(0.12f, 0.11f, 0.12f), FLinearColor(1.f, 0.8f, 0.1f)));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cone"), FVector(25, 0, 5), FVector(0.05f, 0.05f, 0.07f), FLinearColor(1.f, 0.35f, 0.05f), 0.f, false, FRotator(-90.f, 0.f, 0.f)));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cube"), FVector(18, 0, 13), FVector(0.07f, 0.02f, 0.05f), FLinearColor(0.9f, 0.05f, 0.05f)));
		break;
	case EHGItem::Grenade:
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Sphere"), FVector::ZeroVector, FVector(0.14f, 0.14f, 0.18f), FLinearColor(0.12f, 0.2f, 0.08f)));
		OutParts.Add(HG::AddShape(Owner, Parent, TEXT("Cylinder"), FVector(0, 0, 10), FVector(0.05f, 0.05f, 0.06f), Metal));
		break;
	default:
		break;
	}
}

void AHGPickup::Setup(EHGItem InItem)
{
	Item = InItem;
	Spinner = HG::AddPivot(this, Root, FVector(0, 0, 0));
	TArray<USceneComponent*> Parts;
	BuildItem(this, Spinner, Item, Parts);
	const float Scale = Item == EHGItem::Fuse ? 1.6f : 2.2f;
	Spinner->SetRelativeScale3D(FVector(Scale));

	UPointLightComponent* Glow = NewObject<UPointLightComponent>(this);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetupAttachment(Root);
	Glow->SetIntensityUnits(ELightUnits::Unitless);
	Glow->SetIntensity(Item == EHGItem::Fuse ? 260.f : 120.f);
	Glow->SetLightColor(Item == EHGItem::Fuse ? FLinearColor(1.f, 0.8f, 0.25f) : FLinearColor(0.6f, 0.75f, 1.f));
	Glow->SetAttenuationRadius(260.f);
	Glow->SetCastShadows(false);
	Glow->RegisterComponent();
	AddInstanceComponent(Glow);
}

void AHGPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Spinner)
	{
		Spinner->SetRelativeRotation(FRotator(0.f, Age * 70.f, 12.f));
		Spinner->SetRelativeLocation(FVector(0, 0, FMath::Sin(Age * 2.2f) * 7.f));
	}

	AHGPlayer* Player = PlayerOf(this);
	AHGGameMode* Game = GameOf(this);
	if (!Player || !Game || Game->IsOver())
	{
		return;
	}
	if (FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) < 115.f)
	{
		if (Item == EHGItem::Fuse)
		{
			Game->CollectFuse();
			Destroy();
		}
		else if (Player->GiveItem(Item))
		{
			Destroy();
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Thrown chicken / grenade

AHGThrown::AHGThrown()
{
	PrimaryActorTick.bCanEverTick = true;
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Body);
}

void AHGThrown::Launch(EHGItem InItem, const FVector& Velocity)
{
	Item = InItem;
	const bool bChicken = Item == EHGItem::Chicken;

	Body->SetStaticMesh(HG::Shape(TEXT("Sphere")));
	Body->SetWorldScale3D(bChicken ? FVector(0.34f, 0.2f, 0.2f) : FVector(0.16f, 0.16f, 0.2f));
	HG::Paint(Body, bChicken ? FLinearColor(1.f, 0.8f, 0.1f) : FLinearColor(0.12f, 0.2f, 0.08f));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetSimulatePhysics(true);
	Body->SetNotifyRigidBodyCollision(true);
	Body->SetLinearDamping(0.25f);
	Body->SetAngularDamping(0.6f);
	Body->OnComponentHit.AddDynamic(this, &AHGThrown::OnHit);
	Body->SetPhysicsLinearVelocity(Velocity);
	Body->SetPhysicsAngularVelocityInDegrees(FVector(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), 0.f));

	if (bChicken)
	{
		HG::AddShape(this, Body, TEXT("Cone"), FVector(52, 0, 10), FVector(0.2f, 0.3f, 0.35f), FLinearColor(1.f, 0.35f, 0.05f), 0.f, false, FRotator(-90.f, 0.f, 0.f));
	}
	else
	{
		// Somewhere between "any second now" and "long after you stopped caring".
		FuseSeconds = FMath::FRandRange(35.f, 55.f);
	}
}

void AHGThrown::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (Item != EHGItem::Chicken || Age - LastSqueak < 0.3f || NormalImpulse.Size() < 30.f)
	{
		return;
	}
	LastSqueak = Age;
	if (AHGGameMode* Game = GameOf(this))
	{
		Game->ReportNoise(GetActorLocation(), 3200.f, 3.5f);
		Game->Say(TEXT("squeak"), 0.7f);
	}
}

void AHGThrown::Explode()
{
	bExploded = true;
	AHGGameMode* Game = GameOf(this);
	const FVector Here = GetActorLocation();

	Flash = NewObject<UPointLightComponent>(this);
	Flash->SetMobility(EComponentMobility::Movable);
	Flash->SetupAttachment(Body);
	Flash->SetIntensityUnits(ELightUnits::Unitless);
	Flash->SetIntensity(400000.f);
	Flash->SetLightColor(FLinearColor(1.f, 0.75f, 0.4f));
	Flash->SetAttenuationRadius(3000.f);
	Flash->RegisterComponent();
	FlashLeft = 0.45f;
	Body->SetVisibility(false, true);

	if (Game)
	{
		Game->ReportNoise(Here, 6000.f, 1.f);
		if (AHGMonster* Monster = Game->GetMonster())
		{
			if (FVector::Dist2D(Monster->GetActorLocation(), Here) < 1500.f)
			{
				Monster->Stun(10.f);
				Game->Say(TEXT("The grenade worked. It actually worked."), 4.f);
			}
			else
			{
				Game->Say(TEXT("The grenade went off. Nobody was near it."), 4.f);
			}
		}
		if (AHGPlayer* Player = PlayerOf(this))
		{
			if (FVector::Dist2D(Player->GetActorLocation(), Here) < 420.f)
			{
				Game->PlayerDied(TEXT("stood next to the only weapon that works"));
			}
		}
	}
}

void AHGThrown::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;

	if (bExploded)
	{
		FlashLeft -= DeltaSeconds;
		if (Flash)
		{
			Flash->SetIntensity(FMath::Max(0.f, FlashLeft / 0.45f) * 400000.f);
		}
		if (FlashLeft <= 0.f)
		{
			Destroy();
		}
		return;
	}

	if (Item == EHGItem::Grenade)
	{
		if (Age > FuseSeconds)
		{
			Explode();
		}
		return;
	}

	// A chicken that has come to rest can be picked up and thrown again.
	AHGPlayer* Player = PlayerOf(this);
	AHGGameMode* Game = GameOf(this);
	if (Age > 1.2f && Player && Game && !Game->IsOver()
		&& FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) < 120.f
		&& Player->GiveItem(EHGItem::Chicken))
	{
		Destroy();
	}
}

// ---------------------------------------------------------------------------------------------
// Confetti

AHGConfetti::AHGConfetti()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	InitialLifeSpan = 14.f;
}

void AHGConfetti::Burst(const FVector& Direction)
{
	static const FLinearColor Colors[] = {
		FLinearColor(1.f, 0.1f, 0.3f), FLinearColor(0.1f, 0.8f, 1.f), FLinearColor(1.f, 0.85f, 0.1f),
		FLinearColor(0.3f, 1.f, 0.3f), FLinearColor(0.8f, 0.3f, 1.f), FLinearColor(1.f, 0.5f, 0.1f),
	};
	for (int32 i = 0; i < 70; ++i)
	{
		const FVector Offset = FMath::VRand() * FMath::FRandRange(0.f, 14.f);
		UStaticMeshComponent* Bit = HG::AddShape(this, Root, TEXT("Cube"), Offset,
			FVector(0.07f, 0.07f, 0.008f), Colors[i % UE_ARRAY_COUNT(Colors)], 2.f, true,
			FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f));
		Bit->SetCollisionObjectType(ECC_PhysicsBody);
		Bit->SetCollisionResponseToAllChannels(ECR_Ignore);
		Bit->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Bit->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Bit->SetSimulatePhysics(true);
		Bit->SetLinearDamping(2.2f);
		Bit->SetAngularDamping(1.5f);
		const FVector Spread = (Direction + FMath::VRand() * 0.55f).GetSafeNormal();
		Bit->SetPhysicsLinearVelocity(Spread * FMath::FRandRange(500.f, 1500.f) + FVector(0, 0, FMath::FRandRange(100.f, 450.f)));
		Bit->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * 900.f);
	}

	UPointLightComponent* Pop = NewObject<UPointLightComponent>(this);
	Pop->SetMobility(EComponentMobility::Movable);
	Pop->SetupAttachment(Root);
	Pop->SetIntensityUnits(ELightUnits::Unitless);
	Pop->SetIntensity(900.f);
	Pop->SetLightColor(FLinearColor(1.f, 0.6f, 0.8f));
	Pop->SetAttenuationRadius(700.f);
	Pop->SetCastShadows(false);
	Pop->RegisterComponent();
}
