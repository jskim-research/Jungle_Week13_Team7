#include "Physics/PhysX/PhysXPhysicsScene.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Physics/BodySetup/BodySetup.h"
#include "Physics/PhysX/PhysXShapeUtils.h"
#include "Math/Quat.h"
#include "Object/Object.h"  // IsAliveObject
#include "Core/Logging/Log.h"

#include <algorithm>

// PhysX headers
#include <PxPhysicsAPI.h>

using namespace physx;

// ============================================================
// PhysX Error Callback
// ============================================================
class FPhysXErrorCallback : public PxErrorCallback
{
public:
	void reportError(PxErrorCode::Enum code, const char* message,
		const char* file, int line) override
	{
		const char* severity = "Info";
		if (code == PxErrorCode::eABORT || code == PxErrorCode::eOUT_OF_MEMORY)
			severity = "Fatal";
		else if (code == PxErrorCode::eINTERNAL_ERROR || code == PxErrorCode::eINVALID_OPERATION)
			severity = "Error";
		else if (code == PxErrorCode::eINVALID_PARAMETER || code == PxErrorCode::ePERF_WARNING)
			severity = "Warning";
		else if (code == PxErrorCode::eDEBUG_WARNING)
			severity = "Warning";

		UE_LOG("[PhysX %s] %s (%s:%d)", severity, message, file, line);
	}
};

static FPhysXErrorCallback GPhysXErrorCallback;

namespace
{
	bool ResolvePhysXRaycastTarget(const PxRaycastHit& Block, FHitResult& OutHit)
	{
		if (Block.shape && Block.shape->userData)
		{
			UPrimitiveComponent* HitComponent = static_cast<UPrimitiveComponent*>(Block.shape->userData);
			if (!IsValid(HitComponent))
			{
				return false;
			}

			OutHit.HitComponent = HitComponent;

			AActor* HitActor = HitComponent->GetOwner();
			if (IsValid(HitActor))
			{
				OutHit.HitActor = HitActor;
			}

			return true;
		}

		if (Block.actor && Block.actor->userData)
		{
			AActor* HitActor = static_cast<AActor*>(Block.actor->userData);
			if (!IsValid(HitActor))
			{
				return false;
			}

			OutHit.HitActor = HitActor;
			return true;
		}

		return false;
	}

	bool ShouldUseBodyInstancePath(UPrimitiveComponent* Comp)
	{
		if (!IsValid(Comp))
		{
			return false;
		}

		if (Cast<UBoxComponent>(Comp) || Cast<USphereComponent>(Comp) || Cast<UCapsuleComponent>(Comp))
		{
			return true;
		}

		UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Comp);
		if (!StaticMeshComp)
		{
			return false;
		}

		UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();
		if (!StaticMesh)
		{
			return false;
		}

		const UBodySetup* BodySetup = StaticMesh->GetBodySetup();
		return BodySetup && BodySetup->HasSimpleCollision();
	}

	bool ShouldIgnoreActorForQuery(
		const PxRigidActor* Actor,
		const AActor* IgnoreActor,
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents)
	{
		if (!IgnoreActor || !Actor)
		{
			return false;
		}

		if (Actor->userData == IgnoreActor)
		{
			return true;
		}

		for (UPrimitiveComponent* Comp : BodyInstanceComponents)
		{
			if (!IsValid(Comp))
			{
				continue;
			}

			const FBodyInstance* BodyInstance = Comp->GetBodyInstance();
			if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || BodyInstance->Actor != Actor)
			{
				continue;
			}

			if (Comp->GetOwner() == IgnoreActor)
			{
				return true;
			}
		}

		return false;
	}
}
static PxDefaultAllocator GPhysXAllocator;

// ============================================================
// PhysX Foundation/Physics 싱글턴
// PxCreateFoundation은 프로세스당 1회만 허용 — 복수 Scene에서 공유
// ============================================================
static PxFoundation* GSharedFoundation = nullptr;
static PxPhysics* GSharedPhysics = nullptr;
static int32 GSharedRefCount = 0;

static bool AcquireSharedPhysX(PxFoundation*& OutFoundation, PxPhysics*& OutPhysics)
{
	OutFoundation = nullptr;
	OutPhysics = nullptr;

	if (GSharedRefCount == 0)
	{
		GSharedFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
		if (!GSharedFoundation)
		{
			UE_LOG("[PhysX] Failed to create shared foundation");
			return false;
		}

		GSharedPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GSharedFoundation, PxTolerancesScale());
		if (!GSharedPhysics)
		{
			UE_LOG("[PhysX] Failed to create shared physics");
			GSharedFoundation->release();
			GSharedFoundation = nullptr;
			return false;
		}
	}

	++GSharedRefCount;
	OutFoundation = GSharedFoundation;
	OutPhysics = GSharedPhysics;
	return true;
}

