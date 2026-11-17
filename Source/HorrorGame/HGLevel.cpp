#include "HGLevel.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
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

	UInstancedStaticMeshComponent* MakeTiles(AActor* Owner, USceneComponent* Parent, const FLinearColor& Color, bool bCollide)
	{
		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(Owner);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(HG::Shape(TEXT("Cube")));
		Comp->SetupAttachment(Parent);
		Comp->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Comp->SetCollisionResponseToAllChannels(ECR_Block);
		Comp->RegisterComponent();
		Owner->AddInstanceComponent(Comp);
		HG::Paint(Comp, Color);
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

	Walls = MakeTiles(this, Root, Theme.Wall, true);
	Floors = MakeTiles(this, Root, Theme.Floor, true);
	Ceilings = MakeTiles(this, Root, Theme.Ceiling, true);

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
