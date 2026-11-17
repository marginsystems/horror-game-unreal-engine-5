#pragma once

#include "Animation/AnimInstance.h"
#include "CoreMinimal.h"
#include "HGPersonAnim.generated.h"

class UAnimSequence;

/**
 * Plays one clip on a MetaHuman body and turns the head and neck on top of it; it can also raise
 * the arms to carry a child, and stretch the neck and face.
 *
 * The game has no animation Blueprints, and the engine's single-clip player cannot touch a bone,
 * so this small instance does both: it loops a clip in place (a walk clip does not carry the body
 * away; the game moves the actor) and then swings the neck and head towards where the person is
 * looking. The face, hair and clothes follow the body's bones, so they turn with it.
 */
UCLASS(Transient, NotBlueprintable)
class UHGPersonAnim : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** The clip to loop. Changing it starts the new clip from its beginning. */
	void SetClip(UAnimSequence* InClip, float InRate = 1.f);

	/** Where the head should point, in degrees from straight ahead: left and right, and up. */
	void LookTowards(float YawDegrees, float PitchDegrees);

	/** The yaw the head has actually reached, which lags behind what was asked for. */
	float HeadYaw() const { return Yaw; }

	/**
	 * 1 brings both forearms up and across the chest, the way a small child is carried; 0 lets them hang.
	 * While holding, the arms keep one still pose (see SetStill) whatever the clip under them does, so
	 * a walk does not swing the arms that have a child in them. InFold below 1 keeps the forearms
	 * lower, under a child held face to face rather than up on a shoulder.
	 */
	void SetHold(float Alpha, float InFold = 1.f) { WantHold = FMath::Clamp(Alpha, 0.f, 1.f); HoldFold = InFold; }

	/** The clip whose first frame gives the arms their still pose when holding: a standing idle. */
	void SetStill(UAnimSequence* InStill) { Still = InStill; }

	/**
	 * 1 raises the right hand to chest height in front of her, forearm up, the way an instrument is
	 * held ready; 0 lets it hang. Eased like the hold.
	 */
	void SetPresent(float Alpha) { WantPresent = FMath::Clamp(Alpha, 0.f, 1.f); }

	/**
	 * How far the person has stopped looking like a person, from 0 to 1: the neck draws out and the
	 * head and face lengthen with it. It is how a frightened child remembers an adult, and it is
	 * meant to creep in too slowly to be caught happening.
	 */
	void SetWrong(float Alpha) { Wrong = FMath::Clamp(Alpha, 0.f, 1.f); }

	/**
	 * 1 seats the person: thighs forward, shins down. The actor has to be lowered by whoever asks
	 * for it (about 38 units puts the hips on a chair seat 50 high). Set at once, not eased.
	 */
	void SetSit(float Alpha) { Sit = FMath::Clamp(Alpha, 0.f, 1.f); }

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FHGPersonAnimProxy;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Clip;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Still;

	float Rate = 1.f;
	float Time = 0.f;
	float WantYaw = 0.f;
	float WantPitch = 0.f;
	float Yaw = 0.f;
	float Pitch = 0.f;
	float WantHold = 0.f;
	float Hold = 0.f;
	float HoldFold = 1.f;
	float WantPresent = 0.f;
	float Present = 0.f;
	float Wrong = 0.f;
	float Sit = 0.f;
};