static void ReleaseSharedPhysX()
{
	if (GSharedRefCount <= 0)
	{
		GSharedRefCount = 0;
		return;
	}

	--GSharedRefCount;
	if (GSharedRefCount == 0)
	{
		if (GSharedPhysics) { GSharedPhysics->release(); GSharedPhysics = nullptr; }
		if (GSharedFoundation) { GSharedFoundation->release(); GSharedFoundation = nullptr; }
	}
}

// ============================================================
// PhysX Simulation Event Callback
//
// PhysX 의 onContact / onTrigger 는 Scene->fetchResults(true) 진행 중에 호출되며,
// 그 안에서 직접 게임 측 핸들러(NotifyComponentHit 등)를 호출하면 핸들러가
// World->DestroyActor 같은 scene-mutating 작업을 해서 fetchResults 와 겹쳐 크래쉬한다.
//
// 따라서 콜백은 이벤트를 큐에 적재만 하고, FPhysXPhysicsScene::Tick 의 post-simulate
// 단계 끝에서 DispatchPendingEvents 가 한꺼번에 게임 측 Notify 를 호출한다. 이 시점은
// simulate/fetchResults 외부이므로 핸들러가 자유롭게 actor/component 를 추가/제거해도 안전.
// ============================================================
class FPhysXSimulationCallback : public PxSimulationEventCallback
{
public:
	struct FQueuedHit
	{
		UPrimitiveComponent* Self      = nullptr;  // Notify 가 호출되는 대상
		UPrimitiveComponent* Other     = nullptr;
		FVector              NormalImpulse{0,0,0};
		FHitResult           Hit;
		bool                 bBegin = true;       // false = end
	};

	struct FQueuedTrigger
	{
		UPrimitiveComponent* Self  = nullptr;
		UPrimitiveComponent* Other = nullptr;
		bool                 bBegin = true;        // false = end
	};

