#include "HGLevel.h"

#include "Algo/Reverse.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#include "Materials/MaterialInstanceDynamic.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	// Legend: # wall, . floor, L floor with a ceiling lamp, B prop (blocks walking),
	// P player, M monster, E exit, F fuse, 1 pistol, 2 shotgun, 3 rubber chicken, 4 grenade.
	// Every map is 21 x 15. Scripts/check_maps.py checks that everything can be reached.
	// MAPS-BEGIN
	const TCHAR* const ClinicMap[] = {
		TEXT("#####################"),
		TEXT("#P..L.#F....#..L...F#"),
		TEXT("#.....#.....#.......#"),
		TEXT("#..1..#.B...#.B.....#"),
		TEXT("###.#####.#####.###.#"),
		TEXT("#L.....L.....L.....L#"),
		TEXT("#.###.###.#.###.###.#"),
		TEXT("#.#F#.#2..#...#.#B..#"),
		TEXT("#.#.#.#...#.L.#.#..3#"),
		TEXT("#.#.#.#####...#.#...#"),
		TEXT("#.#.#.....#####.##.##"),
		TEXT("#L.....L.....L.....L#"),
		TEXT("###.#####.###.#####.#"),
		TEXT("#F....#B.M..#F.4...E#"),
		TEXT("#####################"),
	};

	const TCHAR* const OfficeMap[] = {
		TEXT("#####################"),
		TEXT("#P....#....L....#..F#"),
		TEXT("#.BB..#.BB...BB.#...#"),
		TEXT("#.....#.........#.1.#"),
		TEXT("#..L.....BB.L.......#"),
		TEXT("#.BB..#...F.....#.BB#"),
		TEXT("#.....#####.#####...#"),
		TEXT("#F.2......L.........#"),
		TEXT("####.##.BB...BB.##.##"),
		TEXT("#....#...........#..#"),
		TEXT("#.BB.#.L.BB.BB.L.#.F#"),
		TEXT("#....#...........#..#"),
		TEXT("#.L....####.####....#"),
		TEXT("#F..3...#.M.4.#....E#"),
		TEXT("#####################"),
	};

	const TCHAR* const MineMap[] = {
		TEXT("#####################"),
		TEXT("#P.......L.........F#"),
		TEXT("#.#.#####.#####.###.#"),
		TEXT("#.#.#F..#.#...#.#1#.#"),
		TEXT("#.#.###.#.#.L.#.#.#.#"),
		TEXT("#L......#.......#...#"),
		TEXT("#.#####.#####.###.#.#"),
		TEXT("#.....#...2...#...#L#"),
		TEXT("#.###.#.#####.#.###.#"),
		TEXT("#.#F#...#.F.#...#...#"),
		TEXT("#.#.#####.L.#####.#.#"),
		TEXT("#L................#.#"),
		TEXT("#.#######.#######.#.#"),
		TEXT("#F..3...#.M..4..#..E#"),
		TEXT("#####################"),
	};
	// MAPS-END

	FHGTheme ThemeFor(EHGVariant V)
	{
		FHGTheme T;
		switch (V)
		{
		case EHGVariant::Office:
			T.Wall = FLinearColor(0.58f, 0.50f, 0.20f);
			T.Floor = FLinearColor(0.24f, 0.19f, 0.09f);
			T.Ceiling = FLinearColor(0.55f, 0.53f, 0.40f);
			T.Prop = FLinearColor(0.35f, 0.33f, 0.30f);
			T.Lamp = FLinearColor(1.0f, 0.90f, 0.55f);
			T.Fog = FLinearColor(0.10f, 0.085f, 0.03f);
			T.LampIntensity = 4200.f;
			T.FogDensity = 0.012f;
			T.FlickerChance = 0.15f;
			break;
		case EHGVariant::Mine:
			T.Wall = FLinearColor(0.16f, 0.115f, 0.085f);
			T.Floor = FLinearColor(0.10f, 0.08f, 0.065f);
			T.Ceiling = FLinearColor(0.09f, 0.07f, 0.06f);
			T.Prop = FLinearColor(0.30f, 0.20f, 0.10f);
			T.Lamp = FLinearColor(1.0f, 0.52f, 0.18f);
			T.Fog = FLinearColor(0.035f, 0.025f, 0.015f);
			T.LampIntensity = 3400.f;
			T.FogDensity = 0.03f;
			T.FlickerChance = 0.5f;
			break;
		default:
			T.Wall = FLinearColor(0.50f, 0.58f, 0.55f);
			T.Floor = FLinearColor(0.20f, 0.22f, 0.22f);
			T.Ceiling = FLinearColor(0.46f, 0.47f, 0.45f);
			T.Prop = FLinearColor(0.75f, 0.77f, 0.75f);
			T.Lamp = FLinearColor(0.75f, 1.0f, 0.88f);
			T.Fog = FLinearColor(0.03f, 0.055f, 0.05f);
			T.LampIntensity = 3600.f;
			T.FogDensity = 0.018f;
			T.FlickerChance = 0.35f;
			break;
		}
		return T;
	}

	UInstancedStaticMeshComponent* MakeTiles(AActor* Owner, USceneComponent* Parent, const FLinearColor& Color, bool bCollide, bool bFloor)
	{
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(Owner);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(HG::Shape(TEXT("Cube")));
		Comp->SetupAttachment(Parent);
		Comp->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Comp->SetCollisionResponseToAllChannels(ECR_Block);
		Comp->RegisterComponent();
		Owner->AddInstanceComponent(Comp);
		// The building wears scanned surfaces: brick and plaster on walls, concrete on floors and ceilings.
		if (UMaterialInstanceDynamic* Material = HG::Paint(Comp, Color))
		{
			Material->SetScalarParameterValue(TEXT("Textured"), 1.f);
			Material->SetScalarParameterValue(TEXT("Floor"), bFloor ? 1.f : 0.f);
		}
		return Comp;
	}
}

AHGLevel::AHGLevel()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void AHGLevel::Build(EHGVariant InVariant)
{
	Variant = InVariant;
	Theme = ThemeFor(Variant);

	const TCHAR* const* Source = ClinicMap;
	int32 Count = UE_ARRAY_COUNT(ClinicMap);
	if (Variant == EHGVariant::Office) { Source = OfficeMap; Count = UE_ARRAY_COUNT(OfficeMap); }
	if (Variant == EHGVariant::Mine) { Source = MineMap; Count = UE_ARRAY_COUNT(MineMap); }

	Rows.Reset();
	for (int32 i = 0; i < Count; ++i)
	{
		Rows.Add(Source[i]);
	}
	Height = Rows.Num();
	Width = Rows[0].Len();

	Walls = MakeTiles(this, Root, Theme.Wall, true, false);
	Floors = MakeTiles(this, Root, Theme.Floor, true, true);
	Ceilings = MakeTiles(this, Root, Theme.Ceiling, true, true);

	const FVector TileScale(Tile / 100.f, Tile / 100.f, 0.2f);
	const FVector WallScale(Tile / 100.f, Tile / 100.f, WallHeight / 100.f);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const FIntPoint T(X, Y);
			const TCHAR C = Rows[Y][X];
			if (C == TEXT('#'))
			{
				Walls->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(T, WallHeight * 0.5f), WallScale));
				continue;
			}

			Floors->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(T, -10.f), TileScale));
			Ceilings->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(T, WallHeight + 10.f), TileScale));

			switch (C)
			{
			case TEXT('P'): PlayerTile = T; break;
			case TEXT('M'): MonsterTile = T; break;
			case TEXT('E'): ExitTile = T; AddExit(T); break;
			case TEXT('F'): FuseTiles.Add(T); break;
			case TEXT('1'): ItemTiles.Emplace(T, EHGItem::Pistol); break;
			case TEXT('2'): ItemTiles.Emplace(T, EHGItem::Shotgun); break;
			case TEXT('3'): ItemTiles.Emplace(T, EHGItem::Chicken); break;
			case TEXT('4'): ItemTiles.Emplace(T, EHGItem::Grenade); break;
			case TEXT('L'): AddLamp(T); break;
			case TEXT('B'): AddProp(T); break;
			default: break;
			}
		}
	}

	UE_LOG(LogHG, Log, TEXT("Built %s: %dx%d, %d fuses, %d items, %d lamps"),
		HG::VariantKey(Variant), Width, Height, FuseTiles.Num(), ItemTiles.Num(), Lamps.Num());
}

void AHGLevel::AddLamp(FIntPoint T)
{
	UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
	Lamp->SetMobility(EComponentMobility::Movable);
	Lamp->SetupAttachment(Root);
	Lamp->SetRelativeLocation(ToWorld(T, WallHeight - 45.f));
	Lamp->SetIntensityUnits(ELightUnits::Unitless);
	Lamp->SetIntensity(Theme.LampIntensity);
	Lamp->SetLightColor(Theme.Lamp);
	Lamp->SetAttenuationRadius(Tile * 2.6f);
	Lamp->SetSourceRadius(20.f);
	Lamp->SetCastShadows(true);
	Lamp->RegisterComponent();
	AddInstanceComponent(Lamp);
	Lamps.Add(Lamp);
	LampFlickers.Add(FMath::FRand() < Theme.FlickerChance);
	LampHoldUntil.Add(0.f);

	// The fitting: a flat panel under the ceiling.
	HG::AddShape(this, Root, TEXT("Cube"), ToWorld(T, WallHeight - 6.f), FVector(1.2f, 0.4f, 0.06f), Theme.Lamp, 8.f);
}

