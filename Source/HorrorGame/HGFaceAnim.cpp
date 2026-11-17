#include "HGFaceAnim.h"

#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "Animation/AnimInstanceProxy.h"

namespace
{
	/** One face control and how much of it each feeling asks for. */
	struct FHGFaceControl
	{
		const TCHAR* Name;
		float Warm;
		float Sad;
		float Wrong;
	};

	// The names are RigLogic's raw controls, the ones a MetaHuman face is animated with.
	const FHGFaceControl Controls[] = {
		{ TEXT("CTRL_expressions_mouthCornerPullL"), 0.42f, 0.10f, 0.95f },
		{ TEXT("CTRL_expressions_mouthCornerPullR"), 0.42f, 0.10f, 0.95f },
		{ TEXT("CTRL_expressions_eyeCheekRaiseL"), 0.30f, 0.10f, 0.f },
		{ TEXT("CTRL_expressions_eyeCheekRaiseR"), 0.30f, 0.10f, 0.f },
		{ TEXT("CTRL_expressions_browRaiseInL"), 0.12f, 0.80f, 0.f },
		{ TEXT("CTRL_expressions_browRaiseInR"), 0.12f, 0.80f, 0.f },
		{ TEXT("CTRL_expressions_mouthCornerDepressL"), 0.f, 0.30f, 0.f },
		{ TEXT("CTRL_expressions_mouthCornerDepressR"), 0.f, 0.30f, 0.f },
		{ TEXT("CTRL_expressions_eyeSquintInnerL"), 0.10f, 0.25f, 0.f },
		{ TEXT("CTRL_expressions_eyeSquintInnerR"), 0.10f, 0.25f, 0.f },
		{ TEXT("CTRL_expressions_eyeWidenL"), 0.f, 0.f, 1.f },
		{ TEXT("CTRL_expressions_eyeWidenR"), 0.f, 0.f, 1.f },
		{ TEXT("CTRL_expressions_browRaiseOuterL"), 0.f, 0.f, 0.55f },
		{ TEXT("CTRL_expressions_browRaiseOuterR"), 0.f, 0.f, 0.55f },
		{ TEXT("CTRL_expressions_mouthStretchL"), 0.f, 0.f, 0.35f },
		{ TEXT("CTRL_expressions_mouthStretchR"), 0.f, 0.f, 0.35f },
		{ TEXT("CTRL_expressions_mouthUpperLipRaiseL"), 0.f, 0.f, 0.45f },
		{ TEXT("CTRL_expressions_mouthUpperLipRaiseR"), 0.f, 0.f, 0.45f },
		{ TEXT("CTRL_expressions_jawOpen"), 0.f, 0.f, 0.10f },
	};
	const TCHAR* const Lids[] = { TEXT("CTRL_expressions_eyeBlinkL"), TEXT("CTRL_expressions_eyeBlinkR") };
}

/** The part that runs on the animation worker thread: it only reads what PreUpdate copied in. */
struct FHGFaceAnimProxy : public FAnimInstanceProxy
{
	explicit FHGFaceAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
	{
		// The head is attached to the body, and takes its neck and head bones from it.
		Follow.bUseAttachedParent = true;
		for (const FHGFaceControl& Control : Controls)
		{
			Names.Add(FName(Control.Name));
		}
		for (const TCHAR* Lid : Lids)
		{
			LidNames.Add(FName(Lid));
		}
	}

	FAnimNode_CopyPoseFromMesh Follow;
	TArray<FName> Names;
	TArray<FName> LidNames;
	float Warm = 0.f;
	float Sad = 0.f;
	float Wrong = 0.f;
	float Blink = 0.f;

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
		Follow.PreUpdate(InAnimInstance);
		const UHGFaceAnim* Face = CastChecked<UHGFaceAnim>(InAnimInstance);
		Warm = Face->Warm;
		Sad = Face->Sad;
		Wrong = Face->Wrong;
		Blink = Face->Blink;
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		Follow.Evaluate_AnyThread(Output);
		// What is left of the person fades as the wrongness comes in, so the two do not add up to a grimace.
		const float Person = 1.f - Wrong;
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			const FHGFaceControl& Control = Controls[i];
			const float Value = FMath::Clamp((Control.Warm * Warm + Control.Sad * Sad) * Person + Control.Wrong * Wrong, 0.f, 1.f);
			if (Value > 0.001f)
			{
				Output.Curve.Set(Names[i], Value);
			}
		}
		const float Shut = Blink * Person;
		if (Shut > 0.001f)
		{
			for (const FName& Lid : LidNames)
			{
				Output.Curve.Set(Lid, Shut);
			}
		}
		return true;
	}
};

FAnimInstanceProxy* UHGFaceAnim::CreateAnimInstanceProxy()
{
	return new FHGFaceAnimProxy(this);
}

void UHGFaceAnim::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// A face changes slowly enough that the change is not seen, only its result.
	Warm = FMath::FInterpTo(Warm, WantWarm, DeltaSeconds, 1.6f);
	Sad = FMath::FInterpTo(Sad, WantSad, DeltaSeconds, 1.2f);

	// People blink every few seconds: down fast, up a little slower.
	NextBlink -= DeltaSeconds;
	if (NextBlink <= 0.f)
	{
		NextBlink = FMath::FRandRange(2.4f, 6.f);
		BlinkLeft = 0.2f;
	}
	BlinkLeft = FMath::Max(0.f, BlinkLeft - DeltaSeconds);
	const float Along = 1.f - BlinkLeft / 0.2f;
	Blink = BlinkLeft > 0.f ? (Along < 0.35f ? Along / 0.35f : 1.f - (Along - 0.35f) / 0.65f) : 0.f;
}