	// Block 접촉 → 큐에 적재
	void onContact(const PxContactPairHeader& PairHeader,
		const PxContactPair* Pairs, PxU32 Count) override
	{
		if (PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_0
			|| PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_1)
			return;

		for (PxU32 i = 0; i < Count; ++i)
		{
			const PxContactPair& CP = Pairs[i];
			const bool bBegin = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
			const bool bEnd = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);
			if (!bBegin && !bEnd) continue;

			auto* CompA = CP.shapes[0] ? static_cast<UPrimitiveComponent*>(CP.shapes[0]->userData) : nullptr;
			auto* CompB = CP.shapes[1] ? static_cast<UPrimitiveComponent*>(CP.shapes[1]->userData) : nullptr;
			if (!CompA || !CompB) continue;

			if (bEnd)
			{
				FQueuedHit A;
				A.Self = CompA;
				A.Other = CompB;
				A.bBegin = false;
				PendingHits.push_back(A);

				FQueuedHit B;
				B.Self = CompB;
				B.Other = CompA;
				B.bBegin = false;
				PendingHits.push_back(B);
				continue;
			}

			// Contact point — 큐 dispatch 시점에 PxContactPair 가 이미 무효이므로 여기서 모두 추출.
			PxContactPairPoint ContactPoints[1];
			PxU32 NumPoints = CP.extractContacts(ContactPoints, 1);

			FVector ContactPos(0, 0, 0);
			FVector ContactNormal(0, 0, 1);
			float Penetration = 0.0f;

			if (NumPoints > 0)
			{
				ContactPos    = FVector(ContactPoints[0].position.x, ContactPoints[0].position.y, ContactPoints[0].position.z);
				ContactNormal = FVector(ContactPoints[0].normal.x,   ContactPoints[0].normal.y,   ContactPoints[0].normal.z);
				Penetration   = ContactPoints[0].separation; // 음수 = 관통
			}

			const FVector NormalImpulse = ContactNormal * Penetration;

			FQueuedHit A;
			A.Self                = CompA;
			A.Other               = CompB;
			A.NormalImpulse       = NormalImpulse;
			A.Hit.bHit            = true;
			A.Hit.HitComponent    = CompB;
			A.Hit.HitActor        = CompB->GetOwner();
			A.Hit.WorldHitLocation= ContactPos;
			A.Hit.ImpactNormal    = ContactNormal;
			A.Hit.WorldNormal     = ContactNormal;
			A.Hit.PenetrationDepth= -Penetration;
			PendingHits.push_back(A);

			FQueuedHit B;
			B.Self                 = CompB;
			B.Other                = CompA;
			B.NormalImpulse        = NormalImpulse * -1.0f;
			B.Hit.bHit             = true;
			B.Hit.HitComponent     = CompA;
			B.Hit.HitActor         = CompA->GetOwner();
			B.Hit.WorldHitLocation = ContactPos;
			B.Hit.ImpactNormal     = ContactNormal * -1.0f;
			B.Hit.WorldNormal      = ContactNormal * -1.0f;
			B.Hit.PenetrationDepth = -Penetration;
			PendingHits.push_back(B);
		}
	}

	// Trigger 진입/이탈 → 큐에 적재
	void onTrigger(PxTriggerPair* Pairs, PxU32 Count) override
	{
		for (PxU32 i = 0; i < Count; ++i)
		{
			const PxTriggerPair& TP = Pairs[i];

			if (TP.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
				continue;

			auto* TriggerComp = TP.triggerShape ? static_cast<UPrimitiveComponent*>(TP.triggerShape->userData) : nullptr;
			auto* OtherComp   = TP.otherShape   ? static_cast<UPrimitiveComponent*>(TP.otherShape->userData)   : nullptr;
			if (!TriggerComp || !OtherComp) continue;

			const bool bBegin = (TP.status == PxPairFlag::eNOTIFY_TOUCH_FOUND);
			const bool bEnd   = (TP.status == PxPairFlag::eNOTIFY_TOUCH_LOST);
			if (!bBegin && !bEnd) continue;

			if (TriggerComp->GetGenerateOverlapEvents())
			{
				PendingTriggers.push_back({ TriggerComp, OtherComp, bBegin });
			}
			if (OtherComp->GetGenerateOverlapEvents())
			{
				PendingTriggers.push_back({ OtherComp, TriggerComp, bBegin });
			}
		}
	}

	// FPhysXPhysicsScene::Tick 끝에서 호출. simulate/fetchResults 바깥이므로 핸들러가
	// 자유롭게 World->DestroyActor / SpawnActor / RegisterComponent 호출 가능.
	// 핸들러 도중 다른 컴포넌트가 destroy되는 경우 대비해 dispatch 직전에 IsAliveObject
	// 검증 — destroy된 포인터를 만지지 않는다.
	void DispatchPendingEvents()
	{
		// move-out — dispatch 도중 새 이벤트가 큐에 들어오는 일은 없지만, 안전하게 swap 후 처리.
		std::vector<FQueuedHit> HitsToDispatch;
		HitsToDispatch.swap(PendingHits);
		std::vector<FQueuedTrigger> TriggersToDispatch;
		TriggersToDispatch.swap(PendingTriggers);

		for (FQueuedHit& E : HitsToDispatch)
		{
			if (!IsValid(E.Self) || !IsValid(E.Other)) continue;
			AActor* OtherActor = E.Other->GetOwner();
			if (!IsValid(OtherActor)) continue;
			if (E.bBegin)
			{
				E.Self->NotifyComponentHit(E.Self, OtherActor, E.Other, E.NormalImpulse, E.Hit);
			}
			else
			{
				E.Self->NotifyComponentEndHit(E.Self, OtherActor, E.Other);
			}
		}

		for (FQueuedTrigger& E : TriggersToDispatch)
		{
			if (!IsValid(E.Self) || !IsValid(E.Other)) continue;
			AActor* OtherActor = E.Other->GetOwner();
			if (!IsValid(OtherActor)) continue;
			if (E.bBegin)
			{
				FHitResult DummyHit;
				E.Self->NotifyComponentBeginOverlap(E.Self, OtherActor, E.Other, 0, false, DummyHit);
			}
			else
			{
				E.Self->NotifyComponentEndOverlap(E.Self, OtherActor, E.Other, 0);
			}
		}
	}

	void ClearPendingEvents()
	{
		PendingHits.clear();
		PendingTriggers.clear();
	}

	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

private:
	std::vector<FQueuedHit>     PendingHits;
	std::vector<FQueuedTrigger> PendingTriggers;
};

// ============================================================
// Transform 변환 유틸
// ============================================================
static PxVec3 ToPxVec3(const FVector& V)
{
	return PxVec3(V.X, V.Y, V.Z);
}

static PxQuat ToPxQuat(const FQuat& Q)
{
	return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
}

static FVector ToFVector(const PxVec3& V)
{
	return FVector(V.x, V.y, V.z);
}