void AHGLevel::AddProp(FIntPoint T)
{
	const FVector Base = ToWorld(T);
	if (Variant == EHGVariant::Clinic)
	{
		// Hospital bed with a pillow and a rail.
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 0, 45), FVector(2.1f, 0.95f, 0.18f), Theme.Prop, 0.f, true);
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 0, 20), FVector(1.9f, 0.8f, 0.4f), FLinearColor(0.2f, 0.22f, 0.22f), 0.f, true);
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(-75, 0, 62), FVector(0.45f, 0.7f, 0.12f), FLinearColor(0.9f, 0.9f, 0.86f));
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(-105, 0, 70), FVector(0.06f, 0.95f, 0.7f), FLinearColor(0.6f, 0.62f, 0.62f), 0.f, true);
	}
	else if (Variant == EHGVariant::Office)
	{
		// Desk with a monitor, and a cubicle screen behind it.
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 0, 72), FVector(2.4f, 1.3f, 0.08f), FLinearColor(0.42f, 0.30f, 0.16f), 0.f, true);
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 0, 36), FVector(2.2f, 1.1f, 0.7f), Theme.Prop, 0.f, true);
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 20, 108), FVector(0.7f, 0.08f, 0.45f), FLinearColor(0.02f, 0.02f, 0.03f));
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 15, 108), FVector(0.62f, 0.02f, 0.38f), FLinearColor(0.2f, 0.5f, 0.9f), 3.f);
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 80, 80), FVector(3.6f, 0.08f, 1.6f), FLinearColor(0.30f, 0.33f, 0.36f), 0.f, true);
	}
	else
	{
		// Stacked crates.
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(-40, -30, 55), FVector(1.1f, 1.1f, 1.1f), Theme.Prop, 0.f, true);
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(70, 50, 40), FVector(0.8f, 0.8f, 0.8f), Theme.Prop * 0.8f, 0.f, true);
		HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(-30, -20, 140), FVector(0.7f, 0.7f, 0.6f), Theme.Prop * 1.2f, 0.f, true,
			FRotator(0.f, 25.f, 0.f));
	}
}

void AHGLevel::AddExit(FIntPoint T)
{
	// Put the shutter on whichever neighbouring wall the exit tile has.
	static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) };
	FIntPoint Dir = Dirs[0];
	for (const FIntPoint& D : Dirs)
	{
		if (IsSolid(T + D)) { Dir = D; break; }
	}
	const FVector Base = ToWorld(T) + FVector(Dir.X, Dir.Y, 0.f) * (Tile * 0.5f - 8.f);
	const FRotator Facing(0.f, Dir.X != 0 ? 0.f : 90.f, 0.f);

	HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 0, 130), FVector(0.1f, 2.2f, 2.6f), FLinearColor(0.25f, 0.25f, 0.27f), 0.f, false, Facing);
	ExitSign = HG::AddShape(this, Root, TEXT("Cube"), Base + FVector(0, 0, 285) - FVector(Dir.X, Dir.Y, 0.f) * 6.f,
		FVector(0.08f, 1.1f, 0.3f), FLinearColor(1.f, 0.05f, 0.03f), 20.f, false, Facing);

	ExitLamp = NewObject<UPointLightComponent>(this);
	ExitLamp->SetMobility(EComponentMobility::Movable);
	ExitLamp->SetupAttachment(Root);
	ExitLamp->SetRelativeLocation(ToWorld(T, 250.f));
	ExitLamp->SetIntensityUnits(ELightUnits::Unitless);
	ExitLamp->SetIntensity(1200.f);
	ExitLamp->SetLightColor(FLinearColor(1.f, 0.05f, 0.03f));
	ExitLamp->SetAttenuationRadius(Tile * 1.6f);
	ExitLamp->SetCastShadows(false);
	ExitLamp->RegisterComponent();
	AddInstanceComponent(ExitLamp);
}

void AHGLevel::SetExitOpen(bool bOpen)
{
	const FLinearColor Color = bOpen ? FLinearColor(0.05f, 1.f, 0.15f) : FLinearColor(1.f, 0.05f, 0.03f);
	if (ExitLamp)
	{
		ExitLamp->SetLightColor(Color);
		ExitLamp->SetIntensity(bOpen ? 2600.f : 1200.f);
	}
	if (ExitSign)
	{
		HG::Paint(ExitSign, Color, 20.f);
	}
}

void AHGLevel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Some lamps are on their way out: they drop to almost nothing for a moment, at random.
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = 0; i < Lamps.Num(); ++i)
	{
		if (!LampFlickers[i] || Now < LampHoldUntil[i])
		{
			continue;
		}
		const bool bDip = FMath::FRand() < 0.22f;
		Lamps[i]->SetIntensity(Theme.LampIntensity * (bDip ? FMath::FRandRange(0.02f, 0.25f) : 1.f));
		LampHoldUntil[i] = Now + (bDip ? FMath::FRandRange(0.04f, 0.18f) : FMath::FRandRange(0.1f, 2.4f));
	}
}

bool AHGLevel::IsSolid(FIntPoint T) const
{
	if (T.X < 0 || T.Y < 0 || T.X >= Width || T.Y >= Height)
	{
		return true;
	}
	return Rows[T.Y][T.X] == TEXT('#');
}

bool AHGLevel::IsOpen(FIntPoint T) const
{
	return !IsSolid(T) && Rows[T.Y][T.X] != TEXT('B');
}

FIntPoint AHGLevel::ToTile(const FVector& World) const
{
	return FIntPoint(FMath::RoundToInt32(World.X / Tile), FMath::RoundToInt32(World.Y / Tile));
}

FVector AHGLevel::ToWorld(FIntPoint T, float Z) const
{
	return FVector(T.X * Tile, T.Y * Tile, Z);
}

bool AHGLevel::FindPath(FIntPoint From, FIntPoint To, TArray<FIntPoint>& OutPath) const
{
	OutPath.Reset();
	if (!IsOpen(From) || !IsOpen(To))
	{
		return false;
	}
	if (From == To)
	{
		return true;
	}

	static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	const int32 Start = From.Y * Width + From.X;
	const int32 Goal = To.Y * Width + To.X;

	TArray<int32> Prev;
	Prev.Init(-2, Width * Height);
	TArray<int32> Queue;
	Queue.Reserve(Width * Height);
	Prev[Start] = -1;
	Queue.Add(Start);

	for (int32 Head = 0; Head < Queue.Num() && Prev[Goal] == -2; ++Head)
	{
		const int32 Cur = Queue[Head];
		const FIntPoint P(Cur % Width, Cur / Width);
		for (const FIntPoint& D : Dirs)
		{
			const FIntPoint N = P + D;
			if (!IsOpen(N))
			{
				continue;
			}
			const int32 Index = N.Y * Width + N.X;
			if (Prev[Index] == -2)
			{
				Prev[Index] = Cur;
				Queue.Add(Index);
			}
		}
	}

	if (Prev[Goal] == -2)
	{
		return false;
	}
	for (int32 Cur = Goal; Cur != Start; Cur = Prev[Cur])
	{
		OutPath.Insert(FIntPoint(Cur % Width, Cur / Width), 0);
	}
	return true;
}

bool AHGLevel::HasLineOfSight(const FVector& A, const FVector& B) const
{
	if (bWorld)
	{
		// A ready-made map: ask the physics scene instead of the text grid.
		FHitResult Hit;
		const FCollisionQueryParams Params(FName(TEXT("HGSight")), false);
		return !GetWorld()->LineTraceSingleByObjectType(Hit, A + FVector(0, 0, 20), B + FVector(0, 0, 20),
			FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllStaticObjects), Params);
	}
	const float Distance = FVector::Dist2D(A, B);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Distance / 40.f));
	for (int32 i = 0; i <= Steps; ++i)
	{
		const FVector P = FMath::Lerp(A, B, static_cast<float>(i) / Steps);
		if (IsSolid(ToTile(P)))
		{
			return false;
		}
	}
	return true;
}

FIntPoint AHGLevel::RandomOpenTile() const
{
	for (int32 Try = 0; Try < 200; ++Try)
	{
		const FIntPoint T(FMath::RandRange(1, Width - 2), FMath::RandRange(1, Height - 2));
		if (IsOpen(T))
		{
			return T;
		}
	}
	return PlayerTile;
}

FIntPoint AHGLevel::NearestOpenTile(FIntPoint T) const
{
	if (IsOpen(T))
	{
		return T;
	}
	for (int32 Radius = 1; Radius < FMath::Max(Width, Height); ++Radius)
	{
		for (int32 DY = -Radius; DY <= Radius; ++DY)
		{
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				const FIntPoint N(T.X + DX, T.Y + DY);
				if (IsOpen(N))
				{
					return N;
				}
			}
		}
	}
	return PlayerTile;
}

