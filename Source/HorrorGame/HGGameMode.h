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
class UHGFaceAnim;
class UHGPersonAnim;
class AHGToy;
class USoundBase;
class UStaticMesh;

/** One of the doctors looking for him in scene five. */
struct FHGSearcher
{
	/** Which of the two doctors this is. */
	int32 Doctor = 0;
	/** The floor this one keeps to. They do not follow him out of it. */
	FBox2D Zone = FBox2D(ForceInit);
	/** The two ends of the walk they keep up while nothing has caught their attention. */
	FVector2D Ends[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
	int32 Heading = 0;
	/** 0 walking the beat, 1 standing at its end, 2 going to look at a noise, 3 standing looking round, 4 coming for him. */
	int32 State = 0;
	float Timer = 0.f;
	FVector2D Target = FVector2D::ZeroVector;
	/** From 0, nothing, to 1, sure they have seen him. */
	float Suspicion = 0.f;
	float SinceSeen = 100.f;
	/** True when they watched him go under whatever he is hiding under. Then hiding does not help. */
	bool bSawHide = false;
	float StepWait = 0.f;
	float CallWait = 9.f;
};

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
	/** Scene five: the toy he threw has landed and squeaked. Whoever is in earshot goes to look. */
	void ToyLanded(const FVector& At);
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
	/** A chapter card shown over the picture for a few seconds, and when it came up. */
	FString CardText;
	float CardStart = -100.f;
	/** How far the picture has burned out to white, from 0 to 1. */
	float WhiteOut = 0.f;
	/**
	 * Scene four, the cloth over his eyes. ClothDown is how far it has been drawn down over the
	 * picture; ClothRed how far the red has soaked into it (past 1 it is everywhere); ClothLoom
	 * how close the shadow of whoever is bending over him has come; ClothBusy how much hands are
	 * moving behind it; ClothShift where he has turned his head under it, each way from -1 to 1.
	 */
	float ClothDown = 0.f;
	float ClothRed = 0.f;
	float ClothLoom = 0.f;
	float ClothBusy = 0.f;
	FVector2D ClothShift = FVector2D::ZeroVector;
	/** The picture going to black, from 0 to 1. */
	float BlackOut = 0.f;
	/** A line of help at the foot of the picture, and when it goes. */
	FString Hint;
	float HintUntil = 0.f;
	/** The closing text: which page is up (-1 for none), how solid its black ground is and how visible its words. */
	int32 ClosingPage = -1;
	float ClosingCover = 0.f;
	float ClosingInk = 0.f;
	/** -HGLessBlood: the story without the red. What soaks into the cloth is dark and colourless. */
	bool bLessBlood = false;
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

	/** What their faces are doing. Null entries when a person has no face mesh. */
	UPROPERTY()
	TObjectPtr<UHGFaceAnim> MomFaceAnim;

	UPROPERTY()
	TArray<TObjectPtr<UHGFaceAnim>> DoctorFaces;

	UPROPERTY()
	TObjectPtr<UAnimSequence> PersonIdle;

	UPROPERTY()
	TObjectPtr<UAnimSequence> PersonWalk;

	/** Spawns one of the MetaHuman people and hands back what animates the body. Null when the plugin lacks them. */
	AActor* SpawnPerson(const TCHAR* ClassPath, const FVector& At, float FacingYaw, UHGPersonAnim*& OutAnim, UHGFaceAnim*& OutFace);
	void TickDoctors(float Dt, const AHGPlayer* Player);

	/**
	 * Scene two, the handover. Mom lifts him, carries him to the door and gives him to the woman
	 * doctor, who turns and carries him away down the hall while he looks back over her shoulder.
	 * He can only look. The further he gets from mom, the less colour is left and the less the
	 * doctors look like people.
	 */
	void TickHandover(float Dt, AHGPlayer* Player);

