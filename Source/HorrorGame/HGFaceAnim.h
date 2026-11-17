#pragma once

#include "Animation/AnimInstance.h"
#include "CoreMinimal.h"
#include "HGFaceAnim.generated.h"

/**
 * Gives a MetaHuman face an expression.
 *
 * A MetaHuman's head is its own mesh. Its bones follow the body's, and its expression comes from
 * named control values that the face's own post-process step (RigLogic) turns into skin. The stock
 * face animation leaves those values at nothing, which is why everyone looked blank. This follows
 * the body the same way and then sets the few controls the story needs: a soft smile, a sad one,
 * blinking, and the look the doctors take on as he is carried away from his mother.
 */
UCLASS(Transient, NotBlueprintable)
class UHGFaceAnim : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** How warm the face is, from blank to a soft smile. */
	void SetWarm(float Alpha) { WantWarm = FMath::Clamp(Alpha, 0.f, 1.f); }
	/** Sadness: the inner brows lift and the mouth turns down a little. With some warmth left it is a sad smile. */
	void SetSad(float Alpha) { WantSad = FMath::Clamp(Alpha, 0.f, 1.f); }
	/**
	 * How far the face has stopped being a person's: the eyes open too wide and stop blinking, and
	 * a smile spreads and stays. It is set directly, not eased, because whoever sets it is already easing it.
	 */
	void SetWrong(float Alpha) { Wrong = FMath::Clamp(Alpha, 0.f, 1.f); }

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FHGFaceAnimProxy;

	float WantWarm = 0.f;
	float WantSad = 0.f;
	float Warm = 0.f;
	float Sad = 0.f;
	float Wrong = 0.f;
	/** 1 while the lids are shut. */
	float Blink = 0.f;
	float NextBlink = 2.f;
	float BlinkLeft = 0.f;
};