float AHGLevel::WallDistance(const FVector& From, const FVector& Direction, float MaxDistance) const
{
	if (bWorld)
	{
		return -1.f;
	}
	float Open = 0.f;
	for (float D = 20.f; D <= MaxDistance; D += 20.f)
	{
		if (!IsOpen(ToTile(From + Direction * D)))
		{
			// Somewhere in the last step. Halve it until the answer is within a couple of units.
			float Solid = D;
			for (int32 i = 0; i < 5; ++i)
			{
				const float Mid = (Open + Solid) * 0.5f;
				if (IsOpen(ToTile(From + Direction * Mid))) { Open = Mid; }
				else { Solid = Mid; }
			}
			return Open;
		}
		Open = D;
	}
	return -1.f;
}

// ---------------------------------------------------------------------------------------------
// Ready-made maps

namespace
{
	// Half-metre steps: a full metre strides straight past a doorway that is not lined up with the grid.
	const float WalkStep = 50.f;
	const float WalkRange = 9000.f;
	const int32 WalkLimit = 60000;
	const FIntPoint WalkDirs[4] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) };

	FCollisionObjectQueryParams SolidThings()
	{
		return FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllStaticObjects);
	}
}

bool AHGLevel::BodyFits(const FVector& A, const FVector& B) const
{
	// Chest and head height: something person-sized can get from A to B.
	FHitResult Hit;
	const FCollisionQueryParams Params(FName(TEXT("HGWalk")), false);
	const FCollisionShape Body = FCollisionShape::MakeSphere(21.f);
	return !GetWorld()->SweepSingleByObjectType(Hit, A + FVector(0, 0, 100), B + FVector(0, 0, 100), FQuat::Identity, SolidThings(), Body, Params)
		&& !GetWorld()->SweepSingleByObjectType(Hit, A + FVector(0, 0, 165), B + FVector(0, 0, 165), FQuat::Identity, SolidThings(), Body, Params);
}

void AHGLevel::BuildFromWorld(const FVector& Start)
{
	bWorld = true;
	Variant = EHGVariant::Hospital;
	UWorld* World = GetWorld();
	const FCollisionQueryParams Params(FName(TEXT("HGFloor")), false);

	auto FloorUnder = [World, &Params](const FVector& At, float Up, float Down, FVector& Out)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, Up), At - FVector(0, 0, Down), SolidThings(), Params))
		{
			return false;
		}
		if (Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.7f)
		{
			return false;
		}
		Out = Hit.ImpactPoint;
		return true;
	};

	FVector First = Start;
	if (!FloorUnder(Start, 60.f, 600.f, First))
	{
		First = Start - FVector(0, 0, 90);
	}

	TArray<FIntPoint> Cells;
	TMap<FIntVector, int32> Index;
	auto KeyOf = [](FIntPoint Cell, float Z) { return FIntVector(Cell.X, Cell.Y, FMath::FloorToInt32(Z / 150.f)); };
	const FIntPoint Origin(FMath::RoundToInt32(First.X / WalkStep), FMath::RoundToInt32(First.Y / WalkStep));
	Spots.Add(First);
	Cells.Add(Origin);
	Index.Add(KeyOf(Origin, First.Z), 0);

	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		for (int32 d = 0; d < 4; ++d)
		{
			int32 Linked = INDEX_NONE;
			const FIntPoint Cell = Cells[i] + WalkDirs[d];
			const FVector Probe(Cell.X * WalkStep, Cell.Y * WalkStep, Spots[i].Z);
			FVector Floor;
			// A step up or down of well under a metre: enough for stairs and ramps, not for ledges.
			if (FVector::Dist2D(Probe, First) <= WalkRange && FloorUnder(Probe, 75.f, 95.f, Floor) && BodyFits(Spots[i], Floor))
			{
				const FIntVector Key = KeyOf(Cell, Floor.Z);
				if (const int32* Known = Index.Find(Key))
				{
					Linked = *Known;
				}
				else if (Spots.Num() < WalkLimit)
				{
					Linked = Spots.Add(Floor);
					Cells.Add(Cell);
					Index.Add(Key, Linked);
				}
			}
			Links.Add(Linked);
		}
	}
	// Spots added after the loop passed them have no links yet; the loop above covers all, since it runs to the end.
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		Columns.FindOrAdd(Cells[i]).Add(i);
	}

	// How many steps each place is from the start.
	TArray<int32> Steps;
	Steps.Init(-1, Spots.Num());
	TArray<int32> Queue;
	Queue.Add(0);
	Steps[0] = 0;
	for (int32 q = 0; q < Queue.Num(); ++q)
	{
		const int32 At = Queue[q];
		for (int32 d = 0; d < 4; ++d)
		{
			const int32 To = Links[At * 4 + d];
			if (To != INDEX_NONE && Steps[To] < 0)
			{
				Steps[To] = Steps[At] + 1;
				Queue.Add(To);
			}
		}
	}

	StartSpot = Spots[0];
	ExitSpot = Spots[0];
	// The monster starts as far away as the building allows: a long walk away, and of those places,
	// the one furthest in a straight line, so it is never just the other side of a wall.
	int32 Farthest = 0;
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		if (Steps[i] > Steps[Farthest]) { Farthest = i; }
	}
	int32 MonsterAt = Farthest;
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		if (Steps[i] * 10 >= Steps[Farthest] * 6 && FVector::Dist(Spots[i], Spots[0]) > FVector::Dist(Spots[MonsterAt], Spots[0]))
		{
			MonsterAt = i;
		}
	}
	MonsterSpot = Spots[MonsterAt];

	// Fuses: spread out. Each new one goes where it is furthest from the start and from the fuses already placed.
	TArray<FVector> Taken;
	Taken.Add(StartSpot);
	for (int32 n = 0; n < 5; ++n)
	{
		int32 Best = INDEX_NONE;
		float BestScore = -1.f;
		for (int32 i = 0; i < Spots.Num(); i += 3)
		{
			if (Steps[i] < 16) { continue; }
			float Nearest = TNumericLimits<float>::Max();
			for (const FVector& T : Taken) { Nearest = FMath::Min(Nearest, FVector::Dist(Spots[i], T)); }
			const float Score = Nearest * FMath::FRandRange(0.8f, 1.f);
			if (Score > BestScore) { BestScore = Score; Best = i; }
		}
		if (Best == INDEX_NONE) { break; }
		FuseSpots.Add(Spots[Best]);
		Taken.Add(Spots[Best]);
	}
	// Weapons: somewhere on the way in.
	const EHGItem Items[] = { EHGItem::Pistol, EHGItem::Shotgun, EHGItem::Chicken, EHGItem::Grenade };
	for (const EHGItem Item : Items)
	{
		for (int32 Try = 0; Try < 200 && Spots.Num() > 1; ++Try)
		{
			const int32 i = FMath::RandRange(1, Spots.Num() - 1);
			if (Steps[i] >= 8 && Steps[i] <= 90)
			{
				ItemSpots.Emplace(Spots[i], Item);
				break;
			}
		}
	}

	// The exit is the way you came in. Its lamp turns green once the fuses are in.
	ExitLamp = NewObject<UPointLightComponent>(this);
	ExitLamp->SetMobility(EComponentMobility::Movable);
	ExitLamp->SetupAttachment(Root);
	ExitLamp->SetWorldLocation(ExitSpot + FVector(0, 0, 210));
	ExitLamp->SetIntensityUnits(ELightUnits::Unitless);
	ExitLamp->SetAttenuationRadius(700.f);
	ExitLamp->SetCastShadows(false);
	ExitLamp->RegisterComponent();
	AddInstanceComponent(ExitLamp);
	SetExitOpen(false);

	UE_LOG(LogHG, Log, TEXT("HGTEST WORLD spots=%d reachable=%d fuses=%d items=%d farthest=%d steps"), Spots.Num(), Queue.Num(), FuseSpots.Num(), ItemSpots.Num(), Steps[Farthest]);
}

int32 AHGLevel::NearestSpot(const FVector& At) const
{
	const FIntPoint Cell(FMath::RoundToInt32(At.X / WalkStep), FMath::RoundToInt32(At.Y / WalkStep));
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Ring = 0; Ring <= 8 && Best == INDEX_NONE; ++Ring)
	{
		for (int32 dx = -Ring; dx <= Ring; ++dx)
		{
			for (int32 dy = -Ring; dy <= Ring; ++dy)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != Ring) { continue; }
				if (const TArray<int32>* Here = Columns.Find(Cell + FIntPoint(dx, dy)))
				{
					for (const int32 i : *Here)
					{
						// Being on the right storey matters more than being close on the plan.
						const float Score = FVector::Dist2D(Spots[i], At) + FMath::Abs(Spots[i].Z - At.Z) * 3.f;
						if (Score < BestScore) { BestScore = Score; Best = i; }
					}
				}
			}
		}
	}
	return Best;
}

