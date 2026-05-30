#include "PhysicsAsset.h"
#include "PhysicsConstraintTemplate.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"

static bool IsValidIndex(int32 Index, int32 Count)
{
	return Index >= 0 && Index < Count;
}

namespace
{
	static constexpr int32 PhysicsAssetSerializeVersion = 2;

	void SerializeTransform(FArchive& Ar, FTransform& Transform)
	{
		Ar << Transform.Location;
		Ar << Transform.Rotation;
		Ar << Transform.Scale;
	}

	void SerializeConstraintMotion(FArchive& Ar, EConstraintMotion& Motion)
	{
		uint8 Value = static_cast<uint8>(Motion);
		Ar << Value;
		if (Ar.IsLoading())
		{
			Motion = static_cast<EConstraintMotion>(Value);
		}
	}

	void SerializeConstraintInstance(FArchive& Ar, FConstraintInstance& Instance)
	{
		Ar << Instance.ConstraintName;
		Ar << Instance.ParentBoneName;
		Ar << Instance.ChildBoneName;
		SerializeTransform(Ar, Instance.ParentFrame);
		SerializeTransform(Ar, Instance.ChildFrame);
		Ar << Instance.bDisableCollision;
		SerializeConstraintMotion(Ar, Instance.LinearXMotion);
		SerializeConstraintMotion(Ar, Instance.LinearYMotion);
		SerializeConstraintMotion(Ar, Instance.LinearZMotion);
		Ar << Instance.LinearLimitSize;
		SerializeConstraintMotion(Ar, Instance.Swing1Motion);
		SerializeConstraintMotion(Ar, Instance.Swing2Motion);
		SerializeConstraintMotion(Ar, Instance.TwistMotion);
		Ar << Instance.Swing1LimitDegrees;
		Ar << Instance.Swing2LimitDegrees;
		Ar << Instance.TwistLimitMinDegrees;
		Ar << Instance.TwistLimitMaxDegrees;
	}
}

void UPhysicsAsset::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);

	for (USkeletalBodySetup* BodySetup : SkeletalBodySetups)
	{
		Collector.AddReferencedObject(BodySetup);
	}

	for (UPhysicsConstraintTemplate* ConstraintSetup : ConstraintSetups)
	{
		Collector.AddReferencedObject(ConstraintSetup);
	}
}

