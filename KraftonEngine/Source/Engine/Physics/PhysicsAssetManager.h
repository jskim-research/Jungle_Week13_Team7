#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"
#include "Object/GarbageCollection.h"
#include "Asset/AssetRegistry.h"

class UPhysicsAsset;

class FPhysicsAssetManager : public TSingleton<FPhysicsAssetManager>, public FGCObject
{
	friend class TSingleton<FPhysicsAssetManager>;

public:
	UPhysicsAsset* Load(const FString& Path);
	UPhysicsAsset* Reload(const FString& Path);
	bool Save(UPhysicsAsset* Asset);
	UPhysicsAsset* CreatePhysicsAsset(const FString& Path);

	void RefreshAvailablePhysicsAssets();
	const TArray<FAssetListItem>& GetAvailablePhysicsAssetFiles() const
	{
		return AvailablePhysicsAssetFiles;
	}

	const char* GetReferencerName() const override { return "FPhysicsAssetManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	TMap<FString, UPhysicsAsset*> LoadedPhysicsAssets;
	TArray<FAssetListItem> AvailablePhysicsAssetFiles;
};