bool AHGLevel::FindWorldPath(const FVector& From, const FVector& To, TArray<FVector>& OutPath) const
{
	OutPath.Reset();
	// The player's position is the middle of their body; the floor is what we walk on.
	const int32 A = NearestSpot(From);
	const int32 B = NearestSpot(To - FVector(0, 0, 0));
	if (A == INDEX_NONE || B == INDEX_NONE)
	{
		return false;
	}
	TArray<int32> Parent;
	Parent.Init(-2, Spots.Num());
	TArray<int32> Queue;
	Queue.Add(A);
	Parent[A] = -1;
	for (int32 q = 0; q < Queue.Num() && Parent[B] == -2; ++q)
	{
		const int32 At = Queue[q];
		for (int32 d = 0; d < 4; ++d)
		{
			const int32 Next = Links[At * 4 + d];
			if (Next != INDEX_NONE && Parent[Next] == -2)
			{
				Parent[Next] = At;
				Queue.Add(Next);
			}
		}
	}
	if (Parent[B] == -2)
	{
		return false;
	}
	TArray<int32> Chain;
	for (int32 At = B; At != A; At = Parent[At])
	{
		Chain.Add(At);
	}
	Algo::Reverse(Chain);

	// Straighten the staircase of grid steps: skip ahead while a body still fits on level ground.
	FVector Anchor = Spots[A];
	for (int32 i = 0; i < Chain.Num(); )
	{
		int32 Reach = i;
		for (int32 j = FMath::Min(i + 14, Chain.Num() - 1); j > i; --j)
		{
			if (FMath::Abs(Spots[Chain[j]].Z - Anchor.Z) < 12.f && BodyFits(Anchor, Spots[Chain[j]]))
			{
				Reach = j;
				break;
			}
		}
		Anchor = Spots[Chain[Reach]];
		OutPath.Add(Anchor);
		i = Reach + 1;
	}
	return true;
}

FVector AHGLevel::RandomSpot() const
{
	return Spots.Num() > 0 ? Spots[FMath::RandRange(0, Spots.Num() - 1)] : FVector::ZeroVector;
}

bool AHGLevel::FindStandableSpot(FVector& OutSpot) const
{
	// The middle of everything the map is built from, then outwards in rings until there is floor
	// underfoot and room to stand.
	FBox Bounds(ForceInit);
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		Bounds += It->GetActorLocation();
	}
	if (!Bounds.IsValid)
	{
		return false;
	}
	const FVector Centre = Bounds.GetCenter();
	const FCollisionQueryParams Params(FName(TEXT("HGStart")), false);
	for (int32 Ring = 0; Ring <= 12; ++Ring)
	{
		for (int32 dx = -Ring; dx <= Ring; ++dx)
		{
			for (int32 dy = -Ring; dy <= Ring; ++dy)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != Ring) { continue; }
				const FVector At(Centre.X + dx * 150.f, Centre.Y + dy * 150.f, Bounds.Min.Z);
				FHitResult Hit;
				if (!GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 220), At - FVector(0, 0, 150), SolidThings(), Params)
					|| Hit.bStartPenetrating || Hit.ImpactNormal.Z < 0.7f)
				{
					continue;
				}
				// Room for a whole person, and for them to take a step in each direction.
				const FVector Floor = Hit.ImpactPoint;
				if (BodyFits(Floor + FVector(60, 0, 0), Floor - FVector(60, 0, 0)) && BodyFits(Floor + FVector(0, 60, 0), Floor - FVector(0, 60, 0)))
				{
					OutSpot = Floor;
					return true;
				}
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// The ward room

namespace
{
	/** Fresh paint: a flat colour with no grime on it. */
	UStaticMeshComponent* Painted(AActor* Owner, USceneComponent* Parent, const TCHAR* Shape, const FVector& At, const FVector& Scale,
		const FLinearColor& Color, bool bCollide = false, float Glow = 0.f, const FRotator& Turn = FRotator::ZeroRotator)
	{
		UStaticMeshComponent* Comp = HG::AddShape(Owner, Parent, Shape, At, Scale, Color, Glow, bCollide, Turn);
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0)))
		{
			Material->SetScalarParameterValue(TEXT("Clean"), 1.f);
			Material->SetScalarParameterValue(TEXT("Roughness"), 0.6f);
		}
		return Comp;
	}

	UPointLightComponent* Lamp(AActor* Owner, USceneComponent* Parent, const FVector& At, const FLinearColor& Color, float Intensity, float Radius, bool bShadows)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Parent);
		Light->SetRelativeLocation(At);
		Light->SetIntensityUnits(ELightUnits::Unitless);
		Light->SetIntensity(Intensity);
		Light->SetLightColor(Color);
		Light->SetAttenuationRadius(Radius);
		Light->SetCastShadows(bShadows);
		Light->RegisterComponent();
		Owner->AddInstanceComponent(Light);
		return Light;
	}
}

