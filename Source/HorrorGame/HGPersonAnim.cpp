#include "HGPersonAnim.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"

/** The part that runs on the animation worker thread: it only reads what PreUpdate copied in. */
struct FHGPersonAnimProxy : public FAnimInstanceProxy
{
	explicit FHGPersonAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	const UAnimSequence* Clip = nullptr;
	float Time = 0.f;
	float Yaw = 0.f;
	float Pitch = 0.f;

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
		const UHGPersonAnim* Person = CastChecked<UHGPersonAnim>(InAnimInstance);
		Clip = Person->Clip;
		Time = Person->Time;
		Yaw = Person->Yaw;
		Pitch = Person->Pitch;
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		if (!Clip)
		{
			Output.ResetToRefPose();
			return true;
		}
		FAnimationPoseData PoseData(Output);
		Clip->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false, {}, true));

		// A walk clip carries the root forwards and snaps it back on every loop. The game moves the
		// actor instead, so the root stays over the actor and keeps only its height.
		FTransform& Root = Output.Pose[FCompactPoseBoneIndex(0)];
		const FVector RootAt = Root.GetTranslation();
		Root.SetTranslation(FVector(0.f, 0.f, RootAt.Z));

		if (FMath::IsNearlyZero(Yaw, 0.05f) && FMath::IsNearlyZero(Pitch, 0.05f))
		{
			return true;
		}
		// The turn is shared out up the neck so it reads as a person looking, not a head on a pin.
		// Parents first: a child's place is worked out from its parent the first time it is asked for.
		struct FShare { FName Bone; float Part; };
		static const FShare Shares[] = {
			{ TEXT("spine_05"), 0.12f }, { TEXT("neck_01"), 0.22f }, { TEXT("neck_02"), 0.22f }, { TEXT("head"), 0.44f } };
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		FCSPose<FCompactPose> Posed;
		Posed.InitPose(Output.Pose);
		for (const FShare& Share : Shares)
		{
			const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Share.Bone);
			if (MeshIndex == INDEX_NONE)
			{
				continue;
			}
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (!Index.IsValid())
			{
				continue;
			}
			// In the body's own space it stands along Z and faces +Y: the nod is about X and the
			// turn about Z, with the turn applied last so the nod stays level.
			FTransform At = Posed.GetComponentSpaceTransform(Index);
			const FQuat Nod(FVector::XAxisVector, FMath::DegreesToRadians(Pitch * Share.Part));
			const FQuat Turn(FVector::ZAxisVector, FMath::DegreesToRadians(Yaw * Share.Part));
			At.SetRotation((Turn * Nod * At.GetRotation()).GetNormalized());
			Posed.SetComponentSpaceTransform(Index, At);
		}
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Posed), Output.Pose);
		return true;
	}
};

FAnimInstanceProxy* UHGPersonAnim::CreateAnimInstanceProxy()
{
	return new FHGPersonAnimProxy(this);
}

void UHGPersonAnim::SetClip(UAnimSequence* InClip, float InRate)
{
	if (Clip != InClip)
	{
		Clip = InClip;
		Time = 0.f;
	}
	Rate = InRate;
}

void UHGPersonAnim::LookTowards(float YawDegrees, float PitchDegrees)
{
	WantYaw = FMath::Clamp(YawDegrees, -80.f, 80.f);
	WantPitch = FMath::Clamp(PitchDegrees, -35.f, 35.f);
}

void UHGPersonAnim::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (Clip)
	{
		const float Length = Clip->GetPlayLength();
		Time = Length > KINDA_SMALL_NUMBER ? FMath::Fmod(Time + DeltaSeconds * Rate, Length) : 0.f;
	}
	// A head starts and stops gently and never snaps.
	Yaw = FMath::FInterpTo(Yaw, WantYaw, DeltaSeconds, 3.2f);
	Pitch = FMath::FInterpTo(Pitch, WantPitch, DeltaSeconds, 3.2f);
}
