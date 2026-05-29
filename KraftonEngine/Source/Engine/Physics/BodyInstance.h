#pragma once

#include "Core/Types/CoreTypes.h"

class UBodySetup;
class UPrimitiveComponent;

namespace physx
{
	class PxMaterial;
	class PxPhysics;
	class PxRigidActor;
	class PxScene;
}

struct FBodyInstanceInitParams
{
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
};

struct FBodyInstance
{
	UPrimitiveComponent* OwnerComponent = nullptr;
	UBodySetup* BodySetup = nullptr;

	physx::PxRigidActor* Actor = nullptr;

	bool bSimulatePhysics = false;
	bool bEnableCollision = true;
	float Mass = 10.0f;

	void InitBody(UBodySetup* InBodySetup, UPrimitiveComponent* InOwnerComponent, const FBodyInstanceInitParams& InitParams);
	void TermBody(const FBodyInstanceInitParams& InitParams);

	void CreateShapesFromBodySetup(const FBodyInstanceInitParams& InitParams);

	void SyncBodyToComponent();
	void SyncComponentToBody();

	bool IsValidBodyInstance() const { return Actor != nullptr; }
};