void UPhysicsAsset::Serialize(FArchive& Ar)
{
	int32 Version = PhysicsAssetSerializeVersion;
	Ar << Version;

	if (Version >= 2)
	{
		Ar << PreviewSkeletalMeshPath;
		if (Ar.IsLoading() && PreviewSkeletalMeshPath.empty())
		{
			PreviewSkeletalMeshPath = "None";
		}
	}
	else if (Ar.IsLoading())
	{
		PreviewSkeletalMeshPath = "None";
	}

	if (Ar.IsLoading())
	{
		for (USkeletalBodySetup* BodySetup : SkeletalBodySetups)
		{
			if (BodySetup)
			{
				UObjectManager::Get().DestroyObject(BodySetup);
			}
		}
		for (UPhysicsConstraintTemplate* ConstraintSetup : ConstraintSetups)
		{
			if (ConstraintSetup)
			{
				UObjectManager::Get().DestroyObject(ConstraintSetup);
			}
		}
		SkeletalBodySetups.clear();
		ConstraintSetups.clear();
		BoundsBodies.clear();
		BodySetupIndexMap.clear();
		CollisionDisableTable.clear();
	}

	uint32 BodyCount = static_cast<uint32>(SkeletalBodySetups.size());
	Ar << BodyCount;
	if (Ar.IsLoading())
	{
		SkeletalBodySetups.reserve(BodyCount);
	}
	for (uint32 BodyIndex = 0; BodyIndex < BodyCount; ++BodyIndex)
	{
		bool bValid = Ar.IsSaving() && SkeletalBodySetups[BodyIndex] != nullptr;
		Ar << bValid;
		if (!bValid)
		{
			if (Ar.IsLoading())
			{
				SkeletalBodySetups.push_back(nullptr);
			}
			continue;
		}

		USkeletalBodySetup* BodySetup = Ar.IsSaving()
			? SkeletalBodySetups[BodyIndex]
			: UObjectManager::Get().CreateObject<USkeletalBodySetup>(this);
		BodySetup->SerializeCollision(Ar);
		Ar << BodySetup->bSkipScaleFromAnimation;
		if (Ar.IsLoading())
		{
			SkeletalBodySetups.push_back(BodySetup);
		}
	}

	uint32 ConstraintCount = static_cast<uint32>(ConstraintSetups.size());
	Ar << ConstraintCount;
	if (Ar.IsLoading())
	{
		ConstraintSetups.reserve(ConstraintCount);
	}
	for (uint32 ConstraintIndex = 0; ConstraintIndex < ConstraintCount; ++ConstraintIndex)
	{
		bool bValid = Ar.IsSaving() && ConstraintSetups[ConstraintIndex] != nullptr;
		Ar << bValid;
		if (!bValid)
		{
			if (Ar.IsLoading())
			{
				ConstraintSetups.push_back(nullptr);
			}
			continue;
		}

		UPhysicsConstraintTemplate* ConstraintSetup = Ar.IsSaving()
			? ConstraintSetups[ConstraintIndex]
			: UObjectManager::Get().CreateObject<UPhysicsConstraintTemplate>(this);
		FConstraintInstance Instance = ConstraintSetup->GetDefaultInstance();
		SerializeConstraintInstance(Ar, Instance);
		ConstraintSetup->SetDefaultInstance(Instance);
		if (Ar.IsLoading())
		{
			ConstraintSetups.push_back(ConstraintSetup);
		}
	}

	uint32 DisabledPairCount = static_cast<uint32>(CollisionDisableTable.size());
	Ar << DisabledPairCount;
	if (Ar.IsSaving())
	{
		for (auto& Pair : CollisionDisableTable)
		{
			FRigidBodyIndexPair BodyPair = Pair.first;
			bool bDisabled = Pair.second;
			Ar << BodyPair.BodyIndexA;
			Ar << BodyPair.BodyIndexB;
			Ar << bDisabled;
		}
	}
	else
	{
		for (uint32 Index = 0; Index < DisabledPairCount; ++Index)
		{
			int32 BodyIndexA = -1;
			int32 BodyIndexB = -1;
			bool bDisabled = false;
			Ar << BodyIndexA;
			Ar << BodyIndexB;
			Ar << bDisabled;
			if (bDisabled)
			{
				CollisionDisableTable[FRigidBodyIndexPair(BodyIndexA, BodyIndexB)] = true;
			}
		}

		UpdateBodySetupIndexMap();
		UpdateBoundsBodiesArray();
	}
}

USkeletalBodySetup* UPhysicsAsset::GetBodySetup(int32 BodyIndex)
{
	if (!IsValidIndex(BodyIndex, static_cast<int32>(SkeletalBodySetups.size())))
	{
		return nullptr;
	}

	return SkeletalBodySetups[BodyIndex];
}

const USkeletalBodySetup* UPhysicsAsset::GetBodySetup(int32 BodyIndex) const
{
	if (!IsValidIndex(BodyIndex, static_cast<int32>(SkeletalBodySetups.size())))
	{
		return nullptr;
	}

	return SkeletalBodySetups[BodyIndex];
}

int32 UPhysicsAsset::FindBodyIndex(FName BodyName) const
{
	if (BodyName.IsNone())
	{
		return -1;
	}

	auto It = BodySetupIndexMap.find(BodyName);
	if (It == BodySetupIndexMap.end())
	{
		return -1;
	}

	return It->second;
}

USkeletalBodySetup* UPhysicsAsset::FindBodySetup(FName BodyName)
{
	const int32 BodyIndex = FindBodyIndex(BodyName);
	return GetBodySetup(BodyIndex);
}

const USkeletalBodySetup* UPhysicsAsset::FindBodySetup(FName BodyName) const
{
	const int32 BodyIndex = FindBodyIndex(BodyName);
	return GetBodySetup(BodyIndex);
}

USkeletalBodySetup* UPhysicsAsset::AddBodySetup(FName BoneName)
{
	if (BoneName.IsNone())
	{
		return nullptr;
	}

	if (USkeletalBodySetup* Existing = FindBodySetup(BoneName))
	{
		return Existing;
	}

	USkeletalBodySetup* NewBodySetup = UObjectManager::Get().CreateObject<USkeletalBodySetup>(this);
	NewBodySetup->SetBoneName(BoneName);

	SkeletalBodySetups.push_back(NewBodySetup);

	UpdateBodySetupIndexMap();
	UpdateBoundsBodiesArray();
	RefreshPhysicsAssetChange();

	return NewBodySetup;
}

bool UPhysicsAsset::RemoveBodySetup(FName BoneName)
{
	const int32 BodyIndex = FindBodyIndex(BoneName);
	return RemoveBodySetupAt(BodyIndex);
}

