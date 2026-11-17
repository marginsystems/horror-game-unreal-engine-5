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

	/** One speech channel and the controls it moves, each by its own share. */
	struct FHGMouthControl
	{
		int32 Channel;
		const TCHAR* Name;
		float Share;
	};
	// Speech: the jaw, the lips rounding (o), pushed forward (oo, w), pressed together (m, b, p),
	// drawn wide (ee, s) and the lower lip tucked under the teeth (f, v). The lower lip also drops
	// a little as the jaw opens, so the teeth show the way they do in real speech.
	const FHGMouthControl MouthControls[] = {
		{ 0, TEXT("CTRL_expressions_jawOpen"), 0.85f },
		{ 0, TEXT("CTRL_expressions_mouthLowerLipDepressL"), 0.3f },
		{ 0, TEXT("CTRL_expressions_mouthLowerLipDepressR"), 0.3f },
		{ 1, TEXT("CTRL_expressions_mouthFunnelUL"), 0.8f },
		{ 1, TEXT("CTRL_expressions_mouthFunnelUR"), 0.8f },
		{ 1, TEXT("CTRL_expressions_mouthFunnelDL"), 0.8f },
		{ 1, TEXT("CTRL_expressions_mouthFunnelDR"), 0.8f },
		{ 2, TEXT("CTRL_expressions_mouthLipsPurseUL"), 0.75f },
		{ 2, TEXT("CTRL_expressions_mouthLipsPurseUR"), 0.75f },
		{ 2, TEXT("CTRL_expressions_mouthLipsPurseDL"), 0.75f },
		{ 2, TEXT("CTRL_expressions_mouthLipsPurseDR"), 0.75f },
		{ 3, TEXT("CTRL_expressions_mouthLipsTogetherUL"), 0.9f },
		{ 3, TEXT("CTRL_expressions_mouthLipsTogetherUR"), 0.9f },
		{ 3, TEXT("CTRL_expressions_mouthLipsTogetherDL"), 0.9f },
		{ 3, TEXT("CTRL_expressions_mouthLipsTogetherDR"), 0.9f },
		{ 4, TEXT("CTRL_expressions_mouthStretchL"), 0.5f },
		{ 4, TEXT("CTRL_expressions_mouthStretchR"), 0.5f },
		{ 5, TEXT("CTRL_expressions_mouthLowerLipBiteL"), 0.8f },
		{ 5, TEXT("CTRL_expressions_mouthLowerLipBiteR"), 0.8f },
	};

	struct FHGLipLine
	{
		const TCHAR* Name;
		int32 Frames;
		const uint8* Data;
	};
#include "HGLipSyncData.inl"
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
		for (const FHGMouthControl& Control : MouthControls)
		{
			MouthNames.Add(FName(Control.Name));
		}
	}

	FAnimNode_CopyPoseFromMesh Follow;
	TArray<FName> Names;
	TArray<FName> LidNames;
	TArray<FName> MouthNames;
	float Mouth[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
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
		FMemory::Memcpy(Mouth, Face->Mouth, sizeof(Mouth));
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
		// Speech goes on top of whatever the face is doing, and is not taken away by the wrongness:
		// they go on talking to him all the way.
		for (int32 i = 0; i < MouthNames.Num(); ++i)
		{
			const float Value = Mouth[MouthControls[i].Channel] * MouthControls[i].Share;
			if (Value > 0.001f)
			{
				Output.Curve.Set(MouthNames[i], FMath::Clamp(Output.Curve.Get(MouthNames[i]) + Value, 0.f, 1.f));
			}
		}
		return true;
	}
};

FAnimInstanceProxy* UHGFaceAnim::CreateAnimInstanceProxy()
{
	return new FHGFaceAnimProxy(this);
}

bool UHGFaceAnim::Speak(const TCHAR* Line)
{
	for (const FHGLipLine& Lip : HGLipLines)
	{
		if (FCString::Stricmp(Lip.Name, Line) == 0)
		{
			SpeechLine = Lip.Data;
			SpeechFrames = Lip.Frames;
			SpeechTime = 0.f;
			return true;
		}
	}
	SpeechLine = nullptr;
	return false;
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

	// The mouth follows the line frame by frame, and settles back to rest once it is said.
	float Want[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	if (SpeechLine)
	{
		SpeechTime += DeltaSeconds;
		const float At = SpeechTime * 50.f;
		const int32 Frame = FMath::FloorToInt32(At);
		if (Frame >= SpeechFrames - 1)
		{
			SpeechLine = nullptr;
		}
		else
		{
			const float Along2 = At - Frame;
			for (int32 c = 0; c < 6; ++c)
			{
				Want[c] = FMath::Lerp(SpeechLine[Frame * 6 + c], SpeechLine[(Frame + 1) * 6 + c], Along2) / 100.f;
			}
		}
	}
	for (int32 c = 0; c < 6; ++c)
	{
		Mouth[c] = SpeechLine ? Want[c] : FMath::FInterpTo(Mouth[c], 0.f, DeltaSeconds, 10.f);
	}
}
