#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Object/Object.h"
#include "Asset/AssetRegistry.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"

class UActorComponent;
class AActor;

class FEditorPropertyWidget : public FEditorWidget
{
public:
	virtual void Render(float DeltaTime) override;
	void SetShowEditorOnlyComponents(bool bEnable) { bShowEditorOnlyComponents = bEnable; }
	bool IsShowingEditorOnlyComponents() const { return bShowEditorOnlyComponents; }

private:
	void RenameActor(AActor* PrimaryActor);
	void RenderComponentTree(AActor* Actor);
	void RenderSceneComponentNode(class USceneComponent* Comp);
	void RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderComponentProperties(AActor* Actor, const TArray<AActor*>& SelectedActors);
	void RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	bool RenderPropertyWidget(TArray<struct FPropertyValue>& Props, int32& Index, bool bDispatchChange = true, const FString& PropertyPath = {});
	bool RenderSoftObjectPropertyWidget(struct FPropertyValue& Prop);
	bool RenderEnumPropertyWidget(struct FPropertyValue& Prop);
	bool RenderStructPropertyWidget(struct FPropertyValue& Prop, bool bDispatchChange, const FString& PropertyPath);
	bool RenderArrayPropertyWidget(struct FPropertyValue& Prop, bool bDispatchChange, const FString& PropertyPath);
	void RenderCallInEditorFunctions(UObject* Object);

	void PropagatePropertyChange(const FString& PropName, const TArray<AActor*>& SelectedActors);

	void AddComponentToActor(AActor* Actor, UClass* ComponentClass);

	static FString OpenObjFileDialog();
	static FString OpenStaticMeshFileDialog();
	static FString OpenFbxFileDialog();

	UActorComponent* SelectedComponent = nullptr;
	AActor* LastSelectedActor = nullptr;
	bool bActorSelected = true; // true: Actor details, false: Component details
	bool bShowEditorOnlyComponents = false;

	char RenameBuffer[256] = {};
	bool bShowDuplicateWarning = false;
	FString PendingStaticMeshImportPath;
	FString* PendingStaticMeshImportTarget = nullptr;
	int32 PendingStaticFbxSkinnedMeshPolicy = 0;

	FFbxSceneImportDialogState SkeletalFbxImportDialog;

	// ##Details child scroll — property edit 시 partial render 후 다음 frame 에만 복원.
	// 매 frame SetScrollY 하면 ImGui 기본 scroll 과 충돌해 스크롤 시 떨림이 난다.
	float CachedDetailsScrollY = 0.0f;
	bool bPendingDetailsScrollRestore = false;
	bool bDeferDetailsScrollRestore = false;
	AActor* LastDetailsScrollActor = nullptr;
	UActorComponent* LastDetailsScrollComponent = nullptr;
	bool LastDetailsScrollActorMode = true;
};
