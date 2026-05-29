#include "Physics/BodyInstance.h"

#include "Component/PrimitiveComponent.h"
#include "Physics/BodySetup.h"

#include <PxPhysicsAPI.h>

using namespace physx;

namespace
{
	PxVec3 ToPxVec3(const FVector& V)
	{
		return PxVec3(V.X, V.Y, V.Z);
	}

	PxQuat ToPxQuat(const FQuat& Q)
	{
		return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
	}

	FVector ToFVector(const PxVec3& V)
	{
		return FVector(V.x, V.y, V.z);
	}

	FQuat ToFQuat(const PxQuat& Q)
	{
		return FQuat(Q.x, Q.y, Q.z, Q.w);
	}

	PxTransform GetPxTransform(UPrimitiveComponent* Comp)
	{
		if (!Comp)
		{
			return PxTransform(PxIdentity);
		}

		const FVector Pos = Comp->GetWorldLocation();
		const FQuat Rot = Comp->GetWorldMatrix().ToQuat();
		return PxTransform(ToPxVec3(Pos), ToPxQuat(Rot));
	}
}

void FBodyInstance::InitBody(UBodySetup* InBodySetup, UPrimitiveComponent* InOwnerComponent, const FBodyInstanceInitParams& InitParams)
{
	TermBody(InitParams);

	BodySetup = InBodySetup;
	OwnerComponent = InOwnerComponent;
	Actor = nullptr;

	if (!OwnerComponent || !InitParams.Physics || !InitParams.Scene)
	{
		return;
	}

	bSimulatePhysics = OwnerComponent->GetSimulatePhysics();
	Mass = OwnerComponent->GetMass();

	const PxTransform BodyTransform = GetPxTransform(OwnerComponent);
	Actor = bSimulatePhysics
		? static_cast<PxRigidActor*>(InitParams.Physics->createRigidDynamic(BodyTransform))
		: static_cast<PxRigidActor*>(InitParams.Physics->createRigidStatic(BodyTransform));

	if (!Actor)
	{
		return;
	}

	Actor->userData = this;
	CreateShapesFromBodySetup(InitParams);

	if (PxRigidDynamic* DynamicActor = Actor->is<PxRigidDynamic>())
	{
		const float MassKg = Mass > 0.0f ? Mass : 1.0f;
		PxRigidBodyExt::setMassAndUpdateInertia(*DynamicActor, MassKg);
		DynamicActor->setCMassLocalPose(PxTransform(ToPxVec3(OwnerComponent->GetCenterOfMass())));
	}

	InitParams.Scene->addActor(*Actor);
}

void FBodyInstance::TermBody(const FBodyInstanceInitParams& InitParams)
{
	if (!Actor)
	{
		return;
	}

	Actor->userData = nullptr;
	if (InitParams.Scene)
	{
		InitParams.Scene->removeActor(*Actor);
	}
	Actor->release();
	Actor = nullptr;
}

void FBodyInstance::CreateShapesFromBodySetup(const FBodyInstanceInitParams& InitParams)
{
	(void)InitParams;
	// Shape creation from UBodySetup::AggGeom
}

void FBodyInstance::SyncBodyToComponent()
{
	if (!Actor || !OwnerComponent)
	{
		return;
	}

	const PxTransform Pose = Actor->getGlobalPose();
	OwnerComponent->SetWorldLocation(ToFVector(Pose.p));
	OwnerComponent->SetRelativeRotation(ToFQuat(Pose.q).ToRotator());
}

void FBodyInstance::SyncComponentToBody()
{
	if (!Actor || !OwnerComponent)
	{
		return;
	}

	Actor->setGlobalPose(GetPxTransform(OwnerComponent));
}
