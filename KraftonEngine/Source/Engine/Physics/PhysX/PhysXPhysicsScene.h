#pragma once

#include "Physics/IPhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Core/Types/CoreTypes.h"
#include <vector>

class AActor;

// Forward declarations — PhysX types
namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxScene;
	class PxDefaultCpuDispatcher;
	class PxMaterial;
	class PxRigidActor;
	class PxRigidDynamic;
	class PxShape;
}

class FPhysXSimulationCallback;

// ============================================================
// FPhysXPhysicsScene — PhysX 4.1 기반 물리 시스템
//
// IPhysicsScene 인터페이스를 통해 Native와 교체 가능.
//
// 두 등록 경로가 공존한다.
// 1) Unreal-style: UStaticMeshComponent + UBodySetup → 컴포넌트당 FBodyInstance / PxRigidActor
// 2) Legacy compound: Shape component(Box/Sphere/Capsule) → 액터당 하나의 PxRigidActor에 shape 합침
// ============================================================
class FPhysXPhysicsScene : public IPhysicsScene
{
public:
	void Initialize(UWorld* InWorld) override;
	void Shutdown() override;

	void RegisterComponent(UPrimitiveComponent* Comp) override;
	void UnregisterComponent(UPrimitiveComponent* Comp) override;
	void RebuildBody(UPrimitiveComponent* Comp) override;

	void Tick(float DeltaTime) override;

	void AddForce(UPrimitiveComponent* Comp, const FVector& Force) override;
	void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) override;
	void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) override;

	FVector GetLinearVelocity(UPrimitiveComponent* Comp) const override;
	void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;
	FVector GetAngularVelocity(UPrimitiveComponent* Comp) const override;
	void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;

	void SetEnableGravity(UPrimitiveComponent* Comp, bool bEnable) override;

	void SetMass(UPrimitiveComponent* Comp, float Mass) override;
	float GetMass(UPrimitiveComponent* Comp) const override;
	void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) override;
	FVector GetCenterOfMass(UPrimitiveComponent* Comp) const override;

	bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const override;
	bool Sweep(const FVector& Start, const FVector& Dir, float MaxDist, const FCollisionShape& Shape, const FQuat& ShapeRot, FHitResult& OutHit, ECollisionChannel TraceChannel, const AActor* IgnoreActor) const override;
	bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const override;

private:
	UWorld* World = nullptr;

	// PhysX core objects
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;
	physx::PxMaterial* DefaultMaterial = nullptr;
	FPhysXSimulationCallback* EventCallback = nullptr;

	// Actor 단위 매핑 — 한 액터의 여러 컴포넌트가 같은 PxRigidActor에 shape로 합쳐진다.
	struct FBodyMapping
	{
		AActor* OwnerActor = nullptr;            // 키
		physx::PxRigidActor* Actor = nullptr;    // PhysX rigid (Dynamic/Static)
		UPrimitiveComponent* RootComp = nullptr; // 트랜스폼 동기화 기준 (Actor->RootComponent)
		TArray<UPrimitiveComponent*> Components; // 등록된 컴포넌트들 (shape 1:1 매칭)
	};
	std::vector<FBodyMapping> BodyMappings;

	// Unreal-style per-component bodies (UStaticMeshComponent + UBodySetup path).
	TArray<UPrimitiveComponent*> BodyInstanceComponents;

	bool bSharedPhysXAcquired = false;
	bool bShutdownComplete = true;

	// 내부 헬퍼
	struct FBodyInstanceInitParams MakeBodyInstanceInitParams() const;
	void ReleaseBodyInstances();
	void PruneInvalidBodyInstanceComponents();
	bool ShouldIgnoreActorForQuery(const physx::PxRigidActor* Actor, const AActor* IgnoreActor) const;
	physx::PxRigidDynamic* GetDynamicActorForComponent(UPrimitiveComponent* Comp) const;
	UPrimitiveComponent* GetMassReferenceComponent(UPrimitiveComponent* Comp) const;
	void ClearPhysXActorUserData(physx::PxRigidActor* Actor) const;
	void ReleaseBodyMappings();
	FBodyMapping* FindMappingByActor(AActor* OwnerActor);
	const FBodyMapping* FindMappingByActor(AActor* OwnerActor) const;
	FBodyMapping* FindMappingByComponent(UPrimitiveComponent* Comp);
	const FBodyMapping* FindMappingByComponent(UPrimitiveComponent* Comp) const;

	// Comp의 geometry를 Mapping의 PxRigidActor에 shape로 추가. 실패 시 nullptr.
	physx::PxShape* AddShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp);
	// Mapping의 actor에서 Comp에 매칭된 shape를 detach.
	void DetachShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp);
};