bool UPhysicsAsset::RemoveBodySetupAt(int32 BodyIndex)
{
	if (!IsValidIndex(BodyIndex, static_cast<int32>(SkeletalBodySetups.size())))
	{
		return false;
	}

	// 이 Body에 연결된 Constraint 제거
	for (int32 ConstraintIndex = static_cast<int32>(ConstraintSetups.size()) - 1; ConstraintIndex >= 0; --ConstraintIndex)
	{
		UPhysicsConstraintTemplate* Constraint = ConstraintSetups[ConstraintIndex];
		if (!Constraint)
		{
			continue;
		}

		const FName RemovedBodyName = SkeletalBodySetups[BodyIndex]->GetBoneName();

		const bool bConnected =
			Constraint->GetParentBoneName() == RemovedBodyName ||
			Constraint->GetChildBoneName() == RemovedBodyName;

		if (bConnected)
		{
			RemoveConstraintSetupAt(ConstraintIndex);
		}
	}

	USkeletalBodySetup* Removed = SkeletalBodySetups[BodyIndex];
	SkeletalBodySetups.erase(SkeletalBodySetups.begin() + BodyIndex);

	if (Removed)
	{
		UObjectManager::Get().DestroyObject(Removed);
	}

	// Body index가 밀렸으므로 index 기반 collision table은 무효화
	CollisionDisableTable.clear();

	UpdateBodySetupIndexMap();
	UpdateBoundsBodiesArray();
	RefreshPhysicsAssetChange();

	return true;
}

UPhysicsConstraintTemplate* UPhysicsAsset::GetConstraintSetup(int32 ConstraintIndex)
{
	if (!IsValidIndex(ConstraintIndex, static_cast<int32>(ConstraintSetups.size())))
	{
		return nullptr;
	}

	return ConstraintSetups[ConstraintIndex];
}

const UPhysicsConstraintTemplate* UPhysicsAsset::GetConstraintSetup(int32 ConstraintIndex) const
{
	if (!IsValidIndex(ConstraintIndex, static_cast<int32>(ConstraintSetups.size())))
	{
		return nullptr;
	}

	return ConstraintSetups[ConstraintIndex];
}

int32 UPhysicsAsset::FindConstraintIndex(FName ConstraintName) const
{
	if (ConstraintName.IsNone())
	{
		return -1;
	}

	for (int32 Index = 0; Index < static_cast<int32>(ConstraintSetups.size()); ++Index)
	{
		const UPhysicsConstraintTemplate* Constraint = ConstraintSetups[Index];
		if (!Constraint)
		{
			continue;
		}

		if (Constraint->GetConstraintName() == ConstraintName)
		{
			return Index;
		}
	}

	return -1;
}

UPhysicsConstraintTemplate* UPhysicsAsset::FindConstraintSetup(FName ConstraintName)
{
	const int32 ConstraintIndex = FindConstraintIndex(ConstraintName);
	return GetConstraintSetup(ConstraintIndex);
}

const UPhysicsConstraintTemplate* UPhysicsAsset::FindConstraintSetup(FName ConstraintName) const
{
	const int32 ConstraintIndex = FindConstraintIndex(ConstraintName);
	return GetConstraintSetup(ConstraintIndex);
}

void UPhysicsAsset::BodyFindConstraints(int32 BodyIndex, TArray<int32>& OutConstraintIndices) const
{
	OutConstraintIndices.clear();

	const USkeletalBodySetup* BodySetup = GetBodySetup(BodyIndex);
	if (!BodySetup)
	{
		return;
	}

	const FName BodyName = BodySetup->GetBoneName();

	for (int32 Index = 0; Index < static_cast<int32>(ConstraintSetups.size()); ++Index)
	{
		const UPhysicsConstraintTemplate* Constraint = ConstraintSetups[Index];
		if (!Constraint)
		{
			continue;
		}

		if (Constraint->GetParentBoneName() == BodyName ||
			Constraint->GetChildBoneName() == BodyName)
		{
			OutConstraintIndices.push_back(Index);
		}
	}
}

