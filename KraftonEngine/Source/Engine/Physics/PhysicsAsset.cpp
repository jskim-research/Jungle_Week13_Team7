#include "PhysicsAsset.h"
#include "PhysicsConstraintTemplate.h"

static bool IsValidIndex(int32 Index, int32 Count)
{
	return Index >= 0 && Index < Count;
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

	USkeletalBodySetup* NewBodySetup = new USkeletalBodySetup();
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

	delete Removed;

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

	UPhysicsConstraintTemplate* NewConstraint = new UPhysicsConstraintTemplate();

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

	delete Removed;

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