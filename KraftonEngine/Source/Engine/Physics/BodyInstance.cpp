#include "Physics/BodyInstance.h"

#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Physics/BodySetup/BodySetup.h"
#include "Physics/PhysX/PhysXShapeUtils.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>

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

	float AbsScale(float Value)
	{
		return std::fabs(Value);
	}

	float MaxAbsScale(const FVector& Scale)
	{
		return (std::max)((std::max)(AbsScale(Scale.X), AbsScale(Scale.Y)), AbsScale(Scale.Z));
	}

	FVector ScaleVector(const FVector& Value, const FVector& Scale)
	{
		return FVector(Value.X * Scale.X, Value.Y * Scale.Y, Value.Z * Scale.Z);
	}

	PxTransform GetElemTransform(const FVector& Center, const FRotator& Rotation, const FVector& Scale)
	{
		return PxTransform(ToPxVec3(ScaleVector(Center, Scale)), ToPxQuat(Rotation.ToQuaternion()));
	}
}

FInitBodySpawnParams::FInitBodySpawnParams(const UPrimitiveComponent* PrimComp)
{
	if (PrimComp)
	{
		bStaticPhysics = !PrimComp->GetSimulatePhysics();
	}
}

namespace
{
	bool ResolveSimulatePhysics(
		const UBodySetup* InBodySetup,
		const UPrimitiveComponent* OwnerComponent,
		const FInitBodySpawnParams& SpawnParams)
	{
		if (SpawnParams.bStaticPhysics)
		{
			return false;
		}

		if (SpawnParams.bPhysicsTypeDeterminesSimulation && InBodySetup)
		{
			switch (InBodySetup->GetPhysicsType())
			{
			case EPhysicsType::Kinematic:
				return false;
			case EPhysicsType::Simulated:
				return true;
			case EPhysicsType::Default:
			default:
				break;
			}
		}

		return OwnerComponent ? OwnerComponent->GetSimulatePhysics() : false;
	}

	bool ResolveEnableCollision(const UBodySetup* InBodySetup, const UPrimitiveComponent* OwnerComponent)
	{
		if (InBodySetup && InBodySetup->GetCollisionResponse() == EBodyCollisionResponse::Disabled)
		{
			return false;
		}

		return OwnerComponent
			? (OwnerComponent->IsQueryCollisionEnabled() || OwnerComponent->IsPhysicsCollisionEnabled())
			: true;
	}
}

void FBodyInstance::InitBody(
	UBodySetup* InBodySetup,
	UPrimitiveComponent* InOwnerComponent,
	const FBodyInstanceInitParams& InitParams,
	const FInitBodySpawnParams& SpawnParams)
{
	TermBody(InitParams);

	BodySetup = InBodySetup;
	OwnerComponent = InOwnerComponent;
	Actor = nullptr;

	if (!OwnerComponent || !InitParams.Physics || !InitParams.Scene)
	{
		return;
	}

	bSimulatePhysics = ResolveSimulatePhysics(BodySetup, OwnerComponent, SpawnParams);
	bEnableCollision = ResolveEnableCollision(BodySetup, OwnerComponent);
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
		DynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !OwnerComponent->GetEnableGravity());
		DynamicActor->wakeUp();
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
	if (!Actor || !BodySetup || !OwnerComponent || !InitParams.DefaultMaterial || !bEnableCollision)
	{
		return;
	}

	const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
	const FVector Scale = OwnerComponent->GetWorldScale();
	const FVector AbsWorldScale(AbsScale(Scale.X), AbsScale(Scale.Y), AbsScale(Scale.Z));
	const float UniformScale = MaxAbsScale(Scale);

	for (const FKBoxElem& Elem : AggGeom.BoxElems)
	{
		const PxVec3 HalfExtents(
			(std::max)(0.001f, Elem.X * 0.5f * AbsWorldScale.X),
			(std::max)(0.001f, Elem.Y * 0.5f * AbsWorldScale.Y),
			(std::max)(0.001f, Elem.Z * 0.5f * AbsWorldScale.Z));

		PxShape* Shape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxBoxGeometry(HalfExtents),
			*InitParams.DefaultMaterial);
		if (!Shape)
		{
			continue;
		}

		Shape->setLocalPose(GetElemTransform(Elem.Center, Elem.Rotation, Scale));
		PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
	}

	for (const FKSphereElem& Elem : AggGeom.SphereElems)
	{
		const float Radius = (std::max)(0.001f, Elem.Radius * UniformScale);
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxSphereGeometry(Radius),
			*InitParams.DefaultMaterial);
		if (!Shape)
		{
			continue;
		}

		Shape->setLocalPose(PxTransform(ToPxVec3(ScaleVector(Elem.Center, Scale)), PxQuat(PxIdentity)));
		PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
	}

	for (const FKSphylElem& Elem : AggGeom.SphylElems)
	{
		const float Radius = (std::max)(0.001f, Elem.Radius * UniformScale);
		const float HalfLength = (std::max)(0.001f, Elem.Length * 0.5f * UniformScale);
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxCapsuleGeometry(Radius, HalfLength),
			*InitParams.DefaultMaterial);
		if (!Shape)
		{
			continue;
		}

		PxTransform LocalPose = GetElemTransform(Elem.Center, Elem.Rotation, Scale);
		LocalPose.q = LocalPose.q * PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));
		Shape->setLocalPose(LocalPose);
		PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
	}
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