	/**
	 * Scene three, the corridor: the rest of the carry. The hall goes on too long and grows too
	 * tall, the lamps go out behind him one after another, the people they pass turn out to have
	 * the doctors' faces and fall in behind, and the doctor who was following goes ahead to hold
	 * open a door on the left. It ends in the white of the room behind it.
	 */
	void TickCorridor(float Dt, AHGPlayer* Player, float Wrong);

	/**
	 * Scene four, the chair. He is on the doctor's lap, facing the room. He is shown the room for
	 * a few seconds, then a cloth is put over his eyes and everything after that is the cloth:
	 * light through it, shadows on it, voices dulled by it, and red coming up into it from below.
	 * Nothing that is done to him is shown.
	 */
	void TickChair(float Dt, AHGPlayer* Player);

	UPROPERTY()
	TObjectPtr<class UAudioComponent> RingBed;

	/**
	 * Scene five, the escape, which is invented. He wakes alone in a side room and goes to find
	 * his mother. He can crawl, hide under beds and a counter, and throw a toy to draw a doctor
	 * off. The two doctors walk their beats, come to noises, and come for him if they see him;
	 * they never run. Being caught is being carried back to where he last got to.
	 */
	void BeginEscape(AHGPlayer* Player);
	void TickEscape(float Dt, AHGPlayer* Player);
	void TickSearcher(FHGSearcher& S, float Dt, AHGPlayer* Player);
	/** Scene six: he finds her. The colour comes back, and then the closing text. */
	void TickEnding(float Dt, AHGPlayer* Player);
	/** Puts every doctor in the state the walk from the ward leaves them in: too tall, the wrong face. */
	void EveryoneWrong(AHGPlayer* Player);
	/** A hospital bed from the private plugin, with room to crawl under it. */
	void AddBed(const FVector& Centre, float Yaw);
	/** The ward's own sound, birds and all: 1 as it is in the ward, 0 gone. */
	void SetWardSound(float Level);

	TArray<FHGSearcher> Searchers;
	/** The places he cannot be seen in while he is on his hands and knees. */
	TArray<FBox> HideBoxes;

	UPROPERTY()
	TArray<TObjectPtr<class UAudioComponent>> WardBeds;
	TArray<float> WardBedFull;

	UPROPERTY()
	TObjectPtr<class UAudioComponent> DroneBed;

	/** His own breathing, heard only while he is hiding. */
	UPROPERTY()
	TObjectPtr<class UAudioComponent> BreathBed;
	float BreathLevel = 0.f;
	bool bWhimpered = false;
	bool bChairWhimper = false;

	UPROPERTY()
	TObjectPtr<AHGToy> EscapeToy;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> BedParts;

	bool bEscape = false;
	bool bEscapeBegun = false;
	bool bEnding = false;
	bool bEndingDone = false;
	bool bHidden = false;
	bool bMomDoorSounded = false;
	bool bToyHint = false;
	bool bEndLifted = false;
	bool bCaughtMoved = false;
	bool bCaughtFreed = false;
	float EscapeTimer = 0.f;
	float CaughtTimer = -1.f;
	float MomDoorOpen = 0.f;
	float MomCallWait = 5.f;
	float EndingTimer = 0.f;
	float EndLift = 0.f;
	float FlickWait = 0.f;
	float ClosingTimer = 0.f;
	int32 Checkpoint = 0;
	int32 Catches = 0;
	int32 EscShot = 0;
	float EscShotTimer = 0.f;
	int32 EscBot = 0;
	float EscBotTimer = 0.f;
	/** One bit for each thing the self-play check saw work: crawl, hide, lure, caught, carried back, door. */
	int32 EscBotPassed = 0;
	FVector EndingFrom = FVector::ZeroVector;

	/** -HGScene=4 starts the story at the chair, for working on it without sitting through the first three. */
	int32 StartScene = 1;
	bool bChair = false;
	bool bChairDone = false;
	float ChairTimer = 0.f;
	int32 ChairCues = 0;
	FRotator ChairLook = FRotator::ZeroRotator;

