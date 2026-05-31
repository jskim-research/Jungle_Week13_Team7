#pragma once

#include "Editor/UI/Asset/AssetEditorWidget.h"
#include "Editor/Viewport/Asset/MeshEditorViewportClient.h"
#include "Object/FName.h"

#include <memory>

namespace ax { namespace NodeEditor { struct EditorContext; } }

class UPhysicsAsset;
class USkeletalMesh;
class USkeletalBodySetup;
class UPhysicsConstraintTemplate;
class FPhysicsAssetPrimitiveGizmoTarget;
class FPhysicsAssetSolidPreviewComponent;
struct FSkeletalMesh;
struct ImVec2;

enum class EPhysicsAssetEditorSelectionType : uint8
{
	None,
	Bone,
	Body,
	Constraint
};

enum class EPhysicsAssetPrimitiveType : uint8
{
	None,
	Box,
	Sphere,
	Capsule
};

class FPhysicsAssetEditorWidget : public FAssetEditorWidget
{
	friend class FPhysicsAssetPrimitiveGizmoTarget;
	friend class FPhysicsAssetSolidPreviewComponent;

public:
	FPhysicsAssetEditorWidget();
	~FPhysicsAssetEditorWidget() override;

	bool CanEdit(UObject* Object) const override;
	bool IsEditingObject(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;
	void Render(float DeltaTime) override;
	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	bool AllowsMultipleInstances() const override { return true; }

private:
	void ResolveEditingObjects(UObject* Object);
	void RenderTreePanel();
	void RenderBodyTree();
	void RenderBodyTreeRow(int32 BodyIndex);
	void RenderConstraintGraph();
	void RenderViewportPanel();
	void RenderViewportPanel(ImVec2 Size);
	void RenderDetailsPanel();
	void RenderBoneTree(const FSkeletalMesh* MeshAsset, int32 BoneIndex);
	void RenderBodyDetails();
	void RenderConstraintDetails();
	void RenderPreviewToolbar();
	bool SaveAsset();

	void SelectBone(int32 BoneIndex);
	void SelectBody(int32 BodyIndex);
	void SelectConstraint(int32 ConstraintIndex);
	void SelectPrimitive(EPhysicsAssetPrimitiveType PrimitiveType, int32 PrimitiveIndex);
	void SelectFirstPrimitiveForBody(USkeletalBodySetup* BodySetup);

	void AddBodyForSelectedBone();
	void RemoveSelectedBody();
	void AddConstraintToParentBody();
	void RemoveSelectedConstraint();

	int32 FindBoneIndexByName(const FSkeletalMesh* MeshAsset, FName BoneName) const;
	int32 FindParentBodyIndexForBone(const FSkeletalMesh* MeshAsset, int32 BoneIndex) const;
	FName MakeUniqueConstraintName(FName ParentBoneName, FName ChildBoneName) const;
	FVector GetBodyWorldLocation(const USkeletalBodySetup* BodySetup) const;
	FQuat GetBodyWorldRotation(const USkeletalBodySetup* BodySetup) const;
	FVector BodyLocalToWorld(const USkeletalBodySetup* BodySetup, const FVector& LocalPosition) const;
	FVector WorldDeltaToBodyLocal(const USkeletalBodySetup* BodySetup, const FVector& WorldDelta) const;

	void CreatePreviewWorld();
	void DestroyPreviewWorld();
	void InitializeConstraintGraphEditor();
	void DestroyConstraintGraphEditor();
	void SyncPreviewSelection();
	void SyncPrimitiveGizmo();
	void UpdateSolidPreview();
	void RenderPhysicsDebug();
	void DrawBodySetupDebug(const USkeletalBodySetup* BodySetup, bool bSelected);
	void DrawConstraintDebug(const UPhysicsConstraintTemplate* Constraint, bool bSelected);

private:
	FMeshEditorViewportClient ViewportClient;
	ax::NodeEditor::EditorContext* ConstraintGraphContext = nullptr;
	std::unique_ptr<FPhysicsAssetPrimitiveGizmoTarget> PrimitiveGizmoTarget;
	FPhysicsAssetSolidPreviewComponent* SolidPreviewComponent = nullptr;

	USkeletalMesh* EditingMesh = nullptr;
	UPhysicsAsset* EditingPhysicsAsset = nullptr;

	EPhysicsAssetEditorSelectionType SelectionType = EPhysicsAssetEditorSelectionType::None;
	int32 SelectedBoneIndex = -1;
	int32 SelectedBodyIndex = -1;
	int32 SelectedConstraintIndex = -1;
	EPhysicsAssetPrimitiveType SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
	int32 SelectedPrimitiveIndex = -1;

	uint32 InstanceId = 0;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;
	bool bPendingClose = false;
	bool bShowBones = true;
	bool bShowBodies = true;
	bool bShowSolidBodies = true;
	bool bShowConstraints = true;
	bool bConstraintGraphLayoutDirty = true;
	uint64 ConstraintGraphTopologyHash = 0;
	float TreePanelWidth = 300.0f;
	float DetailsPanelWidth = 320.0f;
};