static FQuat ToFQuat(const PxQuat& Q)
{
	return FQuat(Q.x, Q.y, Q.z, Q.w);
}

static PxTransform GetPxTransform(UPrimitiveComponent* Comp)
{
	FVector Pos = Comp->GetWorldLocation();
	FQuat Rot = Comp->GetWorldMatrix().ToQuat();
	return PxTransform(ToPxVec3(Pos), ToPxQuat(Rot));
}

// ============================================================
// Collision Filtering
// ============================================================
// filterData 레이아웃:
//   word0 = 자신의 ObjectType (ECollisionChannel)
//   word1 = Block 비트마스크 (해당 채널에 Block 응답인 비트)
//   word2 = Overlap 비트마스크 (해당 채널에 Overlap 응답인 비트)
//   word3 = 소유 액터 UUID — 같은 액터의 두 컴포넌트끼리 충돌을 무시하기 위함
//           (Native 측 O(N²) 루프의 `if (A->GetOwner() == B->GetOwner()) continue;` 가드와 동일 의미)
//           Owner가 없거나 UUID가 0이면 가드 미적용.

// PxFilterShader — 엔진의 채널/응답 매트릭스를 PhysX에서 처리
// 양쪽 모두 상대 채널에 대해 Block이면 물리 충돌, 한쪽이라도 Overlap이면 트리거, 그 외 무시
static PxFilterFlags KraftonFilterShader(
	PxFilterObjectAttributes attributes0, PxFilterData filterData0,
	PxFilterObjectAttributes attributes1, PxFilterData filterData1,
	PxPairFlags& pairFlags, const void* /*constantBlock*/, PxU32 /*constantBlockSize*/)
{
	// 같은 액터(같은 owner UUID)의 두 컴포넌트끼리는 충돌 무시.
	// Native 측 O(N²) 루프의 same-owner 가드와 동일 의미. 차량 차체-바퀴처럼
	// 한 액터가 여러 콜라이더를 가질 때 자기끼리 충돌 시뮬레이션되는 문제를 막는다.
	if (filterData0.word3 != 0 && filterData0.word3 == filterData1.word3)
	{
		return PxFilterFlag::eKILL;
	}

	// 트리거 처리 — 한쪽이라도 트리거면 오버랩 통지만
	if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
	{
		pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	PxU32 channelA = filterData0.word0; // A의 ObjectType
	PxU32 channelB = filterData1.word0; // B의 ObjectType

	// A가 B의 채널에 대해 Block인지, B가 A의 채널에 대해 Block인지
	bool bABlocksB = (filterData0.word1 & (1u << channelB)) != 0;
	bool bBBlocksA = (filterData1.word1 & (1u << channelA)) != 0;

	// 양쪽 모두 Block → 물리 충돌 + contact 콜백
	if (bABlocksB && bBBlocksA)
	{
		pairFlags = PxPairFlag::eCONTACT_DEFAULT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST
			| PxPairFlag::eNOTIFY_CONTACT_POINTS;
		return PxFilterFlag::eDEFAULT;
	}

	// 한쪽이라도 Overlap → 겹침 감지만 (물리적 밀어내기 없음).
	// 일반적으로 이 케이스는 위 trigger shape 분기에서 이미 처리되지만, 등록 시점에
	// trigger flag로 분류되지 않은 simulation shape pair인데 응답이 Overlap인 경우의
	// 안전망. eSOLVE_CONTACT 명시 제외 + eDETECT_DISCRETE_CONTACT + NOTIFY로 detection만.
	bool bAOverlapsB = (filterData0.word2 & (1u << channelB)) != 0;
	bool bBOverlapsA = (filterData1.word2 & (1u << channelA)) != 0;

	if (bAOverlapsB || bBOverlapsA)
	{
		pairFlags = PxPairFlag::eDETECT_DISCRETE_CONTACT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST;
		return PxFilterFlag::eDEFAULT;
	}

	// Ignore — 쌍 완전히 제거
	return PxFilterFlag::eKILL;
}

// ============================================================
// Lifecycle
// ============================================================

void FPhysXPhysicsScene::Initialize(UWorld* InWorld)
{
	// 재초기화 경로가 들어와도 shared PhysX ref-count가 깨지지 않도록 먼저 정리한다.
	Shutdown();

	World = InWorld;
	bShutdownComplete = false;

	// Foundation / Physics — 프로세스 싱글턴 공유
	if (!AcquireSharedPhysX(Foundation, Physics))
	{
		UE_LOG("[PhysX] Failed to create Foundation or Physics");
		bShutdownComplete = true;
		World = nullptr;
		return;
	}
	bSharedPhysXAcquired = true;

	// CPU Dispatcher
	Dispatcher = PxDefaultCpuDispatcherCreate(2);
	if (!Dispatcher)
	{
		UE_LOG("[PhysX] Failed to create CPU dispatcher");
		Shutdown();
		return;
	}

	// Event callback
	EventCallback = new FPhysXSimulationCallback();

	// Scene
	PxSceneDesc SceneDesc(Physics->getTolerancesScale());
	SceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.81f); // Z-up, m 단위
	SceneDesc.cpuDispatcher = Dispatcher;
	SceneDesc.filterShader = KraftonFilterShader;
	SceneDesc.simulationEventCallback = EventCallback;
	Scene = Physics->createScene(SceneDesc);

	if (!Scene)
	{
		UE_LOG("[PhysX] Failed to create Scene");
		Shutdown();
		return;
	}

	// Default material (static friction, dynamic friction, restitution)
	DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f);
	if (!DefaultMaterial)
	{
		UE_LOG("[PhysX] Failed to create default material");
		Shutdown();
		return;
	}

	UE_LOG("[PhysX] Initialized successfully (Scene=%p)", Scene);
}