UPhysicsConstraintTemplate* UPhysicsAsset::AddConstraintSetup(
	FName ConstraintName,
	FName ParentBoneName,
	FName ChildBoneName)
{
	if (ConstraintName.IsNone() || ParentBoneName.IsNone() || ChildBoneName.IsNone())
	{
		return nullptr;
	}

	if (ParentBoneName == ChildBoneName)
	{
		return nullptr;
	}

	if (FindConstraintSetup(ConstraintName))
	{
		return nullptr;
	}

	if (FindBodyIndex(ParentBoneName) == -1 ||
		FindBodyIndex(ChildBoneName) == -1)
	{
		return nullptr;
	}

	UPhysicsConstraintTemplate* NewConstraint = UObjectManager::Get().CreateObject<UPhysicsConstraintTemplate>(this);

	NewConstraint->SetConstraintName(ConstraintName);
	NewConstraint->SetParentBoneName(ParentBoneName);
	NewConstraint->SetChildBoneName(ChildBoneName);

	FConstraintInstance& Instance = NewConstraint->GetDefaultInstance();
	Instance.ConstraintName = ConstraintName;
	Instance.ParentBoneName = ParentBoneName;
	Instance.ChildBoneName = ChildBoneName;

	ConstraintSetups.push_back(NewConstraint);

	RefreshPhysicsAssetChange();

	return NewConstraint;
}

bool UPhysicsAsset::RemoveConstraintSetup(FName ConstraintName)
{
	const int32 ConstraintIndex = FindConstraintIndex(ConstraintName);
	return RemoveConstraintSetupAt(ConstraintIndex);
}

bool UPhysicsAsset::RemoveConstraintSetupAt(int32 ConstraintIndex)
{
	if (!IsValidIndex(ConstraintIndex, static_cast<int32>(ConstraintSetups.size())))
	{
		return false;
	}

	UPhysicsConstraintTemplate* Removed = ConstraintSetups[ConstraintIndex];
	ConstraintSetups.erase(ConstraintSetups.begin() + ConstraintIndex);

	if (Removed)
	{
		UObjectManager::Get().DestroyObject(Removed);
	}

	RefreshPhysicsAssetChange();

	return true;
}

void UPhysicsAsset::DisableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	if (!IsValidIndex(BodyIndexA, static_cast<int32>(SkeletalBodySetups.size())) ||
		!IsValidIndex(BodyIndexB, static_cast<int32>(SkeletalBodySetups.size())) ||
		BodyIndexA == BodyIndexB)
	{
		return;
	}

	CollisionDisableTable[FRigidBodyIndexPair(BodyIndexA, BodyIndexB)] = true;

	RefreshPhysicsAssetChange();
}

void UPhysicsAsset::EnableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	if (!IsValidIndex(BodyIndexA, static_cast<int32>(SkeletalBodySetups.size())) ||
		!IsValidIndex(BodyIndexB, static_cast<int32>(SkeletalBodySetups.size())) ||
		BodyIndexA == BodyIndexB)
	{
		return;
	}

	CollisionDisableTable.erase(FRigidBodyIndexPair(BodyIndexA, BodyIndexB));

	RefreshPhysicsAssetChange();
}

bool UPhysicsAsset::IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const
{
	if (!IsValidIndex(BodyIndexA, static_cast<int32>(SkeletalBodySetups.size())) ||
		!IsValidIndex(BodyIndexB, static_cast<int32>(SkeletalBodySetups.size())) ||
		BodyIndexA == BodyIndexB)
	{
		return false;
	}

	const FRigidBodyIndexPair Pair(BodyIndexA, BodyIndexB);
	return CollisionDisableTable.find(Pair) == CollisionDisableTable.end();
}

void UPhysicsAsset::UpdateBodySetupIndexMap()
{
	BodySetupIndexMap.clear();

	for (int32 Index = 0; Index < static_cast<int32>(SkeletalBodySetups.size()); ++Index)
	{
		USkeletalBodySetup* BodySetup = SkeletalBodySetups[Index];
		if (!BodySetup)
		{
			continue;
		}

		const FName BoneName = BodySetup->GetBoneName();
		if (BoneName.IsNone())
		{
			continue;
		}

		BodySetupIndexMap[BoneName] = Index;
	}
}

void UPhysicsAsset::UpdateBoundsBodiesArray()
{
	BoundsBodies.clear();

	for (int32 Index = 0; Index < static_cast<int32>(SkeletalBodySetups.size()); ++Index)
	{
		if (SkeletalBodySetups[Index])
		{
			BoundsBodies.push_back(Index);
		}
	}
}

void UPhysicsAsset::RefreshPhysicsAssetChange()
{
	// TODO:
	// 나중에 PhysicsAsset Editor / SkeletalMeshComponent 쪽에서
	// asset 변경 알림을 받을 수 있도록 delegate를 연결하면 됨.
}
