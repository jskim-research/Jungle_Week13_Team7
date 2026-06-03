#include "Particles/RotationRate/ParticleModuleMeshRotationRate.h"
#include "Object/GarbageCollection.h"

#include "Particles/ParticleEmitterInstances.h"
#include "Serialization/Archive.h"

UParticleModuleMeshRotationRate::UParticleModuleMeshRotationRate()
{
	bSpawnModule = true;
	bUpdateModule = true;
}

void UParticleModuleMeshRotationRate::Spawn(const FSpawnContext& Context)
{
	const FVector StartRate = StartRotationRate.GetValue(Context.Owner.EmitterTime, Context.GetDistributionData());

	FParticleMeshEmitterInstance* MeshInst = dynamic_cast<FParticleMeshEmitterInstance*>(&Context.Owner);
	if (MeshInst && MeshInst->MeshRotationOffset > 0)
	{
		FMeshRotationPayloadData* Payload = reinterpret_cast<FMeshRotationPayloadData*>(reinterpret_cast<uint8*>(Context.ParticleBase) + MeshInst->MeshRotationOffset);
		const FVector MeshStartRate = StartRate * 360.0f;
		Payload->RotationRateBase += MeshStartRate;
		Payload->RotationRate += MeshStartRate;
		return;
	}

	// Sprite particle은 radians/sec, distribution은 turn/sec 기준으로 사용한다.
	if (Context.ParticleBase)
	{
		const float SpriteRate = StartRate.Z * (2.0f * 3.14159265358979323846f);
		Context.ParticleBase->BaseRotationRate += SpriteRate;
		Context.ParticleBase->RotationRate += SpriteRate;
	}
}

void UParticleModuleMeshRotationRate::AddReferencedObjects(FReferenceCollector& Collector)
{
	UParticleModule::AddReferencedObjects(Collector);
	StartRotationRate.AddReferencedObjects(Collector);
}

void UParticleModuleMeshRotationRate::Serialize(FArchive& Ar)
{
	UParticleModule::Serialize(Ar);
	StartRotationRate.Serialize(Ar);
}