void FPhysXPhysicsScene::Shutdown()
{
	if (bShutdownComplete)
	{
		return;
	}
	bShutdownComplete = true;

	if (EventCallback)
	{
		EventCallback->ClearPendingEvents();
	}

	if (Scene)
	{
		Scene->setSimulationEventCallback(nullptr);
	}

	ReleaseBodyInstances();

	if (Scene)
	{
		Scene->release();
		Scene = nullptr;
	}

	if (DefaultMaterial)
	{
		DefaultMaterial->release();
		DefaultMaterial = nullptr;
	}

	if (EventCallback)
	{
		delete EventCallback;
		EventCallback = nullptr;
	}

	if (Dispatcher)
	{
		Dispatcher->release();
		Dispatcher = nullptr;
	}

	Foundation = nullptr;
	Physics = nullptr;
	World = nullptr;

	if (bSharedPhysXAcquired)
	{
		bSharedPhysXAcquired = false;
		ReleaseSharedPhysX();
	}
}

FBodyInstanceInitParams FPhysXPhysicsScene::MakeBodyInstanceInitParams() const
{
	FBodyInstanceInitParams Params;
	Params.Physics = Physics;
	Params.Scene = Scene;
	Params.DefaultMaterial = DefaultMaterial;
	return Params;
}

void FPhysXPhysicsScene::ReleaseBodyInstances()
{
	const FBodyInstanceInitParams Params = MakeBodyInstanceInitParams();
	for (UPrimitiveComponent* Comp : BodyInstanceComponents)
	{
		if (IsValid(Comp))
		{
			FBodyInstance* BodyInstance = Comp->GetBodyInstance();
			if (BodyInstance && BodyInstance->IsValidBodyInstance())
			{
				BodyInstance->TermBody(Params);
			}
		}
	}
	BodyInstanceComponents.clear();
}

void FPhysXPhysicsScene::PruneInvalidBodyInstanceComponents()
{
	BodyInstanceComponents.erase(
		std::remove_if(BodyInstanceComponents.begin(), BodyInstanceComponents.end(),
			[](UPrimitiveComponent* Comp)
			{
				return !IsValid(Comp) || !Comp->GetBodyInstance() || !Comp->GetBodyInstance()->IsValidBodyInstance();
			}),
		BodyInstanceComponents.end());
}

bool FPhysXPhysicsScene::ShouldIgnoreActorForQuery(const PxRigidActor* Actor, const AActor* IgnoreActor) const
{
	return ::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents);
}

PxRigidDynamic* FPhysXPhysicsScene::GetDynamicActorForComponent(UPrimitiveComponent* Comp) const
{
	if (!Comp)
	{
		return nullptr;
	}

	const FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || !BodyInstance->Actor)
	{
		return nullptr;
	}

	return BodyInstance->Actor->is<PxRigidDynamic>();
}

// ============================================================
// Body 관리 — UE-style per-component FBodyInstance
// ============================================================

void FPhysXPhysicsScene::RegisterComponent(UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp) || !Scene || !Physics || !DefaultMaterial) return;
	if (!ShouldUseBodyInstancePath(Comp)) return;
	if (Comp->GetBodyInstance()->IsValidBodyInstance()) return;

	UBodySetup* BodySetup = nullptr;
	if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Comp))
	{
		if (UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh())
		{
			BodySetup = StaticMesh->GetBodySetup();
		}
	}

	Comp->GetBodyInstance()->InitBody(
		BodySetup,
		Comp,
		MakeBodyInstanceInitParams(),
		FInitBodySpawnParams(Comp));
	if (Comp->GetBodyInstance()->IsValidBodyInstance())
	{
		BodyInstanceComponents.push_back(Comp);
	}
}