void AHGLevel::BuildWard()
{
	Variant = EHGVariant::Ward;
	// The room is 5.2 m by 6.4 m and 3 m high, centred on the origin. The window is in the -X wall, the door in the +X wall.
	const float X = 260.f, Y = 320.f, H = 300.f, T = 20.f;
	const float WinHalf = 90.f, WinLow = 100.f, WinHigh = 230.f;
	const FLinearColor Sunny(0.95f, 0.62f, 0.08f);
	const FLinearColor Sky(0.03f, 0.30f, 0.75f);
	const FLinearColor White(0.85f, 0.84f, 0.80f);
	const FLinearColor Lino(0.36f, 0.17f, 0.07f);

	Painted(this, Root, TEXT("Cube"), FVector(0, 0, -10), FVector((2 * X + 40) / 100.f, (2 * Y + 40) / 100.f, 0.2f), Lino, true);
	Painted(this, Root, TEXT("Cube"), FVector(0, 0, H + 10), FVector((2 * X + 40) / 100.f, (2 * Y + 40) / 100.f, 0.2f), White, true);

	// A wall is yellow above and blue below, with a white rail between: the two-tone paint of every such ward, in happier colours.
	auto Wall = [&](const FVector& Centre, const FVector& Size, const FVector& Inward)
	{
		Painted(this, Root, TEXT("Cube"), Centre, Size / 100.f, Sunny, true);
		if (Centre.Z - Size.Z * 0.5f < 1.f)
		{
			const float Band = FMath::Min(Size.Z, 118.f);
			const FVector Skin(FMath::Abs(Inward.X) > 0.5f ? 2.f : Size.X, FMath::Abs(Inward.Y) > 0.5f ? 2.f : Size.Y, Band);
			const FVector Face = Centre + Inward * (T * 0.5f + 1.f);
			Painted(this, Root, TEXT("Cube"), FVector(Face.X, Face.Y, Band * 0.5f), Skin / 100.f, Sky);
			Painted(this, Root, TEXT("Cube"), FVector(Face.X, Face.Y, Band + 3.f) + Inward, FVector(Skin.X, Skin.Y, 6.f) / 100.f, White);
		}
	};
	// +Y and -Y walls.
	Wall(FVector(0, Y + T * 0.5f, H * 0.5f), FVector(2 * X + 2 * T, T, H), FVector(0, -1, 0));
	Wall(FVector(0, -Y - T * 0.5f, H * 0.5f), FVector(2 * X + 2 * T, T, H), FVector(0, 1, 0));
	// +X wall in three pieces round the doorway (0.92 m wide, 2.08 m high, from y = 104 to 196).
	const float DoorLow = 104.f, DoorHigh = 196.f, DoorTop = 208.f;
	Wall(FVector(X + T * 0.5f, (-Y + DoorLow) * 0.5f, H * 0.5f), FVector(T, DoorLow + Y, H), FVector(-1, 0, 0));
	Wall(FVector(X + T * 0.5f, (Y + DoorHigh) * 0.5f, H * 0.5f), FVector(T, Y - DoorHigh, H), FVector(-1, 0, 0));
	Painted(this, Root, TEXT("Cube"), FVector(X + T * 0.5f, (DoorLow + DoorHigh) * 0.5f, (H + DoorTop) * 0.5f), FVector(T, DoorHigh - DoorLow, H - DoorTop) / 100.f, Sunny, true);
	// The door hangs on a hinge at its low-y edge and opens outwards, into the hall.
	WardDoor = HG::AddPivot(this, Root, FVector(X + 4.f, DoorLow, 0));
	Painted(this, WardDoor, TEXT("Cube"), FVector(0, 46, 104), FVector(0.06f, 0.92f, 2.08f), White, true);
	Painted(this, WardDoor, TEXT("Cube"), FVector(-4, 46, 108), FVector(0.03f, 0.78f, 1.92f), FLinearColor(0.72f, 0.80f, 0.86f));
	Painted(this, WardDoor, TEXT("Sphere"), FVector(-9, 80, 100), FVector(0.07f), FLinearColor(0.75f, 0.6f, 0.2f));

	// The hall outside: the same building with the colour taken out. Brick and stained plaster, bare concrete,
	// a cold light a long way down. It runs left and right of the door.
	{
		const float HallNear = X + T, HallFar = X + T + 300.f, HallLen = 2600.f;
		auto Drab = [this](const FVector& At, const FVector& Size, const FLinearColor& Tint, bool bFloor)
		{
			UStaticMeshComponent* Comp = HG::AddShape(this, Root, TEXT("Cube"), At, Size / 100.f, Tint, 0.f, true);
			if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0)))
			{
				Material->SetScalarParameterValue(TEXT("Textured"), 1.f);
				Material->SetScalarParameterValue(TEXT("Floor"), bFloor ? 1.f : 0.f);
			}
		};
		const FLinearColor Cold(0.30f, 0.36f, 0.40f);
		Drab(FVector((HallNear + HallFar) * 0.5f, 150, -10), FVector(HallFar - HallNear, HallLen, 20), FLinearColor(0.16f, 0.18f, 0.2f), true);
		Drab(FVector((HallNear + HallFar) * 0.5f, 150, H + 10), FVector(HallFar - HallNear, HallLen, 20), FLinearColor(0.2f, 0.22f, 0.24f), true);
		for (int32 i = 0; i < 6; ++i)
		{
			// The far wall, in panels so the brick is not stretched.
			Drab(FVector(HallFar + T * 0.5f, 150.f - HallLen * 0.5f + 216.f + 433.f * i, H * 0.5f), FVector(T, 433, H), Cold, false);
		}
		// Closed at the far left end. To the right it runs on into the corridor of scene three.
		Drab(FVector((HallNear + HallFar) * 0.5f, 150.f - HallLen * 0.5f - T * 0.5f, H * 0.5f), FVector(HallFar - HallNear, T, H), Cold, false);
		// The near wall. Past the ends of the ward it is the hall's own; along the ward it is a skin over
		// the back of the ward's wall, so no yellow shows out here. The doorway is left open.
		for (int32 i = 0; i < 3; ++i)
		{
			Drab(FVector(HallNear - T * 0.5f, Y + T + 185.f + 370.f * i, H * 0.5f), FVector(T, 370, H), Cold, false);
		}
		for (int32 i = 0; i < 2; ++i)
		{
			Drab(FVector(HallNear - T * 0.5f, -Y - T - 202.5f - 405.f * i, H * 0.5f), FVector(T, 405, H), Cold, false);
		}
		Drab(FVector(HallNear + 1.f, (-Y - T + DoorLow) * 0.5f, H * 0.5f), FVector(2, DoorLow + Y + T, H), Cold, false);
		Drab(FVector(HallNear + 1.f, (Y + T + DoorHigh) * 0.5f, H * 0.5f), FVector(2, Y + T - DoorHigh, H), Cold, false);
		Drab(FVector(HallNear + 1.f, (DoorLow + DoorHigh) * 0.5f, (H + DoorTop) * 0.5f), FVector(2, DoorHigh - DoorLow, H - DoorTop), Cold, false);
		// The lamps to the right of the door are kept in order, because in scene three they go out behind him one by one.
		auto HallLamp = [this, HallNear](float Along, float Ceiling, float Intensity, float Radius, bool bShadows)
		{
			HallLamps.Add(Lamp(this, Root, FVector(HallNear + 150, Along, Ceiling - 30), FLinearColor(0.55f, 0.72f, 1.f), Intensity, Radius, bShadows));
			HallLampY.Add(Along);
			HallLampFull.Add(Intensity);
		};
		HallLamp(150, H, 3200.f, 620.f, true);
		// A weak one between, so whoever walks behind him down the hall is never quite lost in the dark.
		HallLamp(150 + 450, H, 1300.f, 520.f, false);
		HallLamp(150 + 900, H, 4500.f, 700.f, false);

		// Scene three, the corridor: the same hall going on far too long. It is built in stretches, and
		// each one is a little taller than the last, with a beam across the joint to hide the step, so
		// that by the end the ceiling is out of sight and the doors are for giants. The handles climb
		// out of a child's reach. The last stretch has the operation room's door in its left wall.
		const float From = 150.f + HallLen * 0.5f, Stretch = 420.f;
		const int32 Stretches = 10;
		const FLinearColor DoorPaint(0.62f, 0.66f, 0.62f);
		for (int32 i = 0; i < Stretches; ++i)
		{
			const float Mid = From + Stretch * (i + 0.5f);
			const float High = H + 26.f * i;
			const bool bLast = i == Stretches - 1;
			Drab(FVector((HallNear + HallFar) * 0.5f, Mid, -10), FVector(HallFar - HallNear, Stretch, 20), FLinearColor(0.16f, 0.18f, 0.2f), true);
			Drab(FVector((HallNear + HallFar) * 0.5f, Mid, High + 10), FVector(HallFar - HallNear, Stretch, 20), FLinearColor(0.2f, 0.22f, 0.24f), true);
			Drab(FVector(HallNear - T * 0.5f, Mid, High * 0.5f), FVector(T, Stretch, High), Cold, false);
			// The beam over the joint with the stretch before.
			Drab(FVector((HallNear + HallFar) * 0.5f, From + Stretch * i, High - 6.f), FVector(HallFar - HallNear, 26, 64), FLinearColor(0.12f, 0.13f, 0.14f), false);
			// The higher the ceiling, the stronger the lamp and the further it has to reach to find the floor.
			HallLamp(Mid, High, (bLast ? 2600.f : 2100.f) * FMath::Pow(High / H, 1.5f), High + 320.f, false);

			const float Grow = 1.f + 0.07f * i;
			const float DoorWide = 92.f, DoorTall = 208.f * Grow;
			if (bLast)
			{
				// The left wall in three pieces round the operation room's doorway.
				const float Low = Mid - DoorWide * 0.5f, Top = Mid + DoorWide * 0.5f;
				Drab(FVector(HallFar + T * 0.5f, (Mid - Stretch * 0.5f + Low) * 0.5f, High * 0.5f), FVector(T, Low - (Mid - Stretch * 0.5f), High), Cold, false);
				Drab(FVector(HallFar + T * 0.5f, (Mid + Stretch * 0.5f + Top) * 0.5f, High * 0.5f), FVector(T, (Mid + Stretch * 0.5f) - Top, High), Cold, false);
				Drab(FVector(HallFar + T * 0.5f, Mid, (High + DoorTall) * 0.5f), FVector(T, DoorWide, High - DoorTall), Cold, false);
				OpDoorAt = FVector(HallFar + T * 0.5f, Mid, 0.f);
				// Hinged like the ward's door, on its low-y edge, and it opens away from him, into the room.
				OpDoor = HG::AddPivot(this, Root, FVector(HallFar + 4.f, Low, 0));
				Painted(this, OpDoor, TEXT("Cube"), FVector(0, DoorWide * 0.5f, DoorTall * 0.5f), FVector(0.06f, DoorWide / 100.f, DoorTall / 100.f), FLinearColor(0.85f, 0.84f, 0.80f), true);
				Painted(this, OpDoor, TEXT("Sphere"), FVector(-9, DoorWide - 12.f, 100.f * Grow), FVector(0.07f), FLinearColor(0.75f, 0.6f, 0.2f));

				// The room behind it: white, too bright, and one chair.
				const float RoomNear = HallFar + T, RoomFar = HallFar + T + 420.f, RoomHalf = 230.f, RoomHigh = 400.f;
				const FLinearColor Tiled(0.92f, 0.93f, 0.95f);
				Painted(this, Root, TEXT("Cube"), FVector((RoomNear + RoomFar) * 0.5f, Mid, -10), FVector((RoomFar - RoomNear) / 100.f, RoomHalf * 0.02f, 0.2f), Tiled, true);
				Painted(this, Root, TEXT("Cube"), FVector((RoomNear + RoomFar) * 0.5f, Mid, RoomHigh + 10), FVector((RoomFar - RoomNear) / 100.f, RoomHalf * 0.02f, 0.2f), Tiled, true);
				Painted(this, Root, TEXT("Cube"), FVector(RoomFar + T * 0.5f, Mid, RoomHigh * 0.5f), FVector(T / 100.f, RoomHalf * 0.02f, RoomHigh / 100.f), Tiled, true);
				for (const float Side : { -1.f, 1.f })
				{
					Painted(this, Root, TEXT("Cube"), FVector((RoomNear + RoomFar) * 0.5f, Mid + Side * (RoomHalf + T * 0.5f), RoomHigh * 0.5f), FVector((RoomFar - RoomNear) / 100.f, T / 100.f, RoomHigh / 100.f), Tiled, true);
				}
				// The wall the door is in is the hall's brick on its other side; in here it is tiled like the rest.
				Painted(this, Root, TEXT("Cube"), FVector(RoomNear + 1.f, (Mid - RoomHalf + Low) * 0.5f, RoomHigh * 0.5f), FVector(0.02f, (Low - (Mid - RoomHalf)) / 100.f, RoomHigh / 100.f), Tiled);
				Painted(this, Root, TEXT("Cube"), FVector(RoomNear + 1.f, (Mid + RoomHalf + Top) * 0.5f, RoomHigh * 0.5f), FVector(0.02f, ((Mid + RoomHalf) - Top) / 100.f, RoomHigh / 100.f), Tiled);
				Painted(this, Root, TEXT("Cube"), FVector(RoomNear + 1.f, Mid, (RoomHigh + DoorTall) * 0.5f), FVector(0.02f, DoorWide / 100.f, (RoomHigh - DoorTall) / 100.f), Tiled);
				OpLight = Lamp(this, Root, FVector(RoomNear + 200, Mid, RoomHigh - 40), FLinearColor(0.92f, 0.97f, 1.f), 0.f, 900.f, false);
				// The chair, facing the door, and a small table with a tray beside it.
				const FLinearColor Seat(0.42f, 0.16f, 0.05f), Steel(0.5f, 0.52f, 0.55f);
				const FVector Chair(RoomNear + 250, Mid, 0);
				ChairAt = Chair;
				Painted(this, Root, TEXT("Cube"), Chair + FVector(0, 0, 47), FVector(0.5f, 0.5f, 0.07f), Seat);
				Painted(this, Root, TEXT("Cube"), Chair + FVector(27, 0, 92), FVector(0.07f, 0.5f, 0.86f), Seat);
				for (const FVector& Leg : { FVector(-21, -21, 22), FVector(-21, 21, 22), FVector(21, -21, 22), FVector(21, 21, 22) })
				{
					Painted(this, Root, TEXT("Cube"), Chair + Leg, FVector(0.05f, 0.05f, 0.44f), Steel);
				}
				// The table stands ahead of the chair and to its left, where he can see what is laid out on it:
				// a tray under a folded cloth, and an enamel basin.
				const FVector Table = Chair + FVector(-105, 118, 0);
				Painted(this, Root, TEXT("Cube"), Table + FVector(0, 0, 70), FVector(0.42f, 0.6f, 0.03f), Steel);
				for (const FVector& Leg : { FVector(-17, -26, 35), FVector(-17, 26, 35), FVector(17, -26, 35), FVector(17, 26, 35) })
				{
					Painted(this, Root, TEXT("Cube"), Table + Leg, FVector(0.04f, 0.04f, 0.7f), Steel);
				}
				Painted(this, Root, TEXT("Cube"), Table + FVector(0, -10, 73.5f), FVector(0.28f, 0.34f, 0.02f), FLinearColor(0.9f, 0.9f, 0.88f));
				Painted(this, Root, TEXT("Cube"), Table + FVector(0, -10, 76.f), FVector(0.22f, 0.26f, 0.03f), FLinearColor(0.97f, 0.97f, 0.96f));
				Painted(this, Root, TEXT("Sphere"), Table + FVector(0, 20, 76.f), FVector(0.2f, 0.2f, 0.09f), FLinearColor(0.95f, 0.95f, 0.93f));
				// A lamp on a stand, turned on the chair.
				const FVector Stand = Chair + FVector(-70, -125, 0);
				Painted(this, Root, TEXT("Cube"), Stand + FVector(0, 0, 90), FVector(0.035f, 0.035f, 1.8f), Steel);
				Painted(this, Root, TEXT("Cube"), Stand + FVector(0, 0, 3), FVector(0.4f, 0.4f, 0.05f), Steel);
				Painted(this, Root, TEXT("Sphere"), Stand + FVector(8, 10, 184), FVector(0.3f, 0.3f, 0.16f), FLinearColor(1.f, 1.f, 0.97f), false, 5.f);
			}
			else
			{
				Drab(FVector(HallFar + T * 0.5f, Mid, High * 0.5f), FVector(T, Stretch, High), Cold, false);
				// A shut door, on the left in one stretch and on the right in the next.
				const bool bLeft = i % 2 == 0;
				const float Face = bLeft ? HallFar - 2.f : HallNear + 2.f;
				const float Out = bLeft ? -1.f : 1.f;
				const float DoorMid = Mid + (bLeft ? 60.f : -70.f);
				Painted(this, Root, TEXT("Cube"), FVector(Face, DoorMid, DoorTall * 0.5f), FVector(0.05f, DoorWide * Grow / 100.f, DoorTall / 100.f), DoorPaint);
				Painted(this, Root, TEXT("Cube"), FVector(Face + Out * 1.5f, DoorMid, DoorTall + 5.f), FVector(0.06f, (DoorWide * Grow + 16.f) / 100.f, 0.1f), FLinearColor(0.3f, 0.32f, 0.3f));
				Painted(this, Root, TEXT("Sphere"), FVector(Face + Out * 6.f, DoorMid + DoorWide * Grow * 0.36f, 100.f * (1.f + 0.11f * i)), FVector(0.07f), FLinearColor(0.75f, 0.6f, 0.2f));
			}
		}
		const float CorridorEnd = From + Stretch * Stretches;
		Drab(FVector((HallNear + HallFar) * 0.5f, CorridorEnd + T * 0.5f, 300.f), FVector(HallFar - HallNear, T, 600), Cold, false);
		Lamp(this, Root, FVector(HallNear + 150, 150 - 900, H - 30), FLinearColor(0.55f, 0.72f, 1.f), 1600.f, 620.f, false);
	}

	// Stands one of the flat wall models (see build_wall_things in make_models.py) against a wall.
	auto OnWall = [this](const TCHAR* Model, const FVector& At, const FVector& Along, float Scale, float Tilt)
	{
		// The importer mirrors a model's Y, so its "up" is -Y here. Turning both axes round keeps the
		// model upright with its front to the room.
		const FRotator Stand = FRotationMatrix::MakeFromXY(-Along, -FVector::UpVector).Rotator();
		if (UStaticMeshComponent* Comp = HG::AddModel(this, Root, Model, At, Stand, FVector(Scale)))
		{
			Comp->AddLocalRotation(FRotator(0.f, Tilt, 0.f));
			for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
			{
				if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(i)))
				{
					Material->SetScalarParameterValue(TEXT("Clean"), 1.f);
					Material->SetScalarParameterValue(TEXT("Local"), 0.f);
				}
			}
		}
	};
	// The door wall: a rainbow, the clock, stars.
	OnWall(TEXT("Deco_Rainbow"), FVector(X - 2.f, -190, 150), FVector(0, 1, 0), 1.25f, 0.f);
	OnWall(TEXT("Clock_Face"), ClockSpot(), FVector(0, 1, 0), 1.f, 0.f);
	MinuteHand = HG::AddPivot(this, Root, ClockSpot());
	MinuteHand->SetWorldRotation(FRotationMatrix::MakeFromXY(FVector(0, -1, 0), -FVector::UpVector).Rotator());
	HourHand = HG::AddPivot(this, Root, ClockSpot());
	HourHand->SetWorldRotation(MinuteHand->GetComponentRotation());
	HG::AddModel(this, MinuteHand, TEXT("Clock_Hand"));
	HG::AddModel(this, HourHand, TEXT("Clock_Hand"), FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.5f, 0.66f, 1.f));
	for (const FVector& At : { FVector(X - 2.f, 60, 255), FVector(X - 2.f, 20, 205), FVector(X - 2.f, 250, 250), FVector(X - 2.f, 285, 190) })
	{
		OnWall(TEXT("Deco_Star"), At, FVector(0, 1, 0), 1.3f, FMath::FRandRange(-25.f, 25.f));
	}
	// The window wall: red curtains, and flowers painted along the rail.
	for (const float Side : { -1.f, 1.f })
	{
		Painted(this, Root, TEXT("Cube"), FVector(-X + 8.f, Side * (WinHalf + 34.f), 168), FVector(0.05f, 0.62f, 1.6f), FLinearColor(0.75f, 0.08f, 0.1f));
	}
	Painted(this, Root, TEXT("Cube"), FVector(-X + 9.f, 0, 250), FVector(0.06f, 2.7f, 0.07f), FLinearColor(0.35f, 0.2f, 0.1f));
	const FLinearColor Petals[] = { FLinearColor(0.9f, 0.1f, 0.12f), FLinearColor(1.f, 0.45f, 0.75f), FLinearColor(0.55f, 0.2f, 0.8f), FLinearColor(1.f, 1.f, 1.f) };
	for (int32 i = 0; i < 9; ++i)
	{
		const float Along = -Y + 50.f + 67.f * i;
		if (FMath::Abs(Along) < WinHalf + 70.f) { continue; }
		const float Tall = 30.f + 14.f * (i % 3);
		Painted(this, Root, TEXT("Cube"), FVector(-X + 1.5f, Along, 124.f + Tall * 0.5f), FVector(0.012f, 0.03f, Tall / 100.f), FLinearColor(0.12f, 0.55f, 0.15f));
		Painted(this, Root, TEXT("Sphere"), FVector(-X + 2.f, Along, 126.f + Tall), FVector(0.03f, 0.2f, 0.2f), Petals[i % UE_ARRAY_COUNT(Petals)]);
		Painted(this, Root, TEXT("Sphere"), FVector(-X + 2.5f, Along, 126.f + Tall), FVector(0.03f, 0.08f, 0.08f), FLinearColor(1.f, 0.8f, 0.1f));
	}
	// The sun wall and the balloon wall get a few stars each too.
	for (const FVector& At : { FVector(60, Y - 2.f, 150), FVector(200, Y - 2.f, 165), FVector(-205, Y - 2.f, 145) })
	{
		OnWall(TEXT("Deco_Star"), At, FVector(-1, 0, 0), 1.2f, FMath::FRandRange(-25.f, 25.f));
	}
	for (const FVector& At : { FVector(215, -Y + 2.f, 150), FVector(-235, -Y + 2.f, 165) })
	{
		OnWall(TEXT("Deco_Star"), At, FVector(1, 0, 0), 1.2f, FMath::FRandRange(-25.f, 25.f));
	}
	// -X wall in four pieces around the window (1.8 m wide, from 1.0 m to 2.3 m up).
	Wall(FVector(-X - T * 0.5f, 0, WinLow * 0.5f), FVector(T, 2 * Y, WinLow), FVector(1, 0, 0));
	Wall(FVector(-X - T * 0.5f, 0, (H + WinHigh) * 0.5f), FVector(T, 2 * Y, H - WinHigh), FVector(1, 0, 0));
	Wall(FVector(-X - T * 0.5f, (Y + WinHalf) * 0.5f, (WinLow + WinHigh) * 0.5f), FVector(T, Y - WinHalf, WinHigh - WinLow), FVector(1, 0, 0));
	Wall(FVector(-X - T * 0.5f, -(Y + WinHalf) * 0.5f, (WinLow + WinHigh) * 0.5f), FVector(T, Y - WinHalf, WinHigh - WinLow), FVector(1, 0, 0));
	// Window frame, a cross bar, a sill, and the day outside.
	Painted(this, Root, TEXT("Cube"), FVector(-X - 10, 0, WinLow - 3), FVector(0.34f, 1.92f, 0.06f), White);
	Painted(this, Root, TEXT("Cube"), FVector(-X - 10, 0, (WinLow + WinHigh) * 0.5f), FVector(0.05f, 0.05f, 1.3f), White);
	Painted(this, Root, TEXT("Cube"), FVector(-X - 10, 0, WinHigh - 40), FVector(0.05f, 1.8f, 0.05f), White);
	Painted(this, Root, TEXT("Cube"), FVector(-X - 220, 0, 200), FVector(0.1f, 12.f, 8.f), FLinearColor(0.55f, 0.78f, 1.f), false, 14.f);

	// Painted on the far wall: a sun with rays. On the near wall: balloons on strings. Clouds on both.
	const float SunY = Y - 1.5f;
	Painted(this, Root, TEXT("Cylinder"), FVector(-70, SunY, 215), FVector(0.9f, 0.9f, 0.02f), FLinearColor(1.f, 0.45f, 0.05f), false, 0.f, FRotator(0, 0, 90));
	for (int32 i = 0; i < 10; ++i)
	{
		const float Angle = 36.f * i;
		const FVector Out(FMath::Cos(FMath::DegreesToRadians(Angle)), 0.f, FMath::Sin(FMath::DegreesToRadians(Angle)));
		Painted(this, Root, TEXT("Cube"), FVector(-70, SunY, 215) + Out * 68.f, FVector(0.26f, 0.015f, 0.06f), FLinearColor(1.f, 0.45f, 0.05f), false, 0.f, FRotator(Angle, 0, 0));
	}
	const FLinearColor Balloons[] = { FLinearColor(0.9f, 0.1f, 0.12f), FLinearColor(0.15f, 0.7f, 0.25f), FLinearColor(0.55f, 0.2f, 0.8f), FLinearColor(1.f, 0.45f, 0.75f) };
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector At(-150.f + 95.f * i, -Y + 2.f, 205.f + 22.f * (i % 2));
		Painted(this, Root, TEXT("Sphere"), At, FVector(0.42f, 0.03f, 0.52f), Balloons[i]);
		Painted(this, Root, TEXT("Cube"), At - FVector(0, 0, 62), FVector(0.012f, 0.012f, 0.7f), FLinearColor(0.25f, 0.25f, 0.25f));
	}
	for (const FVector& At : { FVector(120, Y - 2.f, 240), FVector(170, Y - 2.f, 228), FVector(-200, -Y + 2.f, 255), FVector(60, -Y + 2.f, 262) })
	{
		Painted(this, Root, TEXT("Sphere"), At, FVector(0.7f, 0.03f, 0.3f), White);
		Painted(this, Root, TEXT("Sphere"), At + FVector(34, 0, -8), FVector(0.5f, 0.03f, 0.24f), White);
	}
	// Paper flags on a string across the room.
	const FLinearColor Flags[] = { FLinearColor(0.9f, 0.1f, 0.12f), FLinearColor(1.f, 0.8f, 0.1f), FLinearColor(0.15f, 0.7f, 0.25f), FLinearColor(0.16f, 0.5f, 0.86f), FLinearColor(1.f, 0.45f, 0.05f) };
	for (int32 i = 0; i < 15; ++i)
	{
		const float Along = -Y + 20.f + (2 * Y - 40.f) * i / 14.f;
		const float Sag = 26.f * (1.f - FMath::Square((i - 7) / 7.f));
		Painted(this, Root, TEXT("Cone"), FVector(-40, Along, 262.f - Sag), FVector(0.24f, 0.02f, 0.28f), Flags[i % UE_ARRAY_COUNT(Flags)], false, 0.f, FRotator(180, 0, 0));
	}
	Painted(this, Root, TEXT("Cube"), FVector(-40, 0, 282), FVector(0.01f, 2 * Y / 100.f, 0.01f), White);

	// A round rug where the toys are.
	Painted(this, Root, TEXT("Cylinder"), FVector(60, -70, 0.6f), FVector(3.0f, 3.0f, 0.012f), FLinearColor(0.78f, 0.12f, 0.12f));
	Painted(this, Root, TEXT("Cylinder"), FVector(60, -70, 1.0f), FVector(2.3f, 2.3f, 0.012f), FLinearColor(0.96f, 0.82f, 0.4f));
	Painted(this, Root, TEXT("Cylinder"), FVector(60, -70, 1.4f), FVector(1.5f, 1.5f, 0.012f), FLinearColor(0.16f, 0.5f, 0.86f));

	// Daylight: a hard sun through the window that lands on the floor, soft sky fill at the window, and the ceiling lamp.
	USpotLightComponent* Sun = NewObject<USpotLightComponent>(this);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetupAttachment(Root);
	Sun->SetRelativeLocation(FVector(-X - 330, -60, 420));
	Sun->SetRelativeRotation((FVector(40, -40, 0) - FVector(-X - 330, -60, 420)).Rotation());
	Sun->SetIntensityUnits(ELightUnits::Unitless);
	Sun->SetIntensity(200000.f);
	Sun->SetLightColor(FLinearColor(1.f, 0.93f, 0.78f));
	Sun->SetAttenuationRadius(1600.f);
	Sun->SetInnerConeAngle(16.f);
	Sun->SetOuterConeAngle(24.f);
	Sun->SetCastShadows(true);
	Sun->RegisterComponent();
	AddInstanceComponent(Sun);
	Lamp(this, Root, FVector(-X + 40, 0, 190), FLinearColor(0.7f, 0.85f, 1.f), 3500.f, 900.f, false);
	Lamp(this, Root, FVector(40, 0, H - 30), FLinearColor(1.f, 0.95f, 0.85f), 4500.f, 1000.f, true);
	Painted(this, Root, TEXT("Sphere"), FVector(40, 0, H - 14), FVector(0.4f, 0.4f, 0.2f), FLinearColor(1.f, 0.97f, 0.9f), false, 6.f);

	BuildEscape();
}

