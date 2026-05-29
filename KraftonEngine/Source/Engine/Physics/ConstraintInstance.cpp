#include "Physics/ConstraintInstance.h"

#include "Physics/BodyInstance.h"

#include <PxPhysicsAPI.h>
#include <algorithm>

using namespace physx;

namespace
{
	PxD6Motion::Enum ToPxD6Motion(EConstraintMotion Motion)
	{
		switch (Motion)
		{
		case EConstraintMotion::Free:
			return PxD6Motion::eFREE;
		case EConstraintMotion::Limited:
			return PxD6Motion::eLIMITED;
		case EConstraintMotion::Locked:
		default:
			return PxD6Motion::eLOCKED;
		}
	}

	float DegreesToRadians(float Degrees)
	{
		constexpr float Pi = 3.14159265358979323846f;
		return Degrees * (Pi / 180.0f);
	}
}

void FConstraintInstance::InitConstraint(
	FBodyInstance* ParentBody,
	FBodyInstance* ChildBody,
	const FBodyInstanceInitParams& InitParams)
{
	TermConstraint();

	if (!ParentBody || !ChildBody || !ParentBody->Actor || !ChildBody->Actor || !InitParams.Physics)
	{
		return;
	}

	PxD6Joint* Joint = PxD6JointCreate(
		*InitParams.Physics,
		ParentBody->Actor,
		PxTransform(PxIdentity),
		ChildBody->Actor,
		PxTransform(PxIdentity));

	if (!Joint)
	{
		return;
	}

	Joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, !bDisableCollision);

	Joint->setMotion(PxD6Axis::eX, ToPxD6Motion(LinearXMotion));
	Joint->setMotion(PxD6Axis::eY, ToPxD6Motion(LinearYMotion));
	Joint->setMotion(PxD6Axis::eZ, ToPxD6Motion(LinearZMotion));
	Joint->setMotion(PxD6Axis::eSWING1, ToPxD6Motion(Swing1Motion));
	Joint->setMotion(PxD6Axis::eSWING2, ToPxD6Motion(Swing2Motion));
	Joint->setMotion(PxD6Axis::eTWIST, ToPxD6Motion(TwistMotion));

	if (LinearXMotion == EConstraintMotion::Limited ||
		LinearYMotion == EConstraintMotion::Limited ||
		LinearZMotion == EConstraintMotion::Limited)
	{
		Joint->setLinearLimit(PxJointLinearLimit(
			InitParams.Physics->getTolerancesScale(),
			(std::max)(0.0f, LinearLimitSize)));
	}

	if (Swing1Motion == EConstraintMotion::Limited || Swing2Motion == EConstraintMotion::Limited)
	{
		Joint->setSwingLimit(PxJointLimitCone(
			DegreesToRadians((std::max)(0.0f, Swing1LimitDegrees)),
			DegreesToRadians((std::max)(0.0f, Swing2LimitDegrees))));
	}

	if (TwistMotion == EConstraintMotion::Limited)
	{
		Joint->setTwistLimit(PxJointAngularLimitPair(
			DegreesToRadians(TwistLimitMinDegrees),
			DegreesToRadians(TwistLimitMaxDegrees)));
	}

	ConstraintHandle.Joint = Joint;
}

void FConstraintInstance::TermConstraint()
{
	if (ConstraintHandle.Joint)
	{
		ConstraintHandle.Joint->release();
		ConstraintHandle.Joint = nullptr;
	}

	PhysScene = nullptr;
}