void FPhysXPhysicsScene::UnregisterComponent(UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp) || !Scene) return;

	if (!Comp->GetBodyInstance()->IsValidBodyInstance())
	{
		return;
	}

	Comp->GetBodyInstance()->TermBody(MakeBodyInstanceInitParams());
	BodyInstanceComponents.erase(
		std::remove(BodyInstanceComponents.begin(), BodyInstanceComponents.end(), Comp),
		BodyInstanceComponents.end());
}

void FPhysXPhysicsScene::RebuildBody(UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp) || !Scene) return;

	UnregisterComponent(Comp);
	if (ShouldUseBodyInstancePath(Comp))
	{
		RegisterComponent(Comp);
	}
}

// ============================================================
// Simulation
// ============================================================

void FPhysXPhysicsScene::Tick(float DeltaTime)
{
	if (bShutdownComplete || !Scene || DeltaTime <= 0.0f) return;

	// 어떤 이유로든 frame hitch (씬 로드 / 큰 OBJ 동기 로딩 / Alt-Tab / OS 스파이크) 가
	// 발생해도 PhysX 가 큰 dt 한 번에 적분해 차량·메테오가 콜리전을 뚫는 tunneling 사고를
	// 막기 위한 클램프. 0.1s 는 60 m/s 차량이 한 step 에 6m 이동 — 충돌 박스 내에서 풀림
	// 가능한 수준이고, 그 이상 hitch 면 게임을 느리게 진행시키더라도 안전이 우선.
	constexpr float MaxPhysicsDeltaTime = 0.1f;
	if (DeltaTime > MaxPhysicsDeltaTime)
	{
		DeltaTime = MaxPhysicsDeltaTime;
	}

	PruneInvalidBodyInstanceComponents();

	// ── Pre-simulate: Engine → PhysX Transform 동기화 (per-component) ──
	constexpr float TeleportPosThresholdSq = 1.0f;
	constexpr float TeleportRotThreshold = 0.99f;

	for (UPrimitiveComponent* Comp : BodyInstanceComponents)
	{
		if (!IsValid(Comp))
		{
			continue;
		}

		FBodyInstance* BodyInstance = Comp->GetBodyInstance();
		if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || !BodyInstance->Actor)
		{
			continue;
		}

		PxTransform NewPose = GetPxTransform(Comp);

		if (PxRigidDynamic* Dynamic = BodyInstance->Actor->is<PxRigidDynamic>())
		{
			if (Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
			{
				Dynamic->setKinematicTarget(NewPose);
			}
			else
			{
				PxTransform PxPose = Dynamic->getGlobalPose();
				PxVec3 dp = NewPose.p - PxPose.p;
				const float DistSq = dp.x * dp.x + dp.y * dp.y + dp.z * dp.z;
				const float QDot = std::abs(
					NewPose.q.x * PxPose.q.x + NewPose.q.y * PxPose.q.y +
					NewPose.q.z * PxPose.q.z + NewPose.q.w * PxPose.q.w);

				if (DistSq > TeleportPosThresholdSq || QDot < TeleportRotThreshold)
				{
					Dynamic->setGlobalPose(NewPose);
				}
			}
		}
		else if (BodyInstance->Actor->is<PxRigidStatic>())
		{
			BodyInstance->Actor->setGlobalPose(NewPose);
		}
	}

	// ── Simulate ──
	Scene->simulate(DeltaTime);
	Scene->fetchResults(true);

	// ── Post-simulate: PhysX → Engine Transform 동기화 (per-component) ──
	for (UPrimitiveComponent* Comp : BodyInstanceComponents)
	{
		if (!IsValid(Comp))
		{
			continue;
		}

		FBodyInstance* BodyInstance = Comp->GetBodyInstance();
		if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || !BodyInstance->Actor)
		{
			continue;
		}

		PxRigidDynamic* Dynamic = BodyInstance->Actor->is<PxRigidDynamic>();
		if (!Dynamic)
		{
			continue;
		}
		if (Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
		{
			continue;
		}

		BodyInstance->SyncBodyToComponent();
	}

	// ── Dispatch deferred contact/trigger events ──
	// onContact / onTrigger 는 fetchResults 안에서 fire 되므로 거기서 직접 게임 핸들러를
	// 부르면 핸들러의 World->DestroyActor 등이 PhysX scene 변경 타이밍과 겹쳐 크래쉬한다.
	// 그래서 큐에만 적재했고, 이 시점(simulate/fetchResults 외부)에서 한꺼번에 dispatch.
	if (EventCallback)
	{
		EventCallback->DispatchPendingEvents();
	}
}

