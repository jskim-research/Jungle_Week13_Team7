#include "Particles/Rotation/ParticleModuleMeshRotation.h"
#include "Object/GarbageCollection.h"

#include "Component/Primitive/ParticleSystemComponent.h"
#include "Particles/ParticleEmitterInstances.h"
#include "Serialization/Archive.h"

UParticleModuleMeshRotation::UParticleModuleMeshRotation()
	: bInheritParent(false)
{
	bSpawnModule = true;
	bUpdateModule = false;
}

void UParticleModuleMeshRotation::Spawn(const FSpawnContext& Context)
{
	FVector Rotation = StartRotation.GetValue(Context.Owner.EmitterTime, Context.GetDistributionData());
	if (bInheritParent && Context.Owner.Component)
	{
		const FVector ParentAffectedRotation = Context.Owner.Component->GetWorldRotation().ToVector();
		Rotation.X += ParentAffectedRotation.X / 360.0f;
		Rotation.Y += ParentAffectedRotation.Y / 360.0f;
		Rotation.Z += ParentAffectedRotation.Z / 360.0f;
	}

	FParticleMeshEmitterInstance* MeshInst = dynamic_cast<FParticleMeshEmitterInstance*>(&Context.Owner);
	if (MeshInst && MeshInst->MeshRotationOffset > 0)
	{
		FMeshRotationPayloadData* Payload = reinterpret_cast<FMeshRotationPayloadData*>(reinterpret_cast<uint8*>(Context.ParticleBase) + MeshInst->MeshRotationOffset);
		Payload->InitRotation = Rotation * 360.0f;
		Payload->Rotation += Payload->InitRotation;
		return;
	}

	// Sprite particle은 FBaseParticle::Rotation을 radians로 사용한다.
	// Distribution 값은 기존 MeshRotation과 동일하게 turn(1.0 == 360도) 기준으로 해석한다.
	if (Context.ParticleBase)
	{
		Context.ParticleBase->Rotation += Rotation.Z * (2.0f * 3.14159265358979323846f);
	}
}

void UParticleModuleMeshRotation::AddReferencedObjects(FReferenceCollector& Collector)
{
	UParticleModule::AddReferencedObjects(Collector);
	StartRotation.AddReferencedObjects(Collector);
}

void UParticleModuleMeshRotation::Serialize(FArchive& Ar)
{
	UParticleModule::Serialize(Ar);
	StartRotation.Serialize(Ar);
	bool InheritParent = bInheritParent;
	Ar << InheritParent;
	if (Ar.IsLoading())
	{
		bInheritParent = InheritParent;
	}
}
