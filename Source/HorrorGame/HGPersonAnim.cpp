#include "HGPersonAnim.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
#include "Animation/AttributesRuntime.h"

namespace
{
	FCompactPoseBoneIndex FindIndex(const FBoneContainer& Bones, const TCHAR* Bone)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Bone));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	}
}

/** The part that runs on the animation worker thread: it only reads what PreUpdate copied in. */
struct FHGPersonAnimProxy : public FAnimInstanceProxy
{
	explicit FHGPersonAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	const UAnimSequence* Clip = nullptr;
	const UAnimSequence* Still = nullptr;
	float Time = 0.f;
	float Present = 0.f;
	float HoldFold = 1.f;
	float Yaw = 0.f;
	float Pitch = 0.f;
	float Hold = 0.f;
	float Wrong = 0.f;
	float Sit = 0.f;

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
		const UHGPersonAnim* Person = CastChecked<UHGPersonAnim>(InAnimInstance);
		Clip = Person->Clip;
		Still = Person->Still;
		Present = Person->Present;
		HoldFold = Person->HoldFold;
		Time = Person->Time;
		Yaw = Person->Yaw;
		Pitch = Person->Pitch;
		Hold = Person->Hold;
		Wrong = Person->Wrong;
		Sit = Person->Sit;
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

		if (FMath::IsNearlyZero(Yaw, 0.05f) && FMath::IsNearlyZero(Pitch, 0.05f) && Hold < 0.002f && Present < 0.002f && Wrong < 0.002f && Sit < 0.002f)
		{
			return true;
		}
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();

		// Arms that hold something take their pose from the still clip, not from the walk under
		// them: everything from each collarbone outwards. The torso still moves, and carries them.
		const float StillArms = FMath::Max(Hold, Present);
		if (StillArms > 0.002f && Still && Still != Clip)
		{
			FCompactPose StillPose;
			StillPose.SetBoneContainer(&Bones);
			FBlendedCurve StillCurve;
			StillCurve.InitFrom(Output.Curve);
			UE::Anim::FStackAttributeContainer StillAttributes;
			FAnimationPoseData StillData(StillPose, StillCurve, StillAttributes);
			Still->GetAnimationPose(StillData, FAnimExtractContext(0.0, false, {}, true));
			const FCompactPoseBoneIndex LeftCollar = FindIndex(Bones, TEXT("clavicle_l"));
			const FCompactPoseBoneIndex RightCollar = FindIndex(Bones, TEXT("clavicle_r"));
			for (FCompactPoseBoneIndex Index(1); Index < Output.Pose.GetNumBones(); ++Index)
			{
				// Which arm, if either, this bone hangs from.
				float Side = 0.f;
				for (FCompactPoseBoneIndex Up = Index; Up.IsValid() && Up.GetInt() > 0; Up = Bones.GetParentBoneIndex(Up))
				{
					if (Up == LeftCollar) { Side = Hold; break; }
					if (Up == RightCollar) { Side = StillArms; break; }
				}
				if (Side > 0.002f)
				{
					Output.Pose[Index].BlendWith(StillPose[Index], Side);
				}
			}
		}

