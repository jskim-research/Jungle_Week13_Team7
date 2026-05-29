#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"

class FPhysicsAssetEditorWidget : public FAssetEditorWidget
{
public:
	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Close() override;
	void Render(float DeltaTime) override;

	bool AllowsMultipleInstances() const override { return true; }

private:
	uint32 InstanceId = 0;
	FString WindowIdSuffix;
	bool bPendingClose = false;
};
