#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HGTypes.h"
#include "HGGameMode.generated.h"

class AHGLevel;
class AHGMonster;
class AHGPlayer;

/**
 * Runs one round: builds the place, drops the fuses, weapons and monster into it,
 * and decides when the player has escaped or been caught.
 *
 * Pick the preview with ?Variant=clinic|office|mine on the map URL or -Variant= on the command line.
 * -HGBot plays the round by itself and quits (a logic check); -HGShots saves screenshots and quits.
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
	void TickBot(float Dt, AHGPlayer* Player);
	void TickShots(float Dt, AHGPlayer* Player);
	void Finish(int32 ExitCode);

	UPROPERTY()
	TObjectPtr<AHGLevel> Level;

	UPROPERTY()
	TObjectPtr<AHGMonster> Monster;

	UPROPERTY()
	TObjectPtr<AActor> StartSpot;

	EHGVariant Variant = EHGVariant::Clinic;
	bool bOver = false;
	bool bWon = false;
	bool bBot = false;
	bool bShots = false;
	int32 BotStep = 0;
	float BotTimer = 0.f;
	int32 ShotStep = 0;
	float ShotTimer = 0.f;
	int32 NoisesHeard = 0;
};

/** Everything on screen is drawn here with the engine font, so the project needs no UI assets. */
UCLASS()
class AHGHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void Centered(const FString& Text, float Y, float Scale, const FLinearColor& Color);
};