	/** The people standing in the corridor: the same two faces again. Hidden until the corridor begins. */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> Staff;

	UPROPERTY()
	TArray<TObjectPtr<UHGPersonAnim>> StaffAnims;

	UPROPERTY()
	TArray<TObjectPtr<UHGFaceAnim>> StaffFaces;

	UPROPERTY()
	TObjectPtr<class UAudioComponent> HeartBed;

	/** A faint light on each of their faces, like the one on the doctor who follows. */
	UPROPERTY()
	TArray<TObjectPtr<class UPointLightComponent>> StaffLights;

	/** Hangs a small cold light, off to begin with, in front of a person's face. */
	class UPointLightComponent* AddFaceLight(AActor* Person);

	bool bCorridor = false;
	bool bCorridorDone = false;
	float CorridorTimer = 0.f;
	/** Bit i is set once staff member i has turned to follow, and again in the second mask once walking. */
	int32 StaffJoined = 0;
	int32 StaffWalking = 0;
	int32 LampsOut = 0;
	float LampWait = 0.f;
	TArray<float> LampLevel;
	float StepWait = 0.f;
	float EchoWait = -1.f;
	int32 StepCount = 0;
	bool bOvertaking = false;
	int32 OvertakeLeg = 0;
	FVector OvertakeVia = FVector::ZeroVector;
	bool bOpDoor = false;
	float OpDoorOpen = 0.f;
	float WhiteHeld = 0.f;
	bool bNearlySaid = false;
	bool bHereSaid = false;
	bool bCartHeard = false;
	/** Moves a person a step towards To, on the walk clip. True once they are there and standing. */
	bool WalkPerson(AActor* Person, UHGPersonAnim* Anim, const FVector& To, float Speed, float ClipRate, float Dt);
	void FacePerson(AActor* Person, const FVector& Point, float Dt, float Speed) const;
	/** Turns a person's head towards Target. PitchShare is how much of the up and down they follow. */
	void PersonLooks(const AActor* Person, UHGPersonAnim* Anim, const FVector& Target, float PitchShare) const;
	/** A point given from where a person stands: so far ahead of them, to their left, and up. */
	FVector PersonPoint(const AActor* Person, float Ahead, float Left, float Up) const;

	/** A faint cold light on the face of the doctor who follows, so what her face becomes can be seen. */
	UPROPERTY()
	TObjectPtr<class UPointLightComponent> FollowLight;

	/** How many of the story's voice lines are in the private plugin, for the self-play check. */
	int32 VoicesFound = 0;
	/** And how many of the corridor's licensed sounds. */
	int32 CorridorSoundsFound = 0;
	int32 HandStep = 0;
	float HandTimer = 0.f;
	/** Where his eyes were when mom reached him, and where she stops to pick him up. */
	FVector HandFrom = FVector::ZeroVector;
	FVector HandMomStop = FVector::ZeroVector;
	int32 HandLeg = 0;
	int32 SecondLeg = 0;
	int32 HandShots = 0;
	bool bSecondFollows = false;
	bool bHushed = false;
	bool bCried = false;
	bool bHandedOver = false;
	bool bShotPlaced = false;

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

	/** Paints the cloth over the picture: see the Cloth fields of AHGGameMode. */
	void DrawCloth(const AHGGameMode* Game, float W, float H, float Now);

	/** The cloth is a small picture worked out a few times a second and stretched over the screen. */
	UPROPERTY()
	TObjectPtr<class UTexture2D> Cloth;

	/** The weave, a few pixels across, tiled over it at full sharpness. */
	UPROPERTY()
	TObjectPtr<class UTexture2D> Weave;

	TArray<FColor> ClothPixels;
	/** For each point of the cloth, how late the red reaches it: 0 where it starts, about 1 at the far corners. */
	TArray<float> ClothWet;
	float ClothNext = 0.f;
	void Centered(const FString& Text, float Y, float Scale, const FLinearColor& Color, FName Face = NAME_None);
};
