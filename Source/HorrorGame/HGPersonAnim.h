#pragma once

#include "Animation/AnimInstance.h"
#include "CoreMinimal.h"
#include "HGPersonAnim.generated.h"

class UAnimSequence;

/**
 * Plays one clip on a MetaHuman body and turns the head and neck on top of it.
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

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FHGPersonAnimProxy;

	UPROPERTY()
	TObjectPtr<UAnimSequence> Clip;

	float Rate = 1.f;
	float Time = 0.f;
	float WantYaw = 0.f;
	float WantPitch = 0.f;
	float Yaw = 0.f;
	float Pitch = 0.f;
};