void AHGLevel::BuildEscape()
{
	// The same building with nobody's colour in it: bare walls, a dark floor, a few lamps that are
	// failing. Sizes are in centimetres from the corner the first room starts at; +X is east, +Y north.
	const float H = 300.f, T = 20.f;
	const FLinearColor Cold(0.30f, 0.36f, 0.40f);
	const FLinearColor Floor(0.16f, 0.18f, 0.2f);
	const FLinearColor Roof(0.2f, 0.22f, 0.24f);
	auto Drab = [this](float X0, float Y0, float X1, float Y1, float Z0, float Z1, const FLinearColor& Tint, bool bFloor)
	{
		UStaticMeshComponent* Comp = HG::AddShape(this, Root, TEXT("Cube"), EscapeAt + FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, (Z0 + Z1) * 0.5f),
			FVector(X1 - X0, Y1 - Y0, Z1 - Z0) / 100.f, Tint, 0.f, true);
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0)))
		{
			Material->SetScalarParameterValue(TEXT("Textured"), 1.f);
			Material->SetScalarParameterValue(TEXT("Floor"), bFloor ? 1.f : 0.f);
		}
	};
	auto Wall = [&](float X0, float Y0, float X1, float Y1) { Drab(X0, Y0, X1, Y1, 0.f, H, Cold, false); };
	auto Slab = [&](float X0, float Y0, float X1, float Y1)
	{
		Drab(X0, Y0, X1, Y1, -20.f, 0.f, Floor, true);
		Drab(X0, Y0, X1, Y1, H, H + 20.f, Roof, true);
	};
	auto Light = [this](float X, float Y, float Intensity, float Radius, const FLinearColor& Colour, bool bShadows)
	{
		EscapeLamps.Add(Lamp(this, Root, EscapeAt + FVector(X, Y, 270), Colour, Intensity, Radius, bShadows));
		EscapeLampFull.Add(Intensity);
	};
	const FLinearColor Pale(0.55f, 0.72f, 1.f);

	// The room he wakes in. Its door is shut and its handle is out of reach; the way out is a gap
	// at the bottom of the north wall where a panel has come away.
	Slab(-20, -20, 520, 420);
	Wall(-20, -20, 0, 420);
	Wall(-20, -20, 520, 0);
	Wall(500, -20, 520, 420);
	Wall(-20, 400, 380, 420);
	Wall(450, 400, 520, 420);
	Drab(380, 400, 450, 420, 56.f, H, Cold, false);
	Painted(this, Root, TEXT("Cube"), EscapeAt + FVector(498, 150, 110), FVector(0.05f, 0.95f, 2.2f), FLinearColor(0.62f, 0.66f, 0.62f));
	Painted(this, Root, TEXT("Sphere"), EscapeAt + FVector(492, 186, 176), FVector(0.07f), FLinearColor(0.75f, 0.6f, 0.2f));
	Light(250, 200, 5500.f, 800.f, Pale, false);

	// The long ward: beds down both sides and an aisle between. The beds are put in by the game.
	Slab(-220, 420, 720, 1940);
	Wall(-220, 400, -20, 420);
	Wall(520, 400, 720, 420);
	Wall(-220, 400, -200, 1940);
	Wall(700, 400, 720, 1940);
	Wall(-220, 1920, 560, 1940);
	Wall(650, 1920, 720, 1940);
	Drab(560, 1920, 650, 1940, 210.f, H, Cold, false);
	Light(250, 800, 6500.f, 1000.f, Pale, false);
	Light(250, 1500, 4500.f, 900.f, Pale, false);

	// The corridor across the top, with the nurses' counter along its north wall. Under the counter
	// is a way through that no adult fits.
	Slab(-220, 1940, 1720, 2260);
	Wall(720, 1920, 1720, 1940);
	Wall(-220, 2240, 1720, 2260);
	Wall(-220, 1940, -200, 2240);
	const FLinearColor Wood(0.2f, 0.15f, 0.1f);
	Drab(900, 2158, 1300, 2164, 0.f, 100.f, Wood, false);
	Drab(896, 2158, 1304, 2240, 96.f, 104.f, Wood, false);
	Drab(1080, 2164, 1086, 2240, 60.f, 96.f, Wood, false);
	// Cupboards against the south wall, to break the length of it.
	Drab(820, 1940, 900, 1975, 0.f, 190.f, FLinearColor(0.5f, 0.52f, 0.5f), false);
	Drab(1380, 1940, 1470, 1975, 0.f, 190.f, FLinearColor(0.5f, 0.52f, 0.5f), false);
	Light(300, 2090, 4200.f, 900.f, Pale, false);
	Light(1100, 2090, 5500.f, 900.f, Pale, false);
	Light(1560, 2090, 2200.f, 700.f, Pale, false);

	// The last room: the only one with colour in it. Its wall is the corridor's east end, with the door in it.
	const FLinearColor Sunny(0.95f, 0.62f, 0.08f);
	const FLinearColor White(0.85f, 0.84f, 0.80f);
	const float DoorLow = 2040.f, DoorHigh = 2132.f, DoorTop = 208.f;
	auto Warm = [this](float X0, float Y0, float X1, float Y1, float Z0, float Z1, const FLinearColor& Tint)
	{
		Painted(this, Root, TEXT("Cube"), EscapeAt + FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, (Z0 + Z1) * 0.5f), FVector(X1 - X0, Y1 - Y0, Z1 - Z0) / 100.f, Tint, true);
	};
	Warm(1700, 1820, 1720, DoorLow, 0, H, Sunny);
	Warm(1700, DoorHigh, 1720, 2360, 0, H, Sunny);
	Warm(1700, DoorLow, 1720, DoorHigh, DoorTop, H, Sunny);
	// On the corridor's side the same wall is bare.
	Drab(1697, 1940, 1700, DoorLow, 0.f, H, Cold, false);
	Drab(1697, DoorHigh, 1700, 2240, 0.f, H, Cold, false);
	Drab(1697, DoorLow, 1700, DoorHigh, DoorTop, H, Cold, false);
	Warm(1720, 1820, 2240, 1840, 0, H, Sunny);
	Warm(1720, 2340, 2240, 2360, 0, H, Sunny);
	Warm(2220, 1840, 2240, 2340, 0, H, Sunny);
	Warm(1720, 1840, 2220, 2340, -20, 0, FLinearColor(0.36f, 0.17f, 0.07f));
	Warm(1720, 1840, 2220, 2340, H, H + 20, White);
	// A window full of daylight in the far wall, and a bench under it.
	Painted(this, Root, TEXT("Cube"), EscapeAt + FVector(2218, 2090, 170), FVector(0.03f, 1.8f, 1.3f), FLinearColor(1.f, 0.98f, 0.9f), false, 9.f);
	Painted(this, Root, TEXT("Cube"), EscapeAt + FVector(2215, 2090, 170), FVector(0.05f, 0.06f, 1.3f), White);
	Painted(this, Root, TEXT("Cube"), EscapeAt + FVector(2215, 2090, 170), FVector(0.05f, 1.8f, 0.06f), White);
	Painted(this, Root, TEXT("Cube"), EscapeAt + FVector(2180, 2090, 42), FVector(0.4f, 1.6f, 0.06f), FLinearColor(0.42f, 0.24f, 0.1f), true);
	for (const float Side : { -70.f, 70.f })
	{
		Painted(this, Root, TEXT("Cube"), EscapeAt + FVector(2180, 2090 + Side, 20), FVector(0.36f, 0.06f, 0.4f), FLinearColor(0.42f, 0.24f, 0.1f));
	}
	// The door, hinged on its low-y edge, opening into the room; and the lamp inside, which is the
	// only one here that throws shadows, so its light reaches the corridor through the doorway alone.
	MomDoor = HG::AddPivot(this, Root, EscapeAt + FVector(1704, DoorLow, 0));
	Painted(this, MomDoor, TEXT("Cube"), FVector(0, 46, 104), FVector(0.06f, 0.92f, 2.08f), White, true);
	Painted(this, MomDoor, TEXT("Sphere"), FVector(-9, 80, 100), FVector(0.07f), FLinearColor(0.75f, 0.6f, 0.2f));
	Light(1970, 2090, 5200.f, 760.f, FLinearColor(1.f, 0.93f, 0.8f), true);
	// A line of warm light under the shut door, for him to find from the far end of the corridor.
	Painted(this, Root, TEXT("Cube"), EscapeAt + FVector(1696, (DoorLow + DoorHigh) * 0.5f, 1.5f), FVector(0.05f, 0.9f, 0.03f), FLinearColor(1.f, 0.85f, 0.55f), false, 14.f);
}

