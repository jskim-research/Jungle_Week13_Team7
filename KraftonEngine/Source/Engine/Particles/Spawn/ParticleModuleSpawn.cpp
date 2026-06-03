#include "ParticleModuleSpawn.h"

#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

bool UParticleModuleSpawn::GetSpawnAmount(const FSpawnContext& Context, int32 Offset, float OldLeftover, float DeltaTime, int32& OutNumber, float& OutRate)
{
	OutRate = SpawnRate * SpawnRateScale;
	OutNumber = static_cast<int32>(DeltaTime * OutRate + OldLeftover);
	return true;
}

bool UParticleModuleSpawn::GetBurstCount(const FSpawnContext& Context, int32 Offset, float OldLeftover, float DeltaTime, int32& OutBurstCount)
{
	(void)Context;
	(void)Offset;
	(void)OldLeftover;
	(void)DeltaTime;

	// 실제 Burst firing은 FParticleEmitterInstance가 BurstFired 상태를 보면서 처리한다.
	// 이 함수는 SpawnModuleBase 인터페이스의 capability 응답만 담당한다.
	OutBurstCount = 0;
	return bProcessBurstList != 0;
}

float UParticleModuleSpawn::GetMaximumSpawnRate()
{
	return SpawnRate * SpawnRateScale;
}

float UParticleModuleSpawn::GetEstimatedSpawnRate()
{
	// 원래는 Distribution 데이터로 존재할 때 다른 계산 방식이 있는 거 같은데
	// 지금은 단순하게 float 데이터로 지정했기에 GetMaximumSpawnRate 과 반환값 같음
	return SpawnRate * SpawnRateScale;
}

int32 UParticleModuleSpawn::GetMaximumBurstCount()
{
	int32 MaxBurst = 0;
	for (int32 i = 0; i < static_cast<int32>(BurstList.size()); i++)
	{
		const int32 CountHigh = std::max(BurstList[i].Count, BurstList[i].CountLow);
		MaxBurst += std::max(0, CountHigh);
	}

	const float SafeScale = std::max(0.0f, BurstScale);
	return static_cast<int32>(std::ceil(static_cast<float>(MaxBurst) * SafeScale));
}

#if WITH_EDITOR
void UParticleModuleSpawn::PostEditChangeProperty(const FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UParticleModuleSpawn::Serialize(FArchive& Ar)
{
	UParticleModule::Serialize(Ar);

	int32 Version = 0;
	Ar << Version;

	Ar << SpawnRate;
	Ar << SpawnRateScale;
	Ar << BurstScale;
	Ar << BurstList;
}
