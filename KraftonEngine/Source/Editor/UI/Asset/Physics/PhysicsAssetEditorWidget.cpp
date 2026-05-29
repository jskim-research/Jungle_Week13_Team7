#include "PhysicsAssetEditorWidget.h"

#include "Mesh/Skeletal/SkeletalMesh.h"

#include <imgui.h>
#include <string>

static uint32 GNextPhysicsAssetEditorInstanceId = 0;

bool FPhysicsAssetEditorWidget::CanEdit(UObject* Object) const
{
	// Temporary target until UPhysicsAsset exists. Keep this editor registered before
	// FMeshEditorWidget only while testing the Physics Asset Editor shell.
	return Object && Object->IsA<USkeletalMesh>();
}

void FPhysicsAssetEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);

	InstanceId = GNextPhysicsAssetEditorInstanceId++;
	WindowIdSuffix = "###PhysicsAssetEditor_" + std::to_string(InstanceId);
}

void FPhysicsAssetEditorWidget::Close()
{
	FAssetEditorWidget::Close();
	bPendingClose = false;
}

void FPhysicsAssetEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (bPendingClose)
	{
		Close();
		return;
	}

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	bool bWindowOpen = true;
	FString VisibleTitle = "Physics Asset Editor";

	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(EditedObject))
	{
		const FString& AssetPath = Mesh->GetAssetPathFileName();
		if (!AssetPath.empty())
		{
			VisibleTitle += " - ";
			VisibleTitle += AssetPath;
		}
	}

	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	const FString WindowTitle = VisibleTitle + WindowIdSuffix;

	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			bPendingClose = true;
		}
		return;
	}

	ImGui::TextUnformatted("Physics Asset Editor");
	ImGui::Separator();

	const float AvailableHeight = ImGui::GetContentRegionAvail().y;
	const float DetailsWidth = 300.0f;
	const float TreeWidth = 260.0f;
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;
	const float ViewportWidth = ImGui::GetContentRegionAvail().x - TreeWidth - DetailsWidth - Spacing * 2.0f;
	const float ClampedViewportWidth = ViewportWidth > 120.0f ? ViewportWidth : 120.0f;

	ImGui::BeginChild("PhysicsAssetTree", ImVec2(TreeWidth, AvailableHeight), true);
	ImGui::TextUnformatted("Skeleton / Bodies");
	ImGui::Separator();
	ImGui::TextDisabled("Bone tree placeholder");
	ImGui::Dummy(ImVec2(0.0f, 8.0f));
	ImGui::TextDisabled("BodySetup list placeholder");
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("PhysicsAssetViewport", ImVec2(ClampedViewportWidth, AvailableHeight), true);
	ImGui::TextUnformatted("Viewport");
	ImGui::Separator();
	ImGui::TextDisabled("Physics body debug preview placeholder");
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("PhysicsAssetDetails", ImVec2(DetailsWidth, AvailableHeight), true);
	ImGui::TextUnformatted("Details");
	ImGui::Separator();
	ImGui::TextDisabled("Body / Constraint details placeholder");
	ImGui::EndChild();

	ImGui::End();

	if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}