		FCSPose<FCompactPose> Posed;
		Posed.InitPose(Output.Pose);
		auto Find = [&Bones](const FName& Bone)
		{
			const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Bone);
			return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		};
		// In the body's own space it stands along Z and faces +Y, with its left arm on +X.
		// Parents first, always: a child's place is worked out from its parent the first time it is asked for.

		// The turn is shared out up the neck so it reads as a person looking, not a head on a pin.
		// The same four bones carry the stretch: each lifts a little off the one below, and the head
		// itself grows along its own length, which takes the face with it.
		struct FShare { FName Bone; float Part; float Lift; };
		static const FShare Shares[] = {
			{ TEXT("spine_05"), 0.12f, 0.f }, { TEXT("neck_01"), 0.22f, 2.6f }, { TEXT("neck_02"), 0.22f, 2.6f }, { TEXT("head"), 0.44f, 1.2f } };
		for (const FShare& Share : Shares)
		{
			const FCompactPoseBoneIndex Index = Find(Share.Bone);
			if (!Index.IsValid())
			{
				continue;
			}
			// The nod is about X and the turn about Z, with the turn applied last so the nod stays level.
			FTransform At = Posed.GetComponentSpaceTransform(Index);
			const FQuat Nod(FVector::XAxisVector, FMath::DegreesToRadians(Pitch * Share.Part));
			const FQuat Turn(FVector::ZAxisVector, FMath::DegreesToRadians(Yaw * Share.Part));
			At.SetRotation((Turn * Nod * At.GetRotation()).GetNormalized());
			if (Wrong > 0.f)
			{
				At.AddToTranslation(FVector(0.f, 0.f, Share.Lift * Wrong));
				if (Share.Bone == FName(TEXT("head")))
				{
					// Longer along whichever of the bone's own axes points up the body, a touch narrower across.
					const FQuat Rot = At.GetRotation();
					const FVector Ups(FMath::Abs(Rot.GetAxisX().Z), FMath::Abs(Rot.GetAxisY().Z), FMath::Abs(Rot.GetAxisZ().Z));
					FVector Scale(1.f - 0.05f * Wrong);
					Scale[Ups.X >= Ups.Y && Ups.X >= Ups.Z ? 0 : (Ups.Y >= Ups.Z ? 1 : 2)] = 1.f + 0.2f * Wrong;
					At.SetScale3D(At.GetScale3D() * Scale);
				}
			}
			Posed.SetComponentSpaceTransform(Index, At);
		}

		// Sitting: the thighs swing forward to level and the shins hang straight down from the knees.
		if (Sit > 0.002f)
		{
			struct FLeg { FName Thigh; FName Calf; };
			static const FLeg Legs[] = { { TEXT("thigh_l"), TEXT("calf_l") }, { TEXT("thigh_r"), TEXT("calf_r") } };
			for (const FLeg& Leg : Legs)
			{
				const FCompactPoseBoneIndex Thigh = Find(Leg.Thigh);
				const FCompactPoseBoneIndex Calf = Find(Leg.Calf);
				if (!Thigh.IsValid() || !Calf.IsValid())
				{
					continue;
				}
				FTransform At = Posed.GetComponentSpaceTransform(Thigh);
				At.SetRotation((FQuat(FVector::XAxisVector, FMath::DegreesToRadians(86.f * Sit)) * At.GetRotation()).GetNormalized());
				Posed.SetComponentSpaceTransform(Thigh, At);
				At = Posed.GetComponentSpaceTransform(Calf);
				At.SetRotation((FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-86.f * Sit)) * At.GetRotation()).GetNormalized());
				Posed.SetComponentSpaceTransform(Calf, At);
			}
		}

		// Carrying: the upper arms come forward, the forearms fold up and in across the chest.
		if (Hold > 0.002f)
		{
			struct FArm { FName Upper; FName Lower; float Side; };
			static const FArm Arms[] = { { TEXT("upperarm_l"), TEXT("lowerarm_l"), 1.f }, { TEXT("upperarm_r"), TEXT("lowerarm_r"), -1.f } };
			for (const FArm& Arm : Arms)
			{
				const FCompactPoseBoneIndex Upper = Find(Arm.Upper);
				const FCompactPoseBoneIndex Lower = Find(Arm.Lower);
				if (!Upper.IsValid() || !Lower.IsValid())
				{
					continue;
				}
				FTransform At = Posed.GetComponentSpaceTransform(Upper);
				At.SetRotation((FQuat(FVector::XAxisVector, FMath::DegreesToRadians(34.f * Hold)) * At.GetRotation()).GetNormalized());
				Posed.SetComponentSpaceTransform(Upper, At);
				At = Posed.GetComponentSpaceTransform(Lower);
				const FQuat Fold(FVector::XAxisVector, FMath::DegreesToRadians(62.f * Hold * HoldFold));
				const FQuat Inward(FVector::ZAxisVector, FMath::DegreesToRadians(42.f * Hold * Arm.Side));
				At.SetRotation((Inward * Fold * At.GetRotation()).GetNormalized());
				Posed.SetComponentSpaceTransform(Lower, At);
			}
		}

		// Holding something ready to use: the right upper arm a little forward, the forearm level
		// from the elbow, so the hand is in front of her chest and what is in it points at him.
		if (Present > 0.002f && Hold < 0.5f)
		{
			const FCompactPoseBoneIndex Upper = Find(TEXT("upperarm_r"));
			const FCompactPoseBoneIndex Lower = Find(TEXT("lowerarm_r"));
			if (Upper.IsValid() && Lower.IsValid())
			{
				FTransform At = Posed.GetComponentSpaceTransform(Upper);
				const FQuat Forward(FVector::XAxisVector, FMath::DegreesToRadians(20.f * Present));
				const FQuat Out(FVector::YAxisVector, FMath::DegreesToRadians(-8.f * Present));
				At.SetRotation((Out * Forward * At.GetRotation()).GetNormalized());
				Posed.SetComponentSpaceTransform(Upper, At);
				At = Posed.GetComponentSpaceTransform(Lower);
				const FQuat Fold(FVector::XAxisVector, FMath::DegreesToRadians(78.f * Present));
				const FQuat Inward(FVector::ZAxisVector, FMath::DegreesToRadians(-18.f * Present));
				At.SetRotation((Inward * Fold * At.GetRotation()).GetNormalized());
				Posed.SetComponentSpaceTransform(Lower, At);
			}
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
	Hold = FMath::FInterpTo(Hold, WantHold, DeltaSeconds, 2.4f);
	Present = FMath::FInterpTo(Present, WantPresent, DeltaSeconds, 2.f);
}