bool FPhysXPhysicsScene::Sweep(const FVector& Start, const FVector& Dir, float MaxDist, const FCollisionShape& Shape, const FQuat& ShapeRot, FHitResult& OutHit, ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	if (!Scene) return false;

	// Raycast의 FChannelRaycastFilter와 동일한 로직, Sweep용으로 재선언
	struct FChannelSweepFilter : PxQueryFilterCallback
	{
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents;
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;
		FChannelSweepFilter(
			const TArray<UPrimitiveComponent*>& InBodyInstanceComponents,
			const AActor* InIgnoreActor,
			ECollisionChannel InChannel)
			: BodyInstanceComponents(InBodyInstanceComponents)
			, IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}
		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents))
				return PxQueryHitType::eNONE;

			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				if ((ShapeData.word1 & TraceBit) == 0)
					return PxQueryHitType::eNONE;
			}
			return PxQueryHitType::eBLOCK;
		}
		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	// FCollisionShape → PxGeometry 변환
	// GeometryHolder로 스택에 geometry 보관 (Sphere / Capsule / Box 지원)
	PxGeometryHolder GeomHolder;
	switch (Shape.ShapeType)
	{
	case ECollisionShape::Sphere:
		GeomHolder.storeAny(PxSphereGeometry(Shape.GetSphereRadius()));
		break;
	case ECollisionShape::Capsule:
		// PhysX capsule: halfHeight는 구 제외 실린더 절반 높이
		GeomHolder.storeAny(PxCapsuleGeometry(Shape.GetCapsuleRadius(),
			Shape.GetCapsuleHalfHeight() - Shape.GetCapsuleRadius()));
		break;
	case ECollisionShape::Box:
		GeomHolder.storeAny(PxBoxGeometry(ToPxVec3(Shape.GetExtent())));
		break;
	default:
		return false;
	}

	PxQuat SweepRot = ToPxQuat(ShapeRot);
	if (Shape.ShapeType == ECollisionShape::Capsule)
	{
		SweepRot = (SweepRot * PhysXShapeUtils::GetCapsuleAxisCorrectionQuat()).getNormalized();
	}

	const PxTransform PxStartPose(ToPxVec3(Start), SweepRot);
	const PxVec3 PxDir = ToPxVec3(Dir);

	PxSweepBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FChannelSweepFilter FilterCallback(BodyInstanceComponents, IgnoreActor, TraceChannel);

	bool bStatus = Scene->sweep(
		GeomHolder.any(),    // sweep geometry
		PxStartPose,         // 시작 pose (위치 + 회전)
		PxDir,               // 방향 (unit vector)
		MaxDist,             // 최대 거리
		Hit,
		PxHitFlag::eDEFAULT,
		FilterData,
		&FilterCallback
	);

	if (!bStatus || !Hit.hasBlock) return false;

	const PxSweepHit& Block = Hit.block;
	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = ToFVector(PxStartPose.p) + ToFVector(PxDir) * Block.distance;
	OutHit.ImpactNormal = ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	// distance == 0 이면 시작 지점에서 이미 겹침 (initial overlap)
	OutHit.bStartPenetrating = Block.distance <= 0.0f;

	if (Block.shape && Block.shape->userData)
	{
		OutHit.HitComponent = static_cast<UPrimitiveComponent*>(Block.shape->userData);
		OutHit.HitActor = OutHit.HitComponent->GetOwner();
	}
	else if (Block.actor && Block.actor->userData)
	{
		OutHit.HitActor = static_cast<AActor*>(Block.actor->userData);
	}

	return true;
}

// ============================================================
// Force / Torque
// ============================================================

void FPhysXPhysicsScene::AddForce(UPrimitiveComponent* Comp, const FVector& Force)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->addForce(ToPxVec3(Force));
}

void FPhysXPhysicsScene::AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	PxRigidBodyExt::addForceAtPos(*Dyn, ToPxVec3(Force), ToPxVec3(WorldLocation));
}

void FPhysXPhysicsScene::AddTorque(UPrimitiveComponent* Comp, const FVector& Torque)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->addTorque(ToPxVec3(Torque));
}

// ============================================================
// Velocity
// ============================================================

FVector FPhysXPhysicsScene::GetLinearVelocity(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return { 0, 0, 0 };
	return ToFVector(Dyn->getLinearVelocity());
}

