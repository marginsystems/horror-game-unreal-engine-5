#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HGTypes.h"
#include "HGGameMode.generated.h"

class AHGLevel;
class ACameraActor;
class AHGMonster;
class AHGPlayer;
class AHGSorter;
class UAnimSequence;
class UHGPersonAnim;
class AHGToy;
class USoundBase;
class UStaticMesh;

/**
 * Runs one round: builds the place, drops the fuses, weapons and monster into it,
 * and decides when the player has escaped or been caught.
 *
 * Pick the preview with ?Variant=clinic|office|mine on the map URL or -Variant= on the command line.
 * -HGBot plays the round by itself and quits (a logic check); -HGBot -HGCatch checks the monster
 * catches a player who stands still; -HGShots saves screenshots and quits.
 */
UCLASS()
class AHGGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHGGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** A sound at Location that carries Radius. Whatever hears it goes there and stays HoldSeconds. */
	void ReportNoise(const FVector& Location, float Radius, float HoldSeconds, bool bFootstep = false);
	void CollectFuse();
	void PlayerCaught(AHGMonster* By);
	void PlayerDied(const FString& Cause);
	void Say(const FString& Text, float Seconds = 3.f);
	void Travel(EHGVariant To);

	bool IsOver() const { return bOver; }
	/** True while the engine is still preparing shaders, textures or meshes. The round waits for it. */
	bool IsLoading() const { return LoadingLeft > 0; }
	/** True while the opening shots play and the player has no control yet. */
	bool IsCutscene() const { return CineLeft > 0.f; }
	/** Seconds into the opening shots. */
	float CineElapsed() const { return CineLength - CineLeft; }
	int32 LoadingLeft = 0;
	bool HasWon() const { return bWon; }
	EHGVariant GetVariant() const { return Variant; }
	AHGLevel* GetLevel() const { return Level; }
	AHGMonster* GetMonster() const { return Monster; }

	int32 Fuses = 0;
	int32 FusesTotal = 0;
	FString Message;
	float MessageUntil = 0.f;
	FString EndTitle;
	TArray<FString> EndLines;
	float RoundSeconds = 0.f;

private:
	void Win();
	/** Fills a ready-made map (the hospital) with the round: fuses, weapons and the monster. */
	void PopulateWorldMap();
	/** Scene one of the story: lays out the toys in the ward and walks the player through playing with them. */
	void PopulateWard();
	void TickWard(float Dt, AHGPlayer* Player);
	void BeginCutscene(AHGPlayer* Player);
	void TickCutscene(float Dt, AHGPlayer* Player);

	/** The camera the opening shots are filmed with. */
	UPROPERTY()
	TObjectPtr<ACameraActor> Cine;

	static constexpr float CineLength = 9.5f;
	float CineLeft = 0.f;
	float DoorOpen = 0.f;
	float TickBank = 0.f;
	/** True when a real clock recording is ticking on the wall, so the synthesised tick stays quiet. */
	bool bRealClock = false;
	bool bDoorSounded = false;
	bool bStepsSounded = false;
	void TickBot(float Dt, AHGPlayer* Player);
	void TickCatchBot(float Dt, AHGPlayer* Player);
	void TickShots(float Dt, AHGPlayer* Player);
	void TickWorldShots(float Dt, AHGPlayer* Player);
	void Finish(int32 ExitCode);

	UPROPERTY()
	TObjectPtr<AHGLevel> Level;

	UPROPERTY()
	TObjectPtr<AHGMonster> Monster;

	UPROPERTY()
	TObjectPtr<AActor> StartSpot;

	UPROPERTY()
	TObjectPtr<AHGSorter> WardSorter;

	/** The toy mom asks for once the shapes are in. Null when the toy pack is not installed. */
	UPROPERTY()
	TObjectPtr<AHGToy> WardRabbit;

	/** The sorter's three shapes, for the self-play check. */
	UPROPERTY()
	TArray<TObjectPtr<AHGToy>> WardShapes;

	void TickWardBot(float Dt, AHGPlayer* Player);

	/** Mom, when the private plugin has her. */
	UPROPERTY()
	TObjectPtr<AActor> WardMom;

	/** What moves her body: one clip, and her head turned on top of it. */
	UPROPERTY()
	TObjectPtr<UHGPersonAnim> MomAnim;

	/** The two doctors, waiting down the hall until the clock reaches ten. Empty without the private plugin. */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> WardDoctors;

	UPROPERTY()
	TArray<TObjectPtr<UHGPersonAnim>> DoctorAnims;

	UPROPERTY()
	TObjectPtr<UAnimSequence> PersonIdle;

	UPROPERTY()
	TObjectPtr<UAnimSequence> PersonWalk;

	/** Spawns one of the MetaHuman people and hands back what animates the body. Null when the plugin lacks them. */
	AActor* SpawnPerson(const TCHAR* ClassPath, const FVector& At, float FacingYaw, UHGPersonAnim*& OutAnim);
	void TickDoctors(float Dt, const AHGPlayer* Player);

	void TickMom(float Dt, const AHGPlayer* Player, float SecondsToTen);

	/** The MetaHuman body faces along its own +Y, so "facing a point" needs a quarter turn taken off. */
	float MomYawOffset = -90.f;
	/** True while her body is turning to face him again; it starts late and stops close, so she does not twitch. */
	bool bMomTurning = false;
	float MomFillIntensity = 60.f;
	float DoctorSpeed = 105.f;
	float MomGlance = 0.f;
	float MomNextGlance = 9.f;
	int32 MomShot = 0;

	/** Frames drawn and real seconds passed since loading finished, for the frame-rate line in the log. */
	int32 FramesSeen = 0;
	double SecondsSeen = 0.0;

	int32 WardStep = 0;
	float WardTimer = 0.f;
	FVector MomSpot = FVector(40, 170, 0);

	/** Held here so the sounds and models stay loaded for the whole round. */
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> KeptSounds;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> KeptModels;

	EHGVariant Variant = EHGVariant::Clinic;
	bool bOver = false;
	bool bWon = false;
	bool bBot = false;
	bool bShots = false;
	bool bCatch = false;
	bool bLured = false;
	bool bAssetsComplete = false;
	float WalkSeconds = 0.f;
	FVector WalkFrom = FVector::ZeroVector;
	FVector WalkDirection = FVector::ZeroVector;
	int32 BotStep = 0;
	float BotTimer = 0.f;
	int32 ShotStep = 0;
	float ShotTimer = 0.f;
	int32 NoisesHeard = 0;
};

/** Everything on screen is drawn here with the engine font at its true size, so the project needs no UI assets. */
UCLASS()
class AHGHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	/** Scale 1 is the engine font's own 9 points; text is rasterised at the final size, never stretched. */
	void Write(const FString& Text, const FLinearColor& Color, float X, float Y, float Scale, FName Face = NAME_None);
	float Measure(const FString& Text, float Scale, FName Face = NAME_None) const;
	void Centered(const FString& Text, float Y, float Scale, const FLinearColor& Color, FName Face = NAME_None);
};