void AHGLevel::SetMomDoorOpen(float Alpha)
{
	if (MomDoor)
	{
		MomDoor->SetRelativeRotation(FRotator(0.f, -100.f * FMath::Clamp(Alpha, 0.f, 1.f), 0.f));
	}
}

void AHGLevel::SetEscapeLamp(int32 Index, float Level)
{
	if (EscapeLamps.IsValidIndex(Index) && EscapeLamps[Index])
	{
		EscapeLamps[Index]->SetIntensity(EscapeLampFull[Index] * FMath::Clamp(Level, 0.f, 1.f));
	}
}

void AHGLevel::SetOpDoorOpen(float Alpha)
{
	Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
	if (OpDoor)
	{
		OpDoor->SetRelativeRotation(FRotator(0.f, -105.f * Alpha, 0.f));
	}
	if (OpLight)
	{
		// The light is let out with the door; shut, the room does not exist yet.
		OpLight->SetIntensity(9000.f * Alpha);
	}
}

void AHGLevel::SetHallLamp(int32 Index, float Level)
{
	if (HallLamps.IsValidIndex(Index) && HallLamps[Index])
	{
		HallLamps[Index]->SetIntensity(HallLampFull[Index] * FMath::Clamp(Level, 0.f, 1.f));
	}
}

void AHGLevel::SetDoorOpen(float Alpha)
{
	if (WardDoor)
	{
		// Swings outwards: the free edge moves towards +X.
		WardDoor->SetRelativeRotation(FRotator(0.f, -100.f * FMath::Clamp(Alpha, 0.f, 1.f), 0.f));
	}
}

void AHGLevel::SetClock(float Hours)
{
	// A hand points up its own +Y and the clock's face is turned to the room, so turning it the
	// negative way round its Z reads as clockwise from inside.
	if (MinuteHand && HourHand)
	{
		const float Minutes = FMath::Fmod(Hours, 1.f) * 60.f;
		MinuteHand->SetRelativeRotation((FRotationMatrix::MakeFromXY(FVector(0, -1, 0), -FVector::UpVector).ToQuat() * FRotator(0.f, -6.f * Minutes, 0.f).Quaternion()).Rotator());
		HourHand->SetRelativeRotation((FRotationMatrix::MakeFromXY(FVector(0, -1, 0), -FVector::UpVector).ToQuat() * FRotator(0.f, -30.f * FMath::Fmod(Hours, 12.f), 0.f).Quaternion()).Rotator());
	}
}