void FPhysXPhysicsScene::SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->setLinearVelocity(ToPxVec3(Vel));
}

FVector FPhysXPhysicsScene::GetAngularVelocity(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return { 0, 0, 0 };
	return ToFVector(Dyn->getAngularVelocity());
}

void FPhysXPhysicsScene::SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->setAngularVelocity(ToPxVec3(Vel));
}

// ============================================================
// Gravity
// ============================================================

void FPhysXPhysicsScene::SetEnableGravity(UPrimitiveComponent* Comp, bool bEnable)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;

	Dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bEnable);
	Dyn->wakeUp();
}

// ============================================================
// Mass
// ============================================================

void FPhysXPhysicsScene::SetMass(UPrimitiveComponent* Comp, float NewMass)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;

	PxVec3 LocalCOM = ToPxVec3(Comp->GetCenterOfMass());
	PxRigidBodyExt::setMassAndUpdateInertia(*Dyn, NewMass, &LocalCOM);
}

float FPhysXPhysicsScene::GetMass(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return 1.0f;
	return Dyn->getMass();
}

void FPhysXPhysicsScene::SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->setCMassLocalPose(PxTransform(ToPxVec3(LocalOffset)));
}

FVector FPhysXPhysicsScene::GetCenterOfMass(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return { 0, 0, 0 };
	return ToFVector(Dyn->getCMassLocalPose().p);
}

// ============================================================
// Raycast
// ============================================================

bool FPhysXPhysicsScene::Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	if (!Scene) return false;

	// Channel + IgnoreActor 통합 filter.
	// shape의 queryFilterData는 SetupFilterData에서 word0=ObjectType, word1=Block 마스크.
	// 응답이 TraceChannel에 대해 Block(=word1의 해당 비트 set)인 shape만 hit으로 인정.
	// trigger flag가 set된 shape는 PhysX 측 query에서 자동 제외되므로 별도 처리 불필요.
	struct FChannelRaycastFilter : PxQueryFilterCallback
	{
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents;
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		FChannelRaycastFilter(
			const TArray<UPrimitiveComponent*>& InBodyInstanceComponents,
			const AActor* InIgnoreActor,
			ECollisionChannel InChannel)
			: BodyInstanceComponents(InBodyInstanceComponents)
			, IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents))
			{
				return PxQueryHitType::eNONE;
			}

			// shape의 응답이 TraceChannel에 대해 Block인지 확인.
			// (word1[TraceChannel 비트]가 set이면 Block 응답)
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				if ((ShapeData.word1 & TraceBit) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}

			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FChannelRaycastFilter FilterCallback(BodyInstanceComponents, IgnoreActor, TraceChannel);

	bool bStatus = Scene->raycast(ToPxVec3(Start), ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	if (!ResolvePhysXRaycastTarget(Block, OutHit))
	{
		return false;
	}

	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = ToFVector(Block.position);
	OutHit.ImpactNormal = ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	return true;
}

bool FPhysXPhysicsScene::RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	uint32 ObjectTypeMask, const AActor* IgnoreActor) const
{
	if (!Scene || ObjectTypeMask == 0) return false;

	// SetupFilterData (line ~322) 에서 word0 = ObjectType (채널 enum 값) 으로 set.
	// ObjectType 마스크 비트 검사로 hit 후보 필터.
	// Trigger flag shape 는 PhysX 측 query 단계에서 자동 제외.
	struct FObjectTypeRaycastFilter : PxQueryFilterCallback
	{
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents;
		const AActor* IgnoreActor = nullptr;
		PxU32 ObjectTypeMask = 0;

		FObjectTypeRaycastFilter(
			const TArray<UPrimitiveComponent*>& InBodyInstanceComponents,
			const AActor* InIgnoreActor,
			PxU32 InMask)
			: BodyInstanceComponents(InBodyInstanceComponents)
			, IgnoreActor(InIgnoreActor)
			, ObjectTypeMask(InMask)
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents))
			{
				return PxQueryHitType::eNONE;
			}
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				const PxU32 ShapeObjectBit = 1u << ShapeData.word0;
				if ((ShapeObjectBit & ObjectTypeMask) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}
			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FObjectTypeRaycastFilter FilterCallback(BodyInstanceComponents, IgnoreActor, ObjectTypeMask);

	bool bStatus = Scene->raycast(ToPxVec3(Start), ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	if (!ResolvePhysXRaycastTarget(Block, OutHit))
	{
		return false;
	}

	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = ToFVector(Block.position);
	OutHit.ImpactNormal = ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	return true;
}
