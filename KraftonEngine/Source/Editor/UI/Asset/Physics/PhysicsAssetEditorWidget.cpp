#include "PhysicsAssetEditorWidget.h"

#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Mesh/MeshManager.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Physics/BodySetup/AggregateGeom.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/World.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Debug/GizmoComponent.h"
#include "Debug/DrawDebugHelpers.h"
#include "Gizmo/GizmoTransformTarget.h"
#include "Math/MathUtils.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"
#include "Materials/Material.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"
#include "Render/Shader/ShaderManager.h"
#include "imgui_node_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <string>

static uint32 GNextPhysicsAssetEditorInstanceId = 0;
namespace ed = ax::NodeEditor;

namespace
{
	inline ed::NodeId ToPhysicsNodeId(uint32 Id) { return static_cast<ed::NodeId>(Id); }
	inline ed::PinId ToPhysicsPinId(uint32 Id) { return static_cast<ed::PinId>(Id); }
	inline ed::LinkId ToPhysicsLinkId(uint32 Id) { return static_cast<ed::LinkId>(Id); }
	inline uint32 PhysicsNodeIdToU32(ed::NodeId Id) { return static_cast<uint32>(Id.Get()); }
	inline uint32 PhysicsPinIdToU32(ed::PinId Id) { return static_cast<uint32>(Id.Get()); }
	inline uint32 PhysicsLinkIdToU32(ed::LinkId Id) { return static_cast<uint32>(Id.Get()); }

	constexpr uint32 PhysicsBodyNodeBase = 100000;
	constexpr uint32 PhysicsConstraintNodeBase = 200000;
	constexpr uint32 PhysicsBodyInputPinBase = 300000;
	constexpr uint32 PhysicsBodyOutputPinBase = 400000;
	constexpr uint32 PhysicsConstraintInputPinBase = 500000;
	constexpr uint32 PhysicsConstraintOutputPinBase = 600000;
	constexpr uint32 PhysicsParentLinkBase = 700000;
	constexpr uint32 PhysicsChildLinkBase = 800000;

	uint32 MakeBodyNodeId(int32 BodyIndex) { return PhysicsBodyNodeBase + static_cast<uint32>(BodyIndex); }
	uint32 MakeConstraintNodeId(int32 ConstraintIndex) { return PhysicsConstraintNodeBase + static_cast<uint32>(ConstraintIndex); }
	uint32 MakeBodyInputPinId(int32 BodyIndex) { return PhysicsBodyInputPinBase + static_cast<uint32>(BodyIndex); }
	uint32 MakeBodyOutputPinId(int32 BodyIndex) { return PhysicsBodyOutputPinBase + static_cast<uint32>(BodyIndex); }
	uint32 MakeConstraintInputPinId(int32 ConstraintIndex) { return PhysicsConstraintInputPinBase + static_cast<uint32>(ConstraintIndex); }
	uint32 MakeConstraintOutputPinId(int32 ConstraintIndex) { return PhysicsConstraintOutputPinBase + static_cast<uint32>(ConstraintIndex); }
	uint32 MakeParentLinkId(int32 ConstraintIndex) { return PhysicsParentLinkBase + static_cast<uint32>(ConstraintIndex); }
	uint32 MakeChildLinkId(int32 ConstraintIndex) { return PhysicsChildLinkBase + static_cast<uint32>(ConstraintIndex); }

	bool DecodeBodyInputPin(uint32 PinId, int32& OutBodyIndex)
	{
		if (PinId < PhysicsBodyInputPinBase || PinId >= PhysicsBodyOutputPinBase)
		{
			return false;
		}
		OutBodyIndex = static_cast<int32>(PinId - PhysicsBodyInputPinBase);
		return true;
	}

	bool DecodeBodyOutputPin(uint32 PinId, int32& OutBodyIndex)
	{
		if (PinId < PhysicsBodyOutputPinBase || PinId >= PhysicsConstraintInputPinBase)
		{
			return false;
		}
		OutBodyIndex = static_cast<int32>(PinId - PhysicsBodyOutputPinBase);
		return true;
	}

	bool DecodeBodyNode(uint32 NodeId, int32& OutBodyIndex)
	{
		if (NodeId < PhysicsBodyNodeBase || NodeId >= PhysicsConstraintNodeBase)
		{
			return false;
		}
		OutBodyIndex = static_cast<int32>(NodeId - PhysicsBodyNodeBase);
		return true;
	}

	bool DecodeConstraintNode(uint32 NodeId, int32& OutConstraintIndex)
	{
		if (NodeId < PhysicsConstraintNodeBase || NodeId >= PhysicsBodyInputPinBase)
		{
			return false;
		}
		OutConstraintIndex = static_cast<int32>(NodeId - PhysicsConstraintNodeBase);
		return true;
	}

	bool DecodeConstraintLink(uint32 LinkId, int32& OutConstraintIndex)
	{
		if (LinkId >= PhysicsParentLinkBase && LinkId < PhysicsChildLinkBase)
		{
			OutConstraintIndex = static_cast<int32>(LinkId - PhysicsParentLinkBase);
			return true;
		}
		if (LinkId >= PhysicsChildLinkBase)
		{
			OutConstraintIndex = static_cast<int32>(LinkId - PhysicsChildLinkBase);
			return true;
		}
		return false;
	}

	const char* ToMotionLabel(EConstraintMotion Motion)
	{
		switch (Motion)
		{
		case EConstraintMotion::Free:
			return "Free";
		case EConstraintMotion::Limited:
			return "Limited";
		case EConstraintMotion::Locked:
			return "Locked";
		default:
			return "Unknown";
		}
	}

	bool MotionCombo(const char* Label, EConstraintMotion& Motion)
	{
		bool bChanged = false;
		if (ImGui::BeginCombo(Label, ToMotionLabel(Motion)))
		{
			const EConstraintMotion Values[] = {
				EConstraintMotion::Free,
				EConstraintMotion::Limited,
				EConstraintMotion::Locked
			};

			for (EConstraintMotion Value : Values)
			{
				const bool bSelected = Motion == Value;
				if (ImGui::Selectable(ToMotionLabel(Value), bSelected))
				{
					Motion = Value;
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	bool DragVector(const char* Label, FVector& Value, float Speed = 0.1f)
	{
		float Values[3] = { Value.X, Value.Y, Value.Z };
		if (ImGui::DragFloat3(Label, Values, Speed))
		{
			Value = FVector(Values[0], Values[1], Values[2]);
			return true;
		}
		return false;
	}

	bool DragRotator(const char* Label, FRotator& Value, float Speed = 0.1f)
	{
		float Values[3] = { Value.Roll, Value.Pitch, Value.Yaw };
		if (ImGui::DragFloat3(Label, Values, Speed))
		{
			Value = FRotator(Values[1], Values[2], Values[0]);
			return true;
		}
		return false;
	}

	float ClampPanelWidth(float Value, float MinValue, float MaxValue)
	{
		if (MaxValue < MinValue)
		{
			return MinValue;
		}
		return (std::max)(MinValue, (std::min)(Value, MaxValue));
	}

	void DrawVerticalSplitter(const char* Id, float& LeftWidth, float& RightWidth, float MinLeft, float MinRight, float Height)
	{
		constexpr float SplitterWidth = 6.0f;
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::PushID(Id);
		ImGui::InvisibleButton("Splitter", ImVec2(SplitterWidth, Height));

		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();
		if (bHovered || bActive)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}

		if (bActive)
		{
			const float Delta = ImGui::GetIO().MouseDelta.x;
			const float ClampedDelta = ClampPanelWidth(LeftWidth + Delta, MinLeft, LeftWidth + RightWidth - MinRight) - LeftWidth;
			LeftWidth += ClampedDelta;
			RightWidth -= ClampedDelta;
		}

		const ImU32 Color = ImGui::GetColorU32(bActive
			? ImGuiCol_SeparatorActive
			: (bHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		const float X = (Min.x + Max.x) * 0.5f;
		ImGui::GetWindowDrawList()->AddLine(ImVec2(X, Min.y), ImVec2(X, Max.y), Color, bHovered || bActive ? 2.0f : 1.0f);
		ImGui::PopID();
		ImGui::SameLine(0.0f, 0.0f);
	}

	FString MakeConstraintNameString(FName ParentBoneName, FName ChildBoneName, int32 Suffix)
	{
		FString Name = ParentBoneName.ToString() + "_" + ChildBoneName.ToString() + "_Constraint";
		if (Suffix > 0)
		{
			Name += "_" + std::to_string(Suffix);
		}
		return Name;
	}

	std::filesystem::path ResolveProjectPath(const FString& Path)
	{
		std::filesystem::path FullPath(FPaths::ToWide(Path));
		if (!FullPath.is_absolute())
		{
			FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
		}
		return FullPath.lexically_normal();
	}

	FString MakeDefaultPhysicsAssetPathForMesh(const USkeletalMesh* Mesh)
	{
		if (!Mesh)
		{
			return "None";
		}

		const FString MeshPath = FPaths::MakeProjectRelative(Mesh->GetAssetPathFileName());
		if (MeshPath.empty() || MeshPath == "None")
		{
			return "None";
		}

		std::filesystem::path AssetPath(FPaths::ToWide(MeshPath));
		std::wstring Stem = AssetPath.stem().wstring();
		const std::wstring SkeletalSuffix = L"_SkeletalMesh";
		if (Stem.size() >= SkeletalSuffix.size()
			&& Stem.compare(Stem.size() - SkeletalSuffix.size(), SkeletalSuffix.size(), SkeletalSuffix) == 0)
		{
			Stem.replace(Stem.size() - SkeletalSuffix.size(), SkeletalSuffix.size(), L"_PhysicsAsset");
		}
		else
		{
			Stem += L"_PhysicsAsset";
		}

		AssetPath.replace_filename(Stem + L".uasset");
		return FPaths::ToUtf8(AssetPath.generic_wstring());
	}

	FVector RotateByQuat(const FQuat& Quat, const FVector& Value)
	{
		return Quat.RotateVector(Value);
	}

	void DrawDebugOrientedBox(UWorld* World, const FVector& Center, const FVector& HalfExtent, const FQuat& Rotation, const FColor& Color)
	{
		const FVector Right = RotateByQuat(Rotation, FVector::RightVector) * HalfExtent.Y;
		const FVector Forward = RotateByQuat(Rotation, FVector::ForwardVector) * HalfExtent.X;
		const FVector Up = RotateByQuat(Rotation, FVector::UpVector) * HalfExtent.Z;

		const FVector P0 = Center - Forward - Right - Up;
		const FVector P1 = Center + Forward - Right - Up;
		const FVector P2 = Center + Forward + Right - Up;
		const FVector P3 = Center - Forward + Right - Up;
		const FVector P4 = Center - Forward - Right + Up;
		const FVector P5 = Center + Forward - Right + Up;
		const FVector P6 = Center + Forward + Right + Up;
		const FVector P7 = Center - Forward + Right + Up;

		DrawDebugBox(World, P0, P1, P2, P3, P4, P5, P6, P7, Color, 0.0f);
	}

	void DrawDebugCircle(UWorld* World, const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Radius, const FColor& Color)
	{
		if (!World || Radius <= 0.0f)
		{
			return;
		}

		constexpr int32 Segments = 24;
		const float Step = 2.0f * FMath::Pi / static_cast<float>(Segments);
		FVector Prev = Center + AxisA * Radius;

		for (int32 Index = 1; Index <= Segments; ++Index)
		{
			const float Angle = Step * static_cast<float>(Index);
			const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
			DrawDebugLine(World, Prev, Next, Color, 0.0f);
			Prev = Next;
		}
	}

	void DrawDebugCapsule(UWorld* World, const FVector& Center, float Radius, float Length, const FQuat& Rotation, const FColor& Color)
	{
		if (!World || Radius <= 0.0f)
		{
			return;
		}

		const FVector Axis = RotateByQuat(Rotation, FVector::ForwardVector);
		const FVector Right = RotateByQuat(Rotation, FVector::RightVector);
		const FVector Up = RotateByQuat(Rotation, FVector::UpVector);
		const float HalfLength = (std::max)(0.0f, Length * 0.5f);

		const FVector A = Center - Axis * HalfLength;
		const FVector B = Center + Axis * HalfLength;

		DrawDebugSphere(World, A, Radius, 16, Color, 0.0f);
		DrawDebugSphere(World, B, Radius, 16, Color, 0.0f);
		DrawDebugLine(World, A + Right * Radius, B + Right * Radius, Color, 0.0f);
		DrawDebugLine(World, A - Right * Radius, B - Right * Radius, Color, 0.0f);
		DrawDebugLine(World, A + Up * Radius, B + Up * Radius, Color, 0.0f);
		DrawDebugLine(World, A - Up * Radius, B - Up * Radius, Color, 0.0f);
		DrawDebugCircle(World, A, Right, Up, Radius, Color);
		DrawDebugCircle(World, B, Right, Up, Radius, Color);
	}

	void HashCombine(std::uint64_t& Seed, std::uint64_t Value)
	{
		Seed ^= Value + 0x9e3779b97f4a7c15ull + (Seed << 6) + (Seed >> 2);
	}

	void HashInt(std::uint64_t& Seed, int32 Value)
	{
		HashCombine(Seed, static_cast<std::uint64_t>(static_cast<uint32>(Value)));
	}

	void HashFloat(std::uint64_t& Seed, float Value)
	{
		const float SafeValue = std::isfinite(Value) ? Value : 0.0f;
		const int32 Quantized = static_cast<int32>(std::round(SafeValue * 1000.0f));
		HashInt(Seed, Quantized);
	}

	void HashVector(std::uint64_t& Seed, const FVector& Value)
	{
		HashFloat(Seed, Value.X);
		HashFloat(Seed, Value.Y);
		HashFloat(Seed, Value.Z);
	}

	void HashQuat(std::uint64_t& Seed, const FQuat& Value)
	{
		HashFloat(Seed, Value.X);
		HashFloat(Seed, Value.Y);
		HashFloat(Seed, Value.Z);
		HashFloat(Seed, Value.W);
	}

	FVector4 MakeSolidBodyColor(bool bSelectedBody, bool bSelectedPrimitive)
	{
		if (bSelectedPrimitive)
		{
			return FVector4(1.0f, 0.86f, 0.18f, 0.42f);
		}
		if (bSelectedBody)
		{
			return FVector4(1.0f, 0.62f, 0.16f, 0.34f);
		}
		return FVector4(0.12f, 0.68f, 1.0f, 0.24f);
	}

	uint32 AddSolidVertex(FMeshData& MeshData, const FVector& Position, const FVector4& Color)
	{
		const uint32 Index = static_cast<uint32>(MeshData.Vertices.size());
		MeshData.Vertices.push_back(FVertex{ Position, Color, 0 });
		return Index;
	}

	void AddSolidTriangle(FMeshData& MeshData, uint32 A, uint32 B, uint32 C)
	{
		MeshData.Indices.push_back(A);
		MeshData.Indices.push_back(B);
		MeshData.Indices.push_back(C);
	}

	FVector TransformSolidPoint(const FVector& Center, const FQuat& Rotation, const FVector& LocalPosition)
	{
		return Center + Rotation.RotateVector(LocalPosition);
	}

	void AddSolidBox(FMeshData& MeshData, const FVector& Center, const FVector& HalfExtent, const FQuat& Rotation, const FVector4& Color)
	{
		const FVector LocalCorners[8] = {
			FVector(-HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z),
			FVector( HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z),
			FVector( HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z),
			FVector(-HalfExtent.X,  HalfExtent.Y, -HalfExtent.Z),
			FVector(-HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z),
			FVector( HalfExtent.X, -HalfExtent.Y,  HalfExtent.Z),
			FVector( HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z),
			FVector(-HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z)
		};

		uint32 Indices[8];
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Indices[Index] = AddSolidVertex(MeshData, TransformSolidPoint(Center, Rotation, LocalCorners[Index]), Color);
		}

		const uint32 Triangles[36] = {
			0, 1, 2, 0, 2, 3,
			4, 6, 5, 4, 7, 6,
			0, 4, 5, 0, 5, 1,
			1, 5, 6, 1, 6, 2,
			2, 6, 7, 2, 7, 3,
			3, 7, 4, 3, 4, 0
		};

		for (uint32 TriangleIndex = 0; TriangleIndex < 36; TriangleIndex += 3)
		{
			AddSolidTriangle(
				MeshData,
				Indices[Triangles[TriangleIndex]],
				Indices[Triangles[TriangleIndex + 1]],
				Indices[Triangles[TriangleIndex + 2]]);
		}
	}

	void AddSolidSphere(FMeshData& MeshData, const FVector& Center, float Radius, const FQuat& Rotation, const FVector4& Color)
	{
		if (Radius <= 0.0f)
		{
			return;
		}

		constexpr int32 LatitudeSegments = 12;
		constexpr int32 LongitudeSegments = 24;
		const uint32 BaseIndex = static_cast<uint32>(MeshData.Vertices.size());

		for (int32 Lat = 0; Lat <= LatitudeSegments; ++Lat)
		{
			const float V = static_cast<float>(Lat) / static_cast<float>(LatitudeSegments);
			const float Theta = -FMath::Pi * 0.5f + V * FMath::Pi;
			const float RingRadius = std::cos(Theta) * Radius;
			const float Z = std::sin(Theta) * Radius;

			for (int32 Lon = 0; Lon < LongitudeSegments; ++Lon)
			{
				const float U = static_cast<float>(Lon) / static_cast<float>(LongitudeSegments);
				const float Phi = U * FMath::Pi * 2.0f;
				const FVector Local(
					std::cos(Phi) * RingRadius,
					std::sin(Phi) * RingRadius,
					Z);
				AddSolidVertex(MeshData, TransformSolidPoint(Center, Rotation, Local), Color);
			}
		}

		for (int32 Lat = 0; Lat < LatitudeSegments; ++Lat)
		{
			for (int32 Lon = 0; Lon < LongitudeSegments; ++Lon)
			{
				const int32 NextLon = (Lon + 1) % LongitudeSegments;
				const uint32 A = BaseIndex + static_cast<uint32>(Lat * LongitudeSegments + Lon);
				const uint32 B = BaseIndex + static_cast<uint32>(Lat * LongitudeSegments + NextLon);
				const uint32 C = BaseIndex + static_cast<uint32>((Lat + 1) * LongitudeSegments + Lon);
				const uint32 D = BaseIndex + static_cast<uint32>((Lat + 1) * LongitudeSegments + NextLon);
				AddSolidTriangle(MeshData, A, C, B);
				AddSolidTriangle(MeshData, B, C, D);
			}
		}
	}

	void AddSolidCapsule(FMeshData& MeshData, const FVector& Center, float Radius, float Length, const FQuat& Rotation, const FVector4& Color)
	{
		if (Radius <= 0.0f)
		{
			return;
		}

		struct FCapsuleRing
		{
			float X = 0.0f;
			float RingRadius = 0.0f;
		};

		constexpr int32 RingSegments = 24;
		constexpr int32 HemisphereSegments = 8;
		const float HalfLength = (std::max)(0.0f, Length * 0.5f);

		TArray<FCapsuleRing> Rings;
		for (int32 Index = 0; Index <= HemisphereSegments; ++Index)
		{
			const float T = static_cast<float>(Index) / static_cast<float>(HemisphereSegments);
			const float Angle = -FMath::Pi * 0.5f + T * FMath::Pi * 0.5f;
			Rings.push_back({ -HalfLength + std::sin(Angle) * Radius, std::cos(Angle) * Radius });
		}

		Rings.push_back({ HalfLength, Radius });

		for (int32 Index = 1; Index <= HemisphereSegments; ++Index)
		{
			const float T = static_cast<float>(Index) / static_cast<float>(HemisphereSegments);
			const float Angle = T * FMath::Pi * 0.5f;
			Rings.push_back({ HalfLength + std::sin(Angle) * Radius, std::cos(Angle) * Radius });
		}

		const uint32 BaseIndex = static_cast<uint32>(MeshData.Vertices.size());
		for (const FCapsuleRing& Ring : Rings)
		{
			for (int32 Segment = 0; Segment < RingSegments; ++Segment)
			{
				const float U = static_cast<float>(Segment) / static_cast<float>(RingSegments);
				const float Angle = U * FMath::Pi * 2.0f;
				const FVector Local(
					Ring.X,
					std::cos(Angle) * Ring.RingRadius,
					std::sin(Angle) * Ring.RingRadius);
				AddSolidVertex(MeshData, TransformSolidPoint(Center, Rotation, Local), Color);
			}
		}

		const int32 RingCount = static_cast<int32>(Rings.size());
		for (int32 RingIndex = 0; RingIndex < RingCount - 1; ++RingIndex)
		{
			for (int32 Segment = 0; Segment < RingSegments; ++Segment)
			{
				const int32 NextSegment = (Segment + 1) % RingSegments;
				const uint32 A = BaseIndex + static_cast<uint32>(RingIndex * RingSegments + Segment);
				const uint32 B = BaseIndex + static_cast<uint32>(RingIndex * RingSegments + NextSegment);
				const uint32 C = BaseIndex + static_cast<uint32>((RingIndex + 1) * RingSegments + Segment);
				const uint32 D = BaseIndex + static_cast<uint32>((RingIndex + 1) * RingSegments + NextSegment);
				AddSolidTriangle(MeshData, A, C, B);
				AddSolidTriangle(MeshData, B, C, D);
			}
		}
	}
}

struct FPhysicsAssetPreviewDiffuseConstants
{
	FVector4 DiffuseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
};

class FPhysicsAssetSolidPreviewSceneProxy final : public FPrimitiveSceneProxy
{
public:
	explicit FPhysicsAssetSolidPreviewSceneProxy(UPrimitiveComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
	{
		ProxyFlags = EPrimitiveProxyFlags::EditorOnly | EPrimitiveProxyFlags::NeverCull;
		ProxyFlags &= ~(EPrimitiveProxyFlags::SupportsOutline | EPrimitiveProxyFlags::ShowAABB);
		bCastShadow = false;
	}

	~FPhysicsAssetSolidPreviewSceneProxy() override
	{
		DiffuseCB.Release();
	}

	void UpdateVisibility() override
	{
		FPrimitiveSceneProxy::UpdateVisibility();
		bCastShadow = false;
		bCastShadowAsTwoSided = false;
	}

	void UpdateMesh() override
	{
		UPrimitiveComponent* OwnerComponent = GetOwner();
		if (!OwnerComponent)
		{
			MeshBuffer = nullptr;
			SectionDraws.clear();
			bVisible = false;
			return;
		}

		MeshBuffer = OwnerComponent->GetMeshBuffer();

		if (!DefaultMaterial)
		{
			DefaultMaterial = UMaterial::CreateTransient(
				ERenderPass::AlphaBlend,
				EBlendState::AlphaBlend,
				EDepthStencilState::DepthReadOnly,
				ERasterizerState::SolidNoCull,
				FShaderManager::Get().GetOrCreate(EShaderPath::Primitive));

			FPhysicsAssetPreviewDiffuseConstants& Constants =
				DefaultMaterial->BindPerShaderCB<FPhysicsAssetPreviewDiffuseConstants>(&DiffuseCB, ECBSlot::PerShader0);
			Constants.DiffuseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		SectionDraws.clear();
		if (MeshBuffer && MeshBuffer->IsValid() && DefaultMaterial)
		{
			SectionDraws.push_back({ DefaultMaterial, 0, MeshBuffer->GetIndexBuffer().GetIndexCount() });
		}
	}

private:
	FConstantBuffer DiffuseCB;
};

class FPhysicsAssetSolidPreviewComponent final : public UPrimitiveComponent
{
public:
	FPhysicsAssetSolidPreviewComponent()
	{
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetCastShadow(false);
	}

	~FPhysicsAssetSolidPreviewComponent() override = default;

	FMeshBuffer* GetMeshBuffer() const override
	{
		return const_cast<FMeshBuffer*>(&MeshBuffer);
	}

	FPrimitiveSceneProxy* CreateSceneProxy() override
	{
		return new FPhysicsAssetSolidPreviewSceneProxy(this);
	}

	bool SupportsOutline() const override
	{
		return false;
	}

	void RebuildFromEditor(const FPhysicsAssetEditorWidget& Editor, bool bEnabled)
	{
		SetVisibility(bEnabled);
		if (!bEnabled)
		{
			return;
		}

		const std::uint64_t NewHash = BuildMeshData(Editor, nullptr);
		if (NewHash == LastMeshHash)
		{
			return;
		}

		LastMeshHash = NewHash;

		FMeshData MeshData;
		BuildMeshData(Editor, &MeshData);
		if (MeshData.Vertices.empty() || MeshData.Indices.empty() || !GEngine)
		{
			MeshBuffer.Release();
			MarkRenderStateDirty();
			return;
		}

		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		if (!Device)
		{
			return;
		}

		MeshBuffer.Create(Device, MeshData);
		MarkRenderStateDirty();
	}

private:
	std::uint64_t BuildMeshData(const FPhysicsAssetEditorWidget& Editor, FMeshData* OutMeshData) const
	{
		std::uint64_t Hash = 0;
		HashInt(Hash, Editor.SelectedBodyIndex);
		HashInt(Hash, Editor.SelectedPrimitiveIndex);
		HashInt(Hash, static_cast<int32>(Editor.SelectedPrimitiveType));
		HashInt(Hash, Editor.EditingPhysicsAsset ? Editor.EditingPhysicsAsset->GetBodySetupCount() : 0);

		if (!Editor.EditingPhysicsAsset)
		{
			return Hash;
		}

		for (int32 BodyIndex = 0; BodyIndex < Editor.EditingPhysicsAsset->GetBodySetupCount(); ++BodyIndex)
		{
			const USkeletalBodySetup* BodySetup = Editor.EditingPhysicsAsset->GetBodySetup(BodyIndex);
			if (!BodySetup)
			{
				continue;
			}

			const bool bSelectedBody = Editor.SelectionType == EPhysicsAssetEditorSelectionType::Body
				&& Editor.SelectedBodyIndex == BodyIndex;
			const FQuat BodyRotation = Editor.GetBodyWorldRotation(BodySetup);
			const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();

			HashInt(Hash, BodyIndex);
			HashVector(Hash, Editor.GetBodyWorldLocation(BodySetup));
			HashQuat(Hash, BodyRotation);
			HashInt(Hash, static_cast<int32>(AggGeom.BoxElems.size()));
			HashInt(Hash, static_cast<int32>(AggGeom.SphereElems.size()));
			HashInt(Hash, static_cast<int32>(AggGeom.SphylElems.size()));

			for (int32 Index = 0; Index < static_cast<int32>(AggGeom.BoxElems.size()); ++Index)
			{
				const FKBoxElem& Box = AggGeom.BoxElems[Index];
				const bool bSelectedPrimitive = bSelectedBody
					&& Editor.SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Box
					&& Editor.SelectedPrimitiveIndex == Index;
				const FVector4 Color = MakeSolidBodyColor(bSelectedBody, bSelectedPrimitive);
				const FQuat Rotation = (BodyRotation * Box.Rotation.ToQuaternion()).GetNormalized();
				const FVector Center = Editor.BodyLocalToWorld(BodySetup, Box.Center);
				const FVector HalfExtent = FVector(Box.X, Box.Y, Box.Z) * 0.5f;

				HashVector(Hash, Box.Center);
				HashQuat(Hash, Rotation);
				HashFloat(Hash, Box.X);
				HashFloat(Hash, Box.Y);
				HashFloat(Hash, Box.Z);
				if (OutMeshData)
				{
					AddSolidBox(*OutMeshData, Center, HalfExtent, Rotation, Color);
				}
			}

			for (int32 Index = 0; Index < static_cast<int32>(AggGeom.SphereElems.size()); ++Index)
			{
				const FKSphereElem& Sphere = AggGeom.SphereElems[Index];
				const bool bSelectedPrimitive = bSelectedBody
					&& Editor.SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Sphere
					&& Editor.SelectedPrimitiveIndex == Index;
				const FVector4 Color = MakeSolidBodyColor(bSelectedBody, bSelectedPrimitive);
				const FVector Center = Editor.BodyLocalToWorld(BodySetup, Sphere.Center);

				HashVector(Hash, Sphere.Center);
				HashFloat(Hash, Sphere.Radius);
				if (OutMeshData)
				{
					AddSolidSphere(*OutMeshData, Center, Sphere.Radius, BodyRotation, Color);
				}
			}

			for (int32 Index = 0; Index < static_cast<int32>(AggGeom.SphylElems.size()); ++Index)
			{
				const FKSphylElem& Capsule = AggGeom.SphylElems[Index];
				const bool bSelectedPrimitive = bSelectedBody
					&& Editor.SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Capsule
					&& Editor.SelectedPrimitiveIndex == Index;
				const FVector4 Color = MakeSolidBodyColor(bSelectedBody, bSelectedPrimitive);
				const FQuat Rotation = (BodyRotation * Capsule.Rotation.ToQuaternion()).GetNormalized();
				const FVector Center = Editor.BodyLocalToWorld(BodySetup, Capsule.Center);

				HashVector(Hash, Capsule.Center);
				HashQuat(Hash, Rotation);
				HashFloat(Hash, Capsule.Radius);
				HashFloat(Hash, Capsule.Length);
				if (OutMeshData)
				{
					AddSolidCapsule(*OutMeshData, Center, Capsule.Radius, Capsule.Length, Rotation, Color);
				}
			}
		}

		return Hash;
	}

private:
	mutable FMeshBuffer MeshBuffer;
	std::uint64_t LastMeshHash = ~std::uint64_t{0};
};

class FPhysicsAssetPrimitiveGizmoTarget final : public IGizmoTransformTarget
{
public:
	explicit FPhysicsAssetPrimitiveGizmoTarget(FPhysicsAssetEditorWidget* InEditor)
		: Editor(InEditor)
	{
	}

	bool IsValid() const override
	{
		return Editor && Editor->EditingPhysicsAsset && Editor->SelectedBodyIndex >= 0
			&& Editor->SelectedPrimitiveType != EPhysicsAssetPrimitiveType::None
			&& Editor->SelectedPrimitiveIndex >= 0;
	}

	UWorld* GetWorld() const override
	{
		return Editor ? Editor->ViewportClient.GetPreviewWorld() : nullptr;
	}

	FVector GetWorldLocation() const override
	{
		const USkeletalBodySetup* BodySetup = GetBodySetup();
		if (!BodySetup)
		{
			return FVector::ZeroVector;
		}
		return Editor->BodyLocalToWorld(BodySetup, GetPrimitiveCenter(*BodySetup));
	}

	FRotator GetWorldRotation() const override
	{
		return GetWorldQuat().ToRotator();
	}

	FQuat GetWorldQuat() const override
	{
		const USkeletalBodySetup* BodySetup = GetBodySetup();
		if (!BodySetup)
		{
			return FQuat::Identity;
		}

		return (Editor->GetBodyWorldRotation(BodySetup) * GetPrimitiveLocalQuat(*BodySetup)).GetNormalized();
	}

	FVector GetWorldScale() const override
	{
		return FVector::OneVector;
	}

	void SetWorldLocation(const FVector& NewLocation) override
	{
		USkeletalBodySetup* BodySetup = GetMutableBodySetup();
		if (!BodySetup)
		{
			return;
		}

		const FVector DeltaWorld = NewLocation - GetWorldLocation();
		AddWorldOffset(DeltaWorld);
	}

	void SetWorldRotation(const FRotator& NewRotation) override
	{
		SetWorldRotation(NewRotation.ToQuaternion());
	}

	void SetWorldRotation(const FQuat& NewQuat) override
	{
		USkeletalBodySetup* BodySetup = GetMutableBodySetup();
		if (!BodySetup)
		{
			return;
		}

		const FQuat BoneWorldInv = Editor->GetBodyWorldRotation(BodySetup).Inverse();
		SetPrimitiveLocalQuat(*BodySetup, (BoneWorldInv * NewQuat).GetNormalized());
		Editor->MarkDirty();
		Editor->EditingPhysicsAsset->RefreshPhysicsAssetChange();
	}

	void SetWorldScale(const FVector& NewScale) override
	{
		(void)NewScale;
	}

	void AddWorldOffset(const FVector& Delta) override
	{
		USkeletalBodySetup* BodySetup = GetMutableBodySetup();
		if (!BodySetup)
		{
			return;
		}

		SetPrimitiveCenter(*BodySetup, GetPrimitiveCenter(*BodySetup) + Editor->WorldDeltaToBodyLocal(BodySetup, Delta));
		Editor->MarkDirty();
		Editor->EditingPhysicsAsset->RefreshPhysicsAssetChange();
	}

	void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override
	{
		const FQuat Current = GetWorldQuat();
		SetWorldRotation(bWorldSpace ? (Delta * Current) : (Current * Delta));
	}

	void AddScaleDelta(const FVector& Delta) override
	{
		USkeletalBodySetup* BodySetup = GetMutableBodySetup();
		if (!BodySetup)
		{
			return;
		}

		FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		switch (Editor->SelectedPrimitiveType)
		{
		case EPhysicsAssetPrimitiveType::Box:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.BoxElems.size()))
			{
				FKBoxElem& Box = AggGeom.BoxElems[Editor->SelectedPrimitiveIndex];
				Box.X = (std::max)(0.1f, Box.X + Delta.X);
				Box.Y = (std::max)(0.1f, Box.Y + Delta.Y);
				Box.Z = (std::max)(0.1f, Box.Z + Delta.Z);
			}
			break;
		case EPhysicsAssetPrimitiveType::Sphere:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphereElems.size()))
			{
				FKSphereElem& Sphere = AggGeom.SphereElems[Editor->SelectedPrimitiveIndex];
				Sphere.Radius = (std::max)(0.1f, Sphere.Radius + (Delta.X + Delta.Y + Delta.Z) / 3.0f);
			}
			break;
		case EPhysicsAssetPrimitiveType::Capsule:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphylElems.size()))
			{
				FKSphylElem& Capsule = AggGeom.SphylElems[Editor->SelectedPrimitiveIndex];
				Capsule.Radius = (std::max)(0.1f, Capsule.Radius + (Delta.Y + Delta.Z) * 0.5f);
				Capsule.Length = (std::max)(0.1f, Capsule.Length + Delta.X);
			}
			break;
		default:
			break;
		}

		Editor->MarkDirty();
		Editor->EditingPhysicsAsset->RefreshPhysicsAssetChange();
	}

private:
	USkeletalBodySetup* GetMutableBodySetup() const
	{
		return Editor && Editor->EditingPhysicsAsset
			? Editor->EditingPhysicsAsset->GetBodySetup(Editor->SelectedBodyIndex)
			: nullptr;
	}

	const USkeletalBodySetup* GetBodySetup() const
	{
		return GetMutableBodySetup();
	}

	FVector GetPrimitiveCenter(const USkeletalBodySetup& BodySetup) const
	{
		const FKAggregateGeom& AggGeom = BodySetup.GetAggGeom();
		switch (Editor->SelectedPrimitiveType)
		{
		case EPhysicsAssetPrimitiveType::Box:
			return Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.BoxElems.size())
				? AggGeom.BoxElems[Editor->SelectedPrimitiveIndex].Center
				: FVector::ZeroVector;
		case EPhysicsAssetPrimitiveType::Sphere:
			return Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphereElems.size())
				? AggGeom.SphereElems[Editor->SelectedPrimitiveIndex].Center
				: FVector::ZeroVector;
		case EPhysicsAssetPrimitiveType::Capsule:
			return Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphylElems.size())
				? AggGeom.SphylElems[Editor->SelectedPrimitiveIndex].Center
				: FVector::ZeroVector;
		default:
			return FVector::ZeroVector;
		}
	}

	void SetPrimitiveCenter(USkeletalBodySetup& BodySetup, const FVector& Center) const
	{
		FKAggregateGeom& AggGeom = BodySetup.GetAggGeom();
		switch (Editor->SelectedPrimitiveType)
		{
		case EPhysicsAssetPrimitiveType::Box:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.BoxElems.size()))
			{
				AggGeom.BoxElems[Editor->SelectedPrimitiveIndex].Center = Center;
			}
			break;
		case EPhysicsAssetPrimitiveType::Sphere:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphereElems.size()))
			{
				AggGeom.SphereElems[Editor->SelectedPrimitiveIndex].Center = Center;
			}
			break;
		case EPhysicsAssetPrimitiveType::Capsule:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphylElems.size()))
			{
				AggGeom.SphylElems[Editor->SelectedPrimitiveIndex].Center = Center;
			}
			break;
		default:
			break;
		}
	}

	FQuat GetPrimitiveLocalQuat(const USkeletalBodySetup& BodySetup) const
	{
		const FKAggregateGeom& AggGeom = BodySetup.GetAggGeom();
		switch (Editor->SelectedPrimitiveType)
		{
		case EPhysicsAssetPrimitiveType::Box:
			return Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.BoxElems.size())
				? AggGeom.BoxElems[Editor->SelectedPrimitiveIndex].Rotation.ToQuaternion()
				: FQuat::Identity;
		case EPhysicsAssetPrimitiveType::Capsule:
			return Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphylElems.size())
				? AggGeom.SphylElems[Editor->SelectedPrimitiveIndex].Rotation.ToQuaternion()
				: FQuat::Identity;
		default:
			return FQuat::Identity;
		}
	}

	void SetPrimitiveLocalQuat(USkeletalBodySetup& BodySetup, const FQuat& LocalQuat) const
	{
		FKAggregateGeom& AggGeom = BodySetup.GetAggGeom();
		switch (Editor->SelectedPrimitiveType)
		{
		case EPhysicsAssetPrimitiveType::Box:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.BoxElems.size()))
			{
				AggGeom.BoxElems[Editor->SelectedPrimitiveIndex].Rotation = LocalQuat.ToRotator();
			}
			break;
		case EPhysicsAssetPrimitiveType::Capsule:
			if (Editor->SelectedPrimitiveIndex < static_cast<int32>(AggGeom.SphylElems.size()))
			{
				AggGeom.SphylElems[Editor->SelectedPrimitiveIndex].Rotation = LocalQuat.ToRotator();
			}
			break;
		default:
			break;
		}
	}

private:
	FPhysicsAssetEditorWidget* Editor = nullptr;
};

FPhysicsAssetEditorWidget::FPhysicsAssetEditorWidget()
	: PrimitiveGizmoTarget(std::make_unique<FPhysicsAssetPrimitiveGizmoTarget>(this))
{
	InstanceId = GNextPhysicsAssetEditorInstanceId++;
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("PhysicsAssetEditorPreview_" + Id);
	WindowIdSuffix = "###PhysicsAssetEditor_" + Id;
}

FPhysicsAssetEditorWidget::~FPhysicsAssetEditorWidget() = default;

bool FPhysicsAssetEditorWidget::CanEdit(UObject* Object) const
{
	return Object && (Object->IsA<USkeletalMesh>() || Object->IsA<UPhysicsAsset>());
}

bool FPhysicsAssetEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const USkeletalMesh* CurrentMesh = EditingMesh;
	const USkeletalMesh* RequestedMesh = Cast<USkeletalMesh>(Object);
	if (!IsOpen() || !CurrentMesh || !RequestedMesh)
	{
		return false;
	}

	const FString& CurrentPath = CurrentMesh->GetAssetPathFileName();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedMesh->GetAssetPathFileName();
}

void FPhysicsAssetEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);

	SelectionType = EPhysicsAssetEditorSelectionType::None;
	SelectedBoneIndex = -1;
	SelectedBodyIndex = -1;
	SelectedConstraintIndex = -1;
	SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
	SelectedPrimitiveIndex = -1;

	ResolveEditingObjects(Object);
	InitializeConstraintGraphEditor();
	CreatePreviewWorld();
	SyncPreviewSelection();
}

void FPhysicsAssetEditorWidget::Close()
{
	DestroyPreviewWorld();
	DestroyConstraintGraphEditor();
	FAssetEditorWidget::Close();
	EditingMesh = nullptr;
	EditingPhysicsAsset = nullptr;
	SelectionType = EPhysicsAssetEditorSelectionType::None;
	SelectedBoneIndex = -1;
	SelectedBodyIndex = -1;
	SelectedConstraintIndex = -1;
	SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
	SelectedPrimitiveIndex = -1;
	bPendingClose = false;
	bConstraintGraphLayoutDirty = true;
	ConstraintGraphTopologyHash = 0;
}

void FPhysicsAssetEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
		UpdateSolidPreview();
		RenderPhysicsDebug();
		SyncPrimitiveGizmo();
	}
}

void FPhysicsAssetEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FMeshEditorViewportClient*>(&ViewportClient));
	}
}

void FPhysicsAssetEditorWidget::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAssetEditorWidget::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(EditingMesh, "PhysicsAssetEditor.EditingMesh");
	Collector.AddReferencedObject(EditingPhysicsAsset, "PhysicsAssetEditor.EditingPhysicsAsset");
	Collector.AddReferencedObject(SolidPreviewComponent, "PhysicsAssetEditor.SolidPreviewComponent");
	ViewportClient.AddReferencedObjects(Collector);
}

void FPhysicsAssetEditorWidget::ResolveEditingObjects(UObject* Object)
{
	EditingMesh = Cast<USkeletalMesh>(Object);
	EditingPhysicsAsset = Cast<UPhysicsAsset>(Object);
	const bool bOpenedPhysicsAssetDirectly = EditingPhysicsAsset && !EditingMesh;

	if (!EditingMesh && EditingPhysicsAsset)
	{
		const FString& PreviewMeshPath = EditingPhysicsAsset->GetPreviewSkeletalMeshPath();
		if (!PreviewMeshPath.empty() && PreviewMeshPath != "None" && GEngine)
		{
			EditingMesh = FMeshManager::LoadSkeletalMesh(
				PreviewMeshPath,
				GEngine->GetRenderer().GetFD3DDevice().GetDevice());
		}
	}

	if (EditingMesh)
	{
		if (!bOpenedPhysicsAssetDirectly)
		{
			EditingPhysicsAsset = EditingMesh->GetPhysicsAsset();
		}
		if (!EditingPhysicsAsset)
		{
			const FString DefaultPhysicsAssetPath = MakeDefaultPhysicsAssetPathForMesh(EditingMesh);
			if (!DefaultPhysicsAssetPath.empty() && DefaultPhysicsAssetPath != "None")
			{
				if (std::filesystem::exists(ResolveProjectPath(DefaultPhysicsAssetPath)))
				{
					EditingPhysicsAsset = FPhysicsAssetManager::Get().Load(DefaultPhysicsAssetPath);
				}

				if (!EditingPhysicsAsset)
				{
					EditingPhysicsAsset = UObjectManager::Get().CreateObject<UPhysicsAsset>();
					EditingPhysicsAsset->SetSourcePath(DefaultPhysicsAssetPath);
					EditingPhysicsAsset->SetPreviewSkeletalMeshPath(EditingMesh->GetAssetPathFileName());
					FPhysicsAssetManager::Get().Save(EditingPhysicsAsset);
				}

				EditingPhysicsAsset->SetPreviewSkeletalMeshPath(EditingMesh->GetAssetPathFileName());
				EditingMesh->SetPhysicsAsset(EditingPhysicsAsset);
				FMeshManager::SaveSkeletalMeshPackage(EditingMesh);
			}
		}
		else
		{
			EditingPhysicsAsset->SetPreviewSkeletalMeshPath(EditingMesh->GetAssetPathFileName());
		}
	}

	if (EditingPhysicsAsset)
	{
		EditingPhysicsAsset->UpdateBodySetupIndexMap();
	}
}

bool FPhysicsAssetEditorWidget::SaveAsset()
{
	if (!EditingPhysicsAsset)
	{
		return false;
	}

	if (EditingPhysicsAsset->GetSourcePath().empty() || EditingPhysicsAsset->GetSourcePath() == "None")
	{
		const FString DefaultPhysicsAssetPath = MakeDefaultPhysicsAssetPathForMesh(EditingMesh);
		if (DefaultPhysicsAssetPath.empty() || DefaultPhysicsAssetPath == "None")
		{
			return false;
		}
		EditingPhysicsAsset->SetSourcePath(DefaultPhysicsAssetPath);
	}

	if (EditingMesh)
	{
		EditingPhysicsAsset->SetPreviewSkeletalMeshPath(EditingMesh->GetAssetPathFileName());
	}

	if (!FPhysicsAssetManager::Get().Save(EditingPhysicsAsset))
	{
		return false;
	}

	if (EditingMesh)
	{
		EditingMesh->SetPhysicsAsset(EditingPhysicsAsset);
		FMeshManager::SaveSkeletalMeshPackage(EditingMesh);
	}

	ClearDirty();
	return true;
}

void FPhysicsAssetEditorWidget::CreatePreviewWorld()
{
	if (!EditingMesh || !GEngine)
	{
		return;
	}

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	USkeletalMeshComponent* MeshComp = Actor->AddComponent<USkeletalMeshComponent>();
	MeshComp->SetSkeletalMesh(EditingMesh);
	Actor->SetRootComponent(MeshComp);
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	if (UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>())
	{
		LightComp->SetShadowBias(0.0f);
		LightComp->PushToScene();
	}

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.05f));
	FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));

	const ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
	ViewportClient.Initialize(
		GEngine->GetRenderer().GetFD3DDevice().GetDevice(),
		static_cast<uint32>((std::max)(ViewportSize.x, 320.0f)),
		static_cast<uint32>((std::max)(ViewportSize.y, 240.0f)));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(MeshComp);
	ViewportClient.CreatePreviewGizmo();
	ViewportClient.CreateBoneDebugComponent();

	SolidPreviewComponent = new FPhysicsAssetSolidPreviewComponent();
	Actor->RegisterComponent(SolidPreviewComponent);
	UpdateSolidPreview();

	ViewportClient.ResetCameraToPreviousBounds();
	ViewportClient.ApplyTransformSettingsToGizmo();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);
	FSlateApplication::Get().RegisterViewport(&ViewportClient);
}

void FPhysicsAssetEditorWidget::DestroyPreviewWorld()
{
	SolidPreviewComponent = nullptr;

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}

	FSlateApplication::Get().UnregisterViewport(&ViewportClient);
	ViewportClient.Release();
}

void FPhysicsAssetEditorWidget::InitializeConstraintGraphEditor()
{
	if (ConstraintGraphContext)
	{
		return;
	}

	ed::Config Config;
	ConstraintGraphContext = ed::CreateEditor(&Config);
	bConstraintGraphLayoutDirty = true;
	ConstraintGraphTopologyHash = 0;
}

void FPhysicsAssetEditorWidget::DestroyConstraintGraphEditor()
{
	if (ConstraintGraphContext)
	{
		ed::DestroyEditor(ConstraintGraphContext);
		ConstraintGraphContext = nullptr;
	}
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

	if (EditingMesh)
	{
		const FString& AssetPath = EditingMesh->GetAssetPathFileName();
		if (!AssetPath.empty())
		{
			VisibleTitle += " - ";
			VisibleTitle += AssetPath;
		}
	}
	else if (EditingPhysicsAsset)
	{
		const FString& AssetPath = EditingPhysicsAsset->GetSourcePath();
		if (!AssetPath.empty() && AssetPath != "None")
		{
			VisibleTitle += " - ";
			VisibleTitle += AssetPath;
		}
	}

	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	if (ViewportClient.IsMouseOverViewport())
	{
		WindowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	}

	const FString WindowTitle = VisibleTitle + WindowIdSuffix;

	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			bPendingClose = true;
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);

		const ImGuiIO& IO = ImGui::GetIO();
		if ((IO.KeyCtrl || IO.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			SaveAsset();
		}
	}

	ImGui::TextUnformatted("Physics Asset Editor");
	ImGui::SameLine();
	if (EditingPhysicsAsset)
	{
		ImGui::TextDisabled("Bodies: %d  Constraints: %d",
			EditingPhysicsAsset->GetBodySetupCount(),
			EditingPhysicsAsset->GetConstraintSetupCount());
	}
	ImGui::Separator();

	const char* SaveLabel = IsDirty() ? "Save *" : "Save";
	if (ImGui::Button(SaveLabel, ImVec2(96.0f, 0.0f)))
	{
		SaveAsset();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Ctrl+S");
	ImGui::SameLine();
	if (EditingPhysicsAsset)
	{
		const FString& PhysicsAssetPath = EditingPhysicsAsset->GetSourcePath();
		ImGui::TextDisabled("%s", (!PhysicsAssetPath.empty() && PhysicsAssetPath != "None")
			? PhysicsAssetPath.c_str()
			: "Unsaved PhysicsAsset");
	}
	ImGui::Separator();

	const float AvailableHeight = ImGui::GetContentRegionAvail().y;
	const float ContentWidth = ImGui::GetContentRegionAvail().x;
	constexpr float SplitterWidth = 6.0f;
	constexpr float MinTreeWidth = 180.0f;
	constexpr float MinViewportWidth = 180.0f;
	constexpr float MinDetailsWidth = 240.0f;
	const float WidthBudget = (std::max)(0.0f, ContentWidth - SplitterWidth * 2.0f);

	TreePanelWidth = ClampPanelWidth(TreePanelWidth, MinTreeWidth, WidthBudget - MinViewportWidth - MinDetailsWidth);
	DetailsPanelWidth = ClampPanelWidth(DetailsPanelWidth, MinDetailsWidth, WidthBudget - TreePanelWidth - MinViewportWidth);

	float ViewportWidth = WidthBudget - TreePanelWidth - DetailsPanelWidth;
	if (ViewportWidth < MinViewportWidth)
	{
		float Deficit = MinViewportWidth - ViewportWidth;
		const float DetailsShrink = (std::min)(Deficit, (std::max)(0.0f, DetailsPanelWidth - MinDetailsWidth));
		DetailsPanelWidth -= DetailsShrink;
		Deficit -= DetailsShrink;

		const float TreeShrink = (std::min)(Deficit, (std::max)(0.0f, TreePanelWidth - MinTreeWidth));
		TreePanelWidth -= TreeShrink;
		ViewportWidth = WidthBudget - TreePanelWidth - DetailsPanelWidth;
	}
	ViewportWidth = (std::max)(MinViewportWidth, ViewportWidth);

	ImGui::BeginChild("PhysicsAssetTree", ImVec2(TreePanelWidth, AvailableHeight), true);
	RenderTreePanel();
	ImGui::EndChild();

	DrawVerticalSplitter("TreeViewportSplitter", TreePanelWidth, ViewportWidth, MinTreeWidth, MinViewportWidth, AvailableHeight);

	ImGui::BeginChild("PhysicsAssetViewport", ImVec2(ViewportWidth, AvailableHeight), true);
	RenderViewportPanel(ImGui::GetContentRegionAvail());
	ImGui::EndChild();

	DrawVerticalSplitter("ViewportDetailsSplitter", ViewportWidth, DetailsPanelWidth, MinViewportWidth, MinDetailsWidth, AvailableHeight);

	ImGui::BeginChild("PhysicsAssetDetails", ImVec2(DetailsPanelWidth, AvailableHeight), true);
	RenderDetailsPanel();
	ImGui::EndChild();

	ImGui::End();

	if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}

void FPhysicsAssetEditorWidget::RenderTreePanel()
{
	ImGui::TextUnformatted("Skeleton");
	ImGui::Separator();

	if (!EditingPhysicsAsset)
	{
		ImGui::TextDisabled("No PhysicsAsset.");
		return;
	}

	FSkeletalMesh* MeshAsset = EditingMesh ? EditingMesh->GetSkeletalMeshAsset() : nullptr;
	if (MeshAsset && !MeshAsset->Bones.empty())
	{
		if (ImGui::BeginChild("PhysicsAssetBoneTree", ImVec2(0.0f, 180.0f), true))
		{
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{
				if (MeshAsset->Bones[BoneIndex].ParentIndex < 0)
				{
					RenderBoneTree(MeshAsset, BoneIndex);
				}
			}
		}
		ImGui::EndChild();

		ImGui::TextUnformatted("Bodies");
		ImGui::Separator();
		if (ImGui::Button("+ Body", ImVec2(-1.0f, 0.0f)))
		{
			AddBodyForSelectedBone();
		}
		ImGui::BeginDisabled(SelectedBodyIndex < 0);
		if (ImGui::Button("+ Constraint To Parent", ImVec2(-1.0f, 0.0f)))
		{
			AddConstraintToParentBody();
		}
		ImGui::EndDisabled();
		ImGui::Separator();

		if (ImGui::BeginChild("PhysicsAssetBodyTree", ImVec2(0.0f, 150.0f), true))
		{
			RenderBodyTree();
		}
		ImGui::EndChild();
	}
	else
	{
		ImGui::TextDisabled("No preview skeletal mesh is bound.");
		ImGui::TextWrapped("Open this editor from a SkeletalMesh to edit bodies against a skeleton.");
	}

	ImGui::Dummy(ImVec2(0.0f, 6.0f));
	ImGui::Separator();
	ImGui::TextUnformatted("Constraint Graph");
	if (SelectedBodyIndex >= 0 || SelectedConstraintIndex >= 0)
	{
		RenderConstraintGraph();
	}
	else
	{
		ImGui::TextDisabled("Select a body to inspect constraints.");
	}
}

void FPhysicsAssetEditorWidget::RenderBodyTree()
{
	if (!EditingPhysicsAsset)
	{
		return;
	}

	for (int32 BodyIndex = 0; BodyIndex < EditingPhysicsAsset->GetBodySetupCount(); ++BodyIndex)
	{
		RenderBodyTreeRow(BodyIndex);
	}
}

void FPhysicsAssetEditorWidget::RenderBodyTreeRow(int32 BodyIndex)
{
	if (!EditingPhysicsAsset)
	{
		return;
	}

	USkeletalBodySetup* BodySetup = EditingPhysicsAsset->GetBodySetup(BodyIndex);
	if (!BodySetup)
	{
		return;
	}

	const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
	const int32 ShapeCount = static_cast<int32>(
		AggGeom.BoxElems.size() + AggGeom.SphereElems.size() + AggGeom.SphylElems.size());

	FString Label = BodySetup->GetBoneName().ToString();
	if (ShapeCount > 0)
	{
		Label += "  ";
		Label += std::to_string(ShapeCount);
		Label += ShapeCount == 1 ? " shape" : " shapes";
	}

	ImGui::PushID(BodyIndex);
	const bool bSelected = SelectionType == EPhysicsAssetEditorSelectionType::Body && SelectedBodyIndex == BodyIndex;
	if (ImGui::Selectable(Label.c_str(), bSelected))
	{
		SelectBody(BodyIndex);
	}
	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Select Body"))
		{
			SelectBody(BodyIndex);
		}
		if (ImGui::MenuItem("Add Constraint To Parent"))
		{
			SelectBody(BodyIndex);
			AddConstraintToParentBody();
		}
		if (ImGui::MenuItem("Delete Body"))
		{
			SelectBody(BodyIndex);
			RemoveSelectedBody();
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}

void FPhysicsAssetEditorWidget::RenderConstraintGraph()
{
	if (!EditingPhysicsAsset)
	{
		return;
	}

	InitializeConstraintGraphEditor();
	if (!ConstraintGraphContext)
	{
		ImGui::TextDisabled("Constraint graph is unavailable.");
		return;
	}

	std::uint64_t TopologyHash = 0;
	HashInt(TopologyHash, EditingPhysicsAsset->GetBodySetupCount());
	HashInt(TopologyHash, EditingPhysicsAsset->GetConstraintSetupCount());
	for (int32 ConstraintIndex = 0; ConstraintIndex < EditingPhysicsAsset->GetConstraintSetupCount(); ++ConstraintIndex)
	{
		if (const UPhysicsConstraintTemplate* Constraint = EditingPhysicsAsset->GetConstraintSetup(ConstraintIndex))
		{
			HashInt(TopologyHash, EditingPhysicsAsset->FindBodyIndex(Constraint->GetParentBoneName()));
			HashInt(TopologyHash, EditingPhysicsAsset->FindBodyIndex(Constraint->GetChildBoneName()));
		}
	}
	if (TopologyHash != ConstraintGraphTopologyHash)
	{
		ConstraintGraphTopologyHash = TopologyHash;
		bConstraintGraphLayoutDirty = true;
	}

	const float GraphHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y);
	ImGui::BeginChild("PhysicsConstraintGraphChild", ImVec2(0.0f, GraphHeight), true);

	ed::SetCurrentEditor(ConstraintGraphContext);
	ed::Begin("PhysicsConstraintGraph");

	if (bConstraintGraphLayoutDirty)
	{
		for (int32 BodyIndex = 0; BodyIndex < EditingPhysicsAsset->GetBodySetupCount(); ++BodyIndex)
		{
			const float X = (BodyIndex % 2 == 0) ? 20.0f : 520.0f;
			const float Y = 30.0f + static_cast<float>(BodyIndex / 2) * 110.0f;
			ed::SetNodePosition(ToPhysicsNodeId(MakeBodyNodeId(BodyIndex)), ImVec2(X, Y));
		}
		for (int32 ConstraintIndex = 0; ConstraintIndex < EditingPhysicsAsset->GetConstraintSetupCount(); ++ConstraintIndex)
		{
			ed::SetNodePosition(
				ToPhysicsNodeId(MakeConstraintNodeId(ConstraintIndex)),
				ImVec2(270.0f, 45.0f + static_cast<float>(ConstraintIndex) * 110.0f));
		}
		bConstraintGraphLayoutDirty = false;
	}

	for (int32 BodyIndex = 0; BodyIndex < EditingPhysicsAsset->GetBodySetupCount(); ++BodyIndex)
	{
		const USkeletalBodySetup* BodySetup = EditingPhysicsAsset->GetBodySetup(BodyIndex);
		if (!BodySetup)
		{
			continue;
		}

		const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		const int32 ShapeCount = static_cast<int32>(
			AggGeom.BoxElems.size() + AggGeom.SphereElems.size() + AggGeom.SphylElems.size());
		const bool bSelected = SelectionType == EPhysicsAssetEditorSelectionType::Body && SelectedBodyIndex == BodyIndex;

		ed::BeginNode(ToPhysicsNodeId(MakeBodyNodeId(BodyIndex)));
		ed::BeginPin(ToPhysicsPinId(MakeBodyInputPinId(BodyIndex)), ed::PinKind::Input);
		ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.80f, 1.0f), "o");
		ed::EndPin();
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextColored(
			bSelected ? ImVec4(1.0f, 0.86f, 0.18f, 1.0f) : ImVec4(0.64f, 0.88f, 0.62f, 1.0f),
			"Body");
		ImGui::TextUnformatted(BodySetup->GetBoneName().ToString().c_str());
		ImGui::TextDisabled("%d %s", ShapeCount, ShapeCount == 1 ? "shape" : "shapes");
		ImGui::EndGroup();
		ImGui::SameLine();
		ed::BeginPin(ToPhysicsPinId(MakeBodyOutputPinId(BodyIndex)), ed::PinKind::Output);
		ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.80f, 1.0f), "o");
		ed::EndPin();
		ed::EndNode();
	}

	for (int32 ConstraintIndex = 0; ConstraintIndex < EditingPhysicsAsset->GetConstraintSetupCount(); ++ConstraintIndex)
	{
		const UPhysicsConstraintTemplate* Constraint = EditingPhysicsAsset->GetConstraintSetup(ConstraintIndex);
		if (!Constraint)
		{
			continue;
		}

		const bool bSelected = SelectionType == EPhysicsAssetEditorSelectionType::Constraint
			&& SelectedConstraintIndex == ConstraintIndex;

		ed::BeginNode(ToPhysicsNodeId(MakeConstraintNodeId(ConstraintIndex)));
		ed::BeginPin(ToPhysicsPinId(MakeConstraintInputPinId(ConstraintIndex)), ed::PinKind::Input);
		ImGui::TextColored(ImVec4(0.90f, 0.86f, 0.55f, 1.0f), "o");
		ed::EndPin();
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextColored(
			bSelected ? ImVec4(1.0f, 0.70f, 0.42f, 1.0f) : ImVec4(0.90f, 0.86f, 0.55f, 1.0f),
			"Constraint");
		ImGui::TextUnformatted(Constraint->GetConstraintName().ToString().c_str());
		ImGui::TextDisabled(
			"%s -> %s",
			Constraint->GetParentBoneName().ToString().c_str(),
			Constraint->GetChildBoneName().ToString().c_str());
		ImGui::EndGroup();
		ImGui::SameLine();
		ed::BeginPin(ToPhysicsPinId(MakeConstraintOutputPinId(ConstraintIndex)), ed::PinKind::Output);
		ImGui::TextColored(ImVec4(0.90f, 0.86f, 0.55f, 1.0f), "o");
		ed::EndPin();
		ed::EndNode();
	}

	for (int32 ConstraintIndex = 0; ConstraintIndex < EditingPhysicsAsset->GetConstraintSetupCount(); ++ConstraintIndex)
	{
		const UPhysicsConstraintTemplate* Constraint = EditingPhysicsAsset->GetConstraintSetup(ConstraintIndex);
		if (!Constraint)
		{
			continue;
		}

		const int32 ParentBodyIndex = EditingPhysicsAsset->FindBodyIndex(Constraint->GetParentBoneName());
		const int32 ChildBodyIndex = EditingPhysicsAsset->FindBodyIndex(Constraint->GetChildBoneName());
		if (ParentBodyIndex >= 0)
		{
			ed::Link(
				ToPhysicsLinkId(MakeParentLinkId(ConstraintIndex)),
				ToPhysicsPinId(MakeBodyOutputPinId(ParentBodyIndex)),
				ToPhysicsPinId(MakeConstraintInputPinId(ConstraintIndex)),
				ImColor(130, 210, 190));
		}
		if (ChildBodyIndex >= 0)
		{
			ed::Link(
				ToPhysicsLinkId(MakeChildLinkId(ConstraintIndex)),
				ToPhysicsPinId(MakeConstraintOutputPinId(ConstraintIndex)),
				ToPhysicsPinId(MakeBodyInputPinId(ChildBodyIndex)),
				ImColor(225, 215, 145));
		}
	}

	if (ed::BeginCreate())
	{
		ed::PinId StartPinId = 0;
		ed::PinId EndPinId = 0;
		if (ed::QueryNewLink(&StartPinId, &EndPinId) && StartPinId && EndPinId)
		{
			int32 ParentBodyIndex = -1;
			int32 ChildBodyIndex = -1;
			const uint32 StartPin = PhysicsPinIdToU32(StartPinId);
			const uint32 EndPin = PhysicsPinIdToU32(EndPinId);
			const bool bForward = DecodeBodyOutputPin(StartPin, ParentBodyIndex) && DecodeBodyInputPin(EndPin, ChildBodyIndex);
			const bool bReverse = DecodeBodyOutputPin(EndPin, ParentBodyIndex) && DecodeBodyInputPin(StartPin, ChildBodyIndex);
			bool bAlreadyConstrained = false;
			for (int32 ConstraintIndex = 0; ConstraintIndex < EditingPhysicsAsset->GetConstraintSetupCount(); ++ConstraintIndex)
			{
				if (const UPhysicsConstraintTemplate* ExistingConstraint = EditingPhysicsAsset->GetConstraintSetup(ConstraintIndex))
				{
					const int32 ExistingParentBodyIndex = EditingPhysicsAsset->FindBodyIndex(ExistingConstraint->GetParentBoneName());
					const int32 ExistingChildBodyIndex = EditingPhysicsAsset->FindBodyIndex(ExistingConstraint->GetChildBoneName());
					if (ExistingParentBodyIndex == ParentBodyIndex && ExistingChildBodyIndex == ChildBodyIndex)
					{
						bAlreadyConstrained = true;
						break;
					}
				}
			}

			if ((bForward || bReverse)
				&& ParentBodyIndex != ChildBodyIndex
				&& !bAlreadyConstrained
				&& EditingPhysicsAsset->GetBodySetup(ParentBodyIndex)
				&& EditingPhysicsAsset->GetBodySetup(ChildBodyIndex))
			{
				if (ed::AcceptNewItem(ImVec4(0.55f, 0.90f, 0.80f, 1.0f), 2.0f))
				{
					const USkeletalBodySetup* ParentBody = EditingPhysicsAsset->GetBodySetup(ParentBodyIndex);
					const USkeletalBodySetup* ChildBody = EditingPhysicsAsset->GetBodySetup(ChildBodyIndex);
					const FName ConstraintName = MakeUniqueConstraintName(ParentBody->GetBoneName(), ChildBody->GetBoneName());
					if (EditingPhysicsAsset->AddConstraintSetup(ConstraintName, ParentBody->GetBoneName(), ChildBody->GetBoneName()))
					{
						SelectConstraint(EditingPhysicsAsset->FindConstraintIndex(ConstraintName));
						MarkDirty();
						bConstraintGraphLayoutDirty = true;
					}
				}
			}
			else
			{
				ed::RejectNewItem(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), 2.0f);
			}
		}
	}
	ed::EndCreate();

	TArray<int32> ConstraintIndicesToDelete;
	TArray<int32> BodyIndicesToDelete;
	if (ed::BeginDelete())
	{
		ed::LinkId DeletedLinkId = 0;
		while (ed::QueryDeletedLink(&DeletedLinkId))
		{
			if (ed::AcceptDeletedItem())
			{
				int32 ConstraintIndex = -1;
				if (DecodeConstraintLink(PhysicsLinkIdToU32(DeletedLinkId), ConstraintIndex))
				{
					ConstraintIndicesToDelete.push_back(ConstraintIndex);
				}
			}
		}

		ed::NodeId DeletedNodeId = 0;
		while (ed::QueryDeletedNode(&DeletedNodeId))
		{
			if (ed::AcceptDeletedItem())
			{
				int32 BodyIndex = -1;
				int32 ConstraintIndex = -1;
				const uint32 NodeId = PhysicsNodeIdToU32(DeletedNodeId);
				if (DecodeBodyNode(NodeId, BodyIndex))
				{
					BodyIndicesToDelete.push_back(BodyIndex);
				}
				else if (DecodeConstraintNode(NodeId, ConstraintIndex))
				{
					ConstraintIndicesToDelete.push_back(ConstraintIndex);
				}
			}
		}
	}
	ed::EndDelete();

	auto DeleteUniqueDescending = [](TArray<int32>& Indices, auto&& DeleteFn)
	{
		std::sort(Indices.begin(), Indices.end(), std::greater<int32>());
		int32 LastDeleted = -1;
		for (int32 Index : Indices)
		{
			if (Index == LastDeleted)
			{
				continue;
			}
			DeleteFn(Index);
			LastDeleted = Index;
		}
	};

	DeleteUniqueDescending(ConstraintIndicesToDelete, [&](int32 ConstraintIndex)
	{
		if (EditingPhysicsAsset->RemoveConstraintSetupAt(ConstraintIndex))
		{
			SelectionType = EPhysicsAssetEditorSelectionType::None;
			SelectedConstraintIndex = -1;
			MarkDirty();
			bConstraintGraphLayoutDirty = true;
		}
	});

	DeleteUniqueDescending(BodyIndicesToDelete, [&](int32 BodyIndex)
	{
		if (EditingPhysicsAsset->RemoveBodySetupAt(BodyIndex))
		{
			SelectionType = EPhysicsAssetEditorSelectionType::None;
			SelectedBodyIndex = -1;
			SelectedConstraintIndex = -1;
			SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
			SelectedPrimitiveIndex = -1;
			MarkDirty();
			bConstraintGraphLayoutDirty = true;
		}
	});

	ed::NodeId SelectedNodes[1];
	if (ed::GetSelectedNodes(SelectedNodes, 1) > 0)
	{
		int32 BodyIndex = -1;
		int32 ConstraintIndex = -1;
		const uint32 NodeId = PhysicsNodeIdToU32(SelectedNodes[0]);
		if (DecodeBodyNode(NodeId, BodyIndex)
			&& (SelectionType != EPhysicsAssetEditorSelectionType::Body || BodyIndex != SelectedBodyIndex))
		{
			SelectBody(BodyIndex);
		}
		else if (DecodeConstraintNode(NodeId, ConstraintIndex)
			&& (SelectionType != EPhysicsAssetEditorSelectionType::Constraint || ConstraintIndex != SelectedConstraintIndex))
		{
			SelectConstraint(ConstraintIndex);
		}
	}
	else
	{
		ed::LinkId SelectedLinks[1];
		if (ed::GetSelectedLinks(SelectedLinks, 1) > 0)
		{
			int32 ConstraintIndex = -1;
			if (DecodeConstraintLink(PhysicsLinkIdToU32(SelectedLinks[0]), ConstraintIndex)
				&& (SelectionType != EPhysicsAssetEditorSelectionType::Constraint || ConstraintIndex != SelectedConstraintIndex))
			{
				SelectConstraint(ConstraintIndex);
			}
		}
	}

	ed::End();
	ed::SetCurrentEditor(nullptr);
	ImGui::EndChild();
}

void FPhysicsAssetEditorWidget::RenderBoneTree(const FSkeletalMesh* MeshAsset, int32 BoneIndex)
{
	if (!MeshAsset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
	{
		return;
	}

	const FBone& Bone = MeshAsset->Bones[BoneIndex];
	const int32 BodyIndex = EditingPhysicsAsset ? EditingPhysicsAsset->FindBodyIndex(FName(Bone.Name)) : -1;
	const bool bHasBody = BodyIndex >= 0;

	FString Label = Bone.Name;

	bool bHasChildren = false;
	for (int32 Index = BoneIndex + 1; Index < static_cast<int32>(MeshAsset->Bones.size()); ++Index)
	{
		if (MeshAsset->Bones[Index].ParentIndex == BoneIndex)
		{
			bHasChildren = true;
			break;
		}
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (SelectedBoneIndex == BoneIndex
		&& (SelectionType == EPhysicsAssetEditorSelectionType::Bone || SelectionType == EPhysicsAssetEditorSelectionType::Body))
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (!bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	ImGui::PushID(BoneIndex);
	const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);
	if (bHasBody)
	{
		const ImVec2 ItemMin = ImGui::GetItemRectMin();
		const ImVec2 ItemMax = ImGui::GetItemRectMax();
		const float Radius = 3.5f;
		const ImVec2 Center(ItemMax.x - Radius - 6.0f, (ItemMin.y + ItemMax.y) * 0.5f);
		ImGui::GetWindowDrawList()->AddCircleFilled(Center, Radius, IM_COL32(80, 210, 190, 255));
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Body");
		}
	}
	if (ImGui::IsItemClicked())
	{
		if (bHasBody)
		{
			SelectBody(BodyIndex);
		}
		else
		{
			SelectBone(BoneIndex);
		}
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Add Body"))
		{
			SelectBone(BoneIndex);
			AddBodyForSelectedBone();
		}
		if (bHasBody && ImGui::MenuItem("Select Body"))
		{
			SelectBody(BodyIndex);
		}
		if (bHasBody && ImGui::MenuItem("Delete Body"))
		{
			SelectBody(BodyIndex);
			RemoveSelectedBody();
		}
		if (bHasBody && ImGui::MenuItem("Add Constraint To Parent"))
		{
			SelectBody(BodyIndex);
			AddConstraintToParentBody();
		}
		ImGui::EndPopup();
	}

	if (bOpen && bHasChildren)
	{
		for (int32 Index = BoneIndex + 1; Index < static_cast<int32>(MeshAsset->Bones.size()); ++Index)
		{
			if (MeshAsset->Bones[Index].ParentIndex == BoneIndex)
			{
				RenderBoneTree(MeshAsset, Index);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FPhysicsAssetEditorWidget::RenderViewportPanel()
{
	RenderViewportPanel(ImGui::GetContentRegionAvail());
}

void FPhysicsAssetEditorWidget::RenderViewportPanel(ImVec2 Size)
{
	ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (!VP || Size.x <= 0.0f || Size.y <= 0.0f)
	{
		ImGui::Dummy(Size);
		return;
	}

	VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

	if (VP->GetSRV())
	{
		ImGui::Image((ImTextureID)VP->GetSRV(), Size);
	}
	else
	{
		ImGui::Dummy(Size);
	}

	FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());

	constexpr float ToolbarHeight = 28.0f;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(ViewportPos, ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight), IM_COL32(40, 40, 40, 255));

	FViewportToolbarContext Context;
	Context.Renderer = &GEngine->GetRenderer();
	Context.Gizmo = ViewportClient.GetGizmo();
	Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
	Context.RenderOptions = &ViewportClient.GetRenderOptions();
	Context.ToolbarLeft = ViewportPos.x;
	Context.ToolbarTop = ViewportPos.y;
	Context.ToolbarWidth = Size.x;
	Context.bReservePlayStopSpace = false;
	Context.bShowAddActor = false;
	Context.OnCoordSystemToggled = [&]()
	{
		FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
		Settings.CoordSystem = (Settings.CoordSystem == EEditorCoordSystem::World) ? EEditorCoordSystem::Local : EEditorCoordSystem::World;
		ViewportClient.ApplyTransformSettingsToGizmo();
	};
	Context.OnSettingsChanged = [&]()
	{
		ViewportClient.ApplyTransformSettingsToGizmo();
	};
	Context.OnRenderViewModeExtras = [&]()
	{
		RenderPreviewToolbar();
	};

	FViewportToolbar::Render(Context);
}

void FPhysicsAssetEditorWidget::RenderPreviewToolbar()
{
	ImGui::TextUnformatted("Physics Asset");
	if (ImGui::Button(IsDirty() ? "Save *" : "Save"))
	{
		SaveAsset();
	}
	ImGui::SameLine();
	if (ImGui::Button("Show All Bones"))
	{
		bShowBones = true;
		ViewportClient.SetBoneDebugDrawMode(EBoneDebugDrawMode::AllBones);
		SyncPreviewSelection();
	}
	ImGui::SameLine();
	if (ImGui::Button("Hide Bones"))
	{
		bShowBones = false;
		ViewportClient.SetBoneDebugDrawMode(EBoneDebugDrawMode::SelectedOnly);
		SyncPreviewSelection();
	}
	ImGui::TextDisabled("Bones: %s", bShowBones ? "Visible" : "Hidden");
	ImGui::Checkbox("Bodies", &bShowBodies);
	ImGui::Checkbox("Solid Bodies", &bShowSolidBodies);
	ImGui::Checkbox("Constraints", &bShowConstraints);

	if (bShowBones)
	{
		const EBoneDebugDrawMode CurrentBoneDrawMode = ViewportClient.GetBoneDebugDrawMode();
		int32 BoneDrawMode = static_cast<int32>(CurrentBoneDrawMode);
		ImGui::Separator();
		ImGui::TextUnformatted("Bone Display");
		ImGui::RadioButton("Selected Bone", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::SelectedOnly));
		ImGui::RadioButton("All Bones", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::AllBones));
		if (BoneDrawMode != static_cast<int32>(CurrentBoneDrawMode))
		{
			ViewportClient.SetBoneDebugDrawMode(static_cast<EBoneDebugDrawMode>(BoneDrawMode));
			SyncPreviewSelection();
		}
	}
}

void FPhysicsAssetEditorWidget::RenderDetailsPanel()
{
	ImGui::TextUnformatted("Details");
	ImGui::Separator();

	if (!EditingPhysicsAsset)
	{
		ImGui::TextDisabled("No PhysicsAsset.");
		return;
	}

	switch (SelectionType)
	{
	case EPhysicsAssetEditorSelectionType::Bone:
	{
		FSkeletalMesh* MeshAsset = EditingMesh ? EditingMesh->GetSkeletalMeshAsset() : nullptr;
		if (!MeshAsset || SelectedBoneIndex < 0 || SelectedBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
		{
			ImGui::TextDisabled("No bone selected.");
			return;
		}

		const FBone& Bone = MeshAsset->Bones[SelectedBoneIndex];
		ImGui::Text("Bone: %s", Bone.Name.c_str());
		ImGui::Text("Parent: %d", Bone.ParentIndex);
		const int32 BodyIndex = EditingPhysicsAsset->FindBodyIndex(FName(Bone.Name));
		if (BodyIndex >= 0)
		{
			ImGui::Text("Body: %d", BodyIndex);
			if (ImGui::Button("Select Body", ImVec2(-1.0f, 0.0f)))
			{
				SelectBody(BodyIndex);
			}
		}
		else if (ImGui::Button("Add Body", ImVec2(-1.0f, 0.0f)))
		{
			AddBodyForSelectedBone();
		}
		break;
	}
	case EPhysicsAssetEditorSelectionType::Body:
		RenderBodyDetails();
		break;
	case EPhysicsAssetEditorSelectionType::Constraint:
		RenderConstraintDetails();
		break;
	default:
		ImGui::TextDisabled("Select a bone, body, or constraint.");
		break;
	}
}

void FPhysicsAssetEditorWidget::RenderBodyDetails()
{
	USkeletalBodySetup* BodySetup = EditingPhysicsAsset->GetBodySetup(SelectedBodyIndex);
	if (!BodySetup)
	{
		ImGui::TextDisabled("Invalid body selection.");
		return;
	}

	ImGui::Text("Body: %s", BodySetup->GetBoneName().ToString().c_str());
	ImGui::Text("Index: %d", SelectedBodyIndex);
	if (ImGui::Checkbox("Skip Scale From Animation", &BodySetup->bSkipScaleFromAnimation))
	{
		MarkDirty();
		EditingPhysicsAsset->RefreshPhysicsAssetChange();
	}

	if (ImGui::Button("+ Box", ImVec2(92.0f, 0.0f)))
	{
		BodySetup->GetAggGeom().BoxElems.push_back(FKBoxElem());
		SelectPrimitive(EPhysicsAssetPrimitiveType::Box, static_cast<int32>(BodySetup->GetAggGeom().BoxElems.size()) - 1);
		MarkDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Sphere", ImVec2(92.0f, 0.0f)))
	{
		BodySetup->GetAggGeom().SphereElems.push_back(FKSphereElem());
		SelectPrimitive(EPhysicsAssetPrimitiveType::Sphere, static_cast<int32>(BodySetup->GetAggGeom().SphereElems.size()) - 1);
		MarkDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Capsule", ImVec2(92.0f, 0.0f)))
	{
		BodySetup->GetAggGeom().SphylElems.push_back(FKSphylElem());
		SelectPrimitive(EPhysicsAssetPrimitiveType::Capsule, static_cast<int32>(BodySetup->GetAggGeom().SphylElems.size()) - 1);
		MarkDirty();
	}

	ImGui::Separator();
	FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();

	if (ImGui::TreeNodeEx("Boxes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int32 Index = 0; Index < static_cast<int32>(AggGeom.BoxElems.size()); ++Index)
		{
			ImGui::PushID(Index);
			FKBoxElem& Box = AggGeom.BoxElems[Index];
			if (ImGui::TreeNodeEx("Box", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const bool bSelectedPrimitive = SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Box && SelectedPrimitiveIndex == Index;
				if (ImGui::Selectable("Edit With Gizmo", bSelectedPrimitive))
				{
					SelectPrimitive(EPhysicsAssetPrimitiveType::Box, Index);
				}
				bool bChanged = false;
				bChanged |= DragVector("Center", Box.Center);
				bChanged |= DragRotator("Rotation", Box.Rotation);
				bChanged |= ImGui::DragFloat("X", &Box.X, 0.1f, 0.1f, 100000.0f);
				bChanged |= ImGui::DragFloat("Y", &Box.Y, 0.1f, 0.1f, 100000.0f);
				bChanged |= ImGui::DragFloat("Z", &Box.Z, 0.1f, 0.1f, 100000.0f);
				if (bChanged)
				{
					MarkDirty();
					EditingPhysicsAsset->RefreshPhysicsAssetChange();
				}
				if (ImGui::Button("Delete Box", ImVec2(-1.0f, 0.0f)))
				{
					AggGeom.BoxElems.erase(AggGeom.BoxElems.begin() + Index);
					SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
					SelectedPrimitiveIndex = -1;
					MarkDirty();
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Spheres", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int32 Index = 0; Index < static_cast<int32>(AggGeom.SphereElems.size()); ++Index)
		{
			ImGui::PushID(Index);
			FKSphereElem& Sphere = AggGeom.SphereElems[Index];
			if (ImGui::TreeNodeEx("Sphere", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const bool bSelectedPrimitive = SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Sphere && SelectedPrimitiveIndex == Index;
				if (ImGui::Selectable("Edit With Gizmo", bSelectedPrimitive))
				{
					SelectPrimitive(EPhysicsAssetPrimitiveType::Sphere, Index);
				}
				bool bChanged = false;
				bChanged |= DragVector("Center", Sphere.Center);
				bChanged |= ImGui::DragFloat("Radius", &Sphere.Radius, 0.1f, 0.1f, 100000.0f);
				if (bChanged)
				{
					MarkDirty();
					EditingPhysicsAsset->RefreshPhysicsAssetChange();
				}
				if (ImGui::Button("Delete Sphere", ImVec2(-1.0f, 0.0f)))
				{
					AggGeom.SphereElems.erase(AggGeom.SphereElems.begin() + Index);
					SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
					SelectedPrimitiveIndex = -1;
					MarkDirty();
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Capsules", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int32 Index = 0; Index < static_cast<int32>(AggGeom.SphylElems.size()); ++Index)
		{
			ImGui::PushID(Index);
			FKSphylElem& Capsule = AggGeom.SphylElems[Index];
			if (ImGui::TreeNodeEx("Capsule", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const bool bSelectedPrimitive = SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Capsule && SelectedPrimitiveIndex == Index;
				if (ImGui::Selectable("Edit With Gizmo", bSelectedPrimitive))
				{
					SelectPrimitive(EPhysicsAssetPrimitiveType::Capsule, Index);
				}
				bool bChanged = false;
				bChanged |= DragVector("Center", Capsule.Center);
				bChanged |= DragRotator("Rotation", Capsule.Rotation);
				bChanged |= ImGui::DragFloat("Radius", &Capsule.Radius, 0.1f, 0.1f, 100000.0f);
				bChanged |= ImGui::DragFloat("Length", &Capsule.Length, 0.1f, 0.1f, 100000.0f);
				if (bChanged)
				{
					MarkDirty();
					EditingPhysicsAsset->RefreshPhysicsAssetChange();
				}
				if (ImGui::Button("Delete Capsule", ImVec2(-1.0f, 0.0f)))
				{
					AggGeom.SphylElems.erase(AggGeom.SphylElems.begin() + Index);
					SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
					SelectedPrimitiveIndex = -1;
					MarkDirty();
					ImGui::TreePop();
					ImGui::PopID();
					break;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	ImGui::Separator();
	if (ImGui::Button("Delete Body", ImVec2(-1.0f, 0.0f)))
	{
		RemoveSelectedBody();
	}
}

void FPhysicsAssetEditorWidget::RenderConstraintDetails()
{
	UPhysicsConstraintTemplate* Constraint = EditingPhysicsAsset->GetConstraintSetup(SelectedConstraintIndex);
	if (!Constraint)
	{
		ImGui::TextDisabled("Invalid constraint selection.");
		return;
	}

	FConstraintInstance& Instance = Constraint->GetDefaultInstance();
	ImGui::Text("Constraint: %s", Constraint->GetConstraintName().ToString().c_str());
	ImGui::Text("Parent: %s", Constraint->GetParentBoneName().ToString().c_str());
	ImGui::Text("Child: %s", Constraint->GetChildBoneName().ToString().c_str());

	bool bChanged = false;
	bChanged |= ImGui::Checkbox("Disable Collision", &Instance.bDisableCollision);
	ImGui::Separator();
	ImGui::TextUnformatted("Linear Motion");
	bChanged |= MotionCombo("X", Instance.LinearXMotion);
	bChanged |= MotionCombo("Y", Instance.LinearYMotion);
	bChanged |= MotionCombo("Z", Instance.LinearZMotion);
	bChanged |= ImGui::DragFloat("Linear Limit", &Instance.LinearLimitSize, 0.1f, 0.0f, 100000.0f);

	ImGui::Separator();
	ImGui::TextUnformatted("Angular Motion");
	bChanged |= MotionCombo("Swing 1", Instance.Swing1Motion);
	bChanged |= MotionCombo("Swing 2", Instance.Swing2Motion);
	bChanged |= MotionCombo("Twist", Instance.TwistMotion);
	bChanged |= ImGui::DragFloat("Swing 1 Limit", &Instance.Swing1LimitDegrees, 0.1f, 0.0f, 180.0f);
	bChanged |= ImGui::DragFloat("Swing 2 Limit", &Instance.Swing2LimitDegrees, 0.1f, 0.0f, 180.0f);
	bChanged |= ImGui::DragFloat("Twist Min", &Instance.TwistLimitMinDegrees, 0.1f, -180.0f, 180.0f);
	bChanged |= ImGui::DragFloat("Twist Max", &Instance.TwistLimitMaxDegrees, 0.1f, -180.0f, 180.0f);

	if (bChanged)
	{
		Constraint->SetDefaultInstance(Instance);
		MarkDirty();
		EditingPhysicsAsset->RefreshPhysicsAssetChange();
	}

	ImGui::Separator();
	if (ImGui::Button("Delete Constraint", ImVec2(-1.0f, 0.0f)))
	{
		RemoveSelectedConstraint();
	}
}

void FPhysicsAssetEditorWidget::SelectBone(int32 BoneIndex)
{
	SelectionType = EPhysicsAssetEditorSelectionType::Bone;
	SelectedBoneIndex = BoneIndex;
	SelectedConstraintIndex = -1;
	SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
	SelectedPrimitiveIndex = -1;
	SyncPreviewSelection();
}

void FPhysicsAssetEditorWidget::SelectBody(int32 BodyIndex)
{
	SelectionType = EPhysicsAssetEditorSelectionType::Body;
	SelectedBodyIndex = BodyIndex;
	SelectedConstraintIndex = -1;
	SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
	SelectedPrimitiveIndex = -1;

	if (EditingMesh && EditingPhysicsAsset)
	{
		if (USkeletalBodySetup* BodySetup = EditingPhysicsAsset->GetBodySetup(BodyIndex))
		{
			SelectedBoneIndex = FindBoneIndexByName(EditingMesh->GetSkeletalMeshAsset(), BodySetup->GetBoneName());
			SelectFirstPrimitiveForBody(BodySetup);
		}
	}
	SyncPreviewSelection();
}

void FPhysicsAssetEditorWidget::SelectConstraint(int32 ConstraintIndex)
{
	SelectionType = EPhysicsAssetEditorSelectionType::Constraint;
	SelectedConstraintIndex = ConstraintIndex;
	SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
	SelectedPrimitiveIndex = -1;
	SyncPreviewSelection();
}

void FPhysicsAssetEditorWidget::SelectPrimitive(EPhysicsAssetPrimitiveType PrimitiveType, int32 PrimitiveIndex)
{
	SelectedPrimitiveType = PrimitiveType;
	SelectedPrimitiveIndex = PrimitiveIndex;
	SyncPrimitiveGizmo();
}

void FPhysicsAssetEditorWidget::SelectFirstPrimitiveForBody(USkeletalBodySetup* BodySetup)
{
	if (!BodySetup)
	{
		return;
	}

	const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
	if (!AggGeom.BoxElems.empty())
	{
		SelectPrimitive(EPhysicsAssetPrimitiveType::Box, 0);
	}
	else if (!AggGeom.SphereElems.empty())
	{
		SelectPrimitive(EPhysicsAssetPrimitiveType::Sphere, 0);
	}
	else if (!AggGeom.SphylElems.empty())
	{
		SelectPrimitive(EPhysicsAssetPrimitiveType::Capsule, 0);
	}
}

void FPhysicsAssetEditorWidget::AddBodyForSelectedBone()
{
	if (!EditingMesh || !EditingPhysicsAsset)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = EditingMesh->GetSkeletalMeshAsset();
	if (!MeshAsset || SelectedBoneIndex < 0 || SelectedBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
	{
		return;
	}

	const FName BoneName(MeshAsset->Bones[SelectedBoneIndex].Name);
	USkeletalBodySetup* BodySetup = EditingPhysicsAsset->AddBodySetup(BoneName);
	if (!BodySetup)
	{
		return;
	}

	if (BodySetup->GetAggGeom().IsEmpty())
	{
		BodySetup->GetAggGeom().BoxElems.push_back(FKBoxElem());
	}

	EditingPhysicsAsset->UpdateBodySetupIndexMap();
	const int32 BodyIndex = EditingPhysicsAsset->FindBodyIndex(BoneName);
	SelectBody(BodyIndex);
	MarkDirty();
	bConstraintGraphLayoutDirty = true;
}

void FPhysicsAssetEditorWidget::RemoveSelectedBody()
{
	if (!EditingPhysicsAsset || SelectedBodyIndex < 0)
	{
		return;
	}

	if (EditingPhysicsAsset->RemoveBodySetupAt(SelectedBodyIndex))
	{
		SelectionType = EPhysicsAssetEditorSelectionType::None;
		SelectedBodyIndex = -1;
		SelectedConstraintIndex = -1;
		SelectedPrimitiveType = EPhysicsAssetPrimitiveType::None;
		SelectedPrimitiveIndex = -1;
		MarkDirty();
		bConstraintGraphLayoutDirty = true;
		SyncPreviewSelection();
	}
}

void FPhysicsAssetEditorWidget::AddConstraintToParentBody()
{
	if (!EditingMesh || !EditingPhysicsAsset || SelectedBodyIndex < 0)
	{
		return;
	}

	FSkeletalMesh* MeshAsset = EditingMesh->GetSkeletalMeshAsset();
	USkeletalBodySetup* ChildBody = EditingPhysicsAsset->GetBodySetup(SelectedBodyIndex);
	if (!MeshAsset || !ChildBody)
	{
		return;
	}

	const int32 ChildBoneIndex = FindBoneIndexByName(MeshAsset, ChildBody->GetBoneName());
	const int32 ParentBodyIndex = FindParentBodyIndexForBone(MeshAsset, ChildBoneIndex);
	USkeletalBodySetup* ParentBody = EditingPhysicsAsset->GetBodySetup(ParentBodyIndex);
	if (!ParentBody)
	{
		return;
	}

	const FName ConstraintName = MakeUniqueConstraintName(ParentBody->GetBoneName(), ChildBody->GetBoneName());
	UPhysicsConstraintTemplate* Constraint = EditingPhysicsAsset->AddConstraintSetup(
		ConstraintName,
		ParentBody->GetBoneName(),
		ChildBody->GetBoneName());

	if (Constraint)
	{
		SelectConstraint(EditingPhysicsAsset->FindConstraintIndex(ConstraintName));
		MarkDirty();
		bConstraintGraphLayoutDirty = true;
	}
}

void FPhysicsAssetEditorWidget::RemoveSelectedConstraint()
{
	if (!EditingPhysicsAsset || SelectedConstraintIndex < 0)
	{
		return;
	}

	if (EditingPhysicsAsset->RemoveConstraintSetupAt(SelectedConstraintIndex))
	{
		SelectionType = EPhysicsAssetEditorSelectionType::None;
		SelectedConstraintIndex = -1;
		MarkDirty();
		bConstraintGraphLayoutDirty = true;
		SyncPreviewSelection();
	}
}

int32 FPhysicsAssetEditorWidget::FindBoneIndexByName(const FSkeletalMesh* MeshAsset, FName BoneName) const
{
	if (!MeshAsset)
	{
		return -1;
	}

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
	{
		if (FName(MeshAsset->Bones[BoneIndex].Name) == BoneName)
		{
			return BoneIndex;
		}
	}

	return -1;
}

int32 FPhysicsAssetEditorWidget::FindParentBodyIndexForBone(const FSkeletalMesh* MeshAsset, int32 BoneIndex) const
{
	if (!MeshAsset || !EditingPhysicsAsset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
	{
		return -1;
	}

	int32 ParentIndex = MeshAsset->Bones[BoneIndex].ParentIndex;
	while (ParentIndex >= 0 && ParentIndex < static_cast<int32>(MeshAsset->Bones.size()))
	{
		const FName ParentBoneName(MeshAsset->Bones[ParentIndex].Name);
		const int32 ParentBodyIndex = EditingPhysicsAsset->FindBodyIndex(ParentBoneName);
		if (ParentBodyIndex >= 0)
		{
			return ParentBodyIndex;
		}
		ParentIndex = MeshAsset->Bones[ParentIndex].ParentIndex;
	}

	return -1;
}

FName FPhysicsAssetEditorWidget::MakeUniqueConstraintName(FName ParentBoneName, FName ChildBoneName) const
{
	if (!EditingPhysicsAsset)
	{
		return FName::None;
	}

	for (int32 Suffix = 0; Suffix < 10000; ++Suffix)
	{
		const FName Candidate(MakeConstraintNameString(ParentBoneName, ChildBoneName, Suffix));
		if (!EditingPhysicsAsset->FindConstraintSetup(Candidate))
		{
			return Candidate;
		}
	}

	return FName::None;
}

FVector FPhysicsAssetEditorWidget::GetBodyWorldLocation(const USkeletalBodySetup* BodySetup) const
{
	USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
	FSkeletalMesh* MeshAsset = EditingMesh ? EditingMesh->GetSkeletalMeshAsset() : nullptr;
	if (!BodySetup || !MeshComp || !MeshAsset)
	{
		return FVector::ZeroVector;
	}

	const int32 BoneIndex = FindBoneIndexByName(MeshAsset, BodySetup->GetBoneName());
	return BoneIndex >= 0 ? MeshComp->GetBoneLocationByIndex(BoneIndex) : FVector::ZeroVector;
}

FQuat FPhysicsAssetEditorWidget::GetBodyWorldRotation(const USkeletalBodySetup* BodySetup) const
{
	USkeletalMeshComponent* MeshComp = ViewportClient.GetPreviewMeshComponent();
	FSkeletalMesh* MeshAsset = EditingMesh ? EditingMesh->GetSkeletalMeshAsset() : nullptr;
	if (!BodySetup || !MeshComp || !MeshAsset)
	{
		return FQuat::Identity;
	}

	const int32 BoneIndex = FindBoneIndexByName(MeshAsset, BodySetup->GetBoneName());
	return BoneIndex >= 0 ? MeshComp->GetBoneQuatByIndex(BoneIndex) : FQuat::Identity;
}

FVector FPhysicsAssetEditorWidget::BodyLocalToWorld(const USkeletalBodySetup* BodySetup, const FVector& LocalPosition) const
{
	return GetBodyWorldLocation(BodySetup) + GetBodyWorldRotation(BodySetup).RotateVector(LocalPosition);
}

FVector FPhysicsAssetEditorWidget::WorldDeltaToBodyLocal(const USkeletalBodySetup* BodySetup, const FVector& WorldDelta) const
{
	return GetBodyWorldRotation(BodySetup).Inverse().RotateVector(WorldDelta);
}

void FPhysicsAssetEditorWidget::SyncPreviewSelection()
{
	if (!EditingMesh)
	{
		return;
	}

	int32 BoneIndexForPreview = bShowBones ? SelectedBoneIndex : -1;
	if (!bShowBones)
	{
		ViewportClient.SetBoneDebugDrawMode(EBoneDebugDrawMode::SelectedOnly);
	}

	if (bShowBones && SelectionType == EPhysicsAssetEditorSelectionType::Constraint && EditingPhysicsAsset)
	{
		if (UPhysicsConstraintTemplate* Constraint = EditingPhysicsAsset->GetConstraintSetup(SelectedConstraintIndex))
		{
			BoneIndexForPreview = FindBoneIndexByName(EditingMesh->GetSkeletalMeshAsset(), Constraint->GetChildBoneName());
		}
	}

	ViewportClient.SetSelectedBone(EditingMesh, BoneIndexForPreview);
	SyncPrimitiveGizmo();
}

void FPhysicsAssetEditorWidget::SyncPrimitiveGizmo()
{
	UGizmoComponent* Gizmo = ViewportClient.GetGizmo();
	if (!Gizmo)
	{
		return;
	}

	if (SelectionType == EPhysicsAssetEditorSelectionType::Body
		&& PrimitiveGizmoTarget
		&& PrimitiveGizmoTarget->IsValid())
	{
		Gizmo->SetTarget(PrimitiveGizmoTarget.get());
		return;
	}

	if (SelectionType != EPhysicsAssetEditorSelectionType::Bone)
	{
		Gizmo->Deactivate();
	}
}

void FPhysicsAssetEditorWidget::UpdateSolidPreview()
{
	if (!SolidPreviewComponent)
	{
		return;
	}

	const bool bEnabled = EditingPhysicsAsset && ViewportClient.GetPreviewWorld() && bShowBodies && bShowSolidBodies;
	SolidPreviewComponent->RebuildFromEditor(*this, bEnabled);
}

void FPhysicsAssetEditorWidget::RenderPhysicsDebug()
{
	if (!EditingPhysicsAsset || !ViewportClient.GetPreviewWorld())
	{
		return;
	}

	if (bShowBones)
	{
		ViewportClient.SetBoneDebugDrawMode(ViewportClient.GetBoneDebugDrawMode());
	}

	if (bShowBodies)
	{
		for (int32 BodyIndex = 0; BodyIndex < EditingPhysicsAsset->GetBodySetupCount(); ++BodyIndex)
		{
			DrawBodySetupDebug(
				EditingPhysicsAsset->GetBodySetup(BodyIndex),
				SelectionType == EPhysicsAssetEditorSelectionType::Body && SelectedBodyIndex == BodyIndex);
		}
	}

	if (bShowConstraints)
	{
		for (int32 ConstraintIndex = 0; ConstraintIndex < EditingPhysicsAsset->GetConstraintSetupCount(); ++ConstraintIndex)
		{
			DrawConstraintDebug(
				EditingPhysicsAsset->GetConstraintSetup(ConstraintIndex),
				SelectionType == EPhysicsAssetEditorSelectionType::Constraint && SelectedConstraintIndex == ConstraintIndex);
		}
	}
}

void FPhysicsAssetEditorWidget::DrawBodySetupDebug(const USkeletalBodySetup* BodySetup, bool bSelected)
{
	UWorld* World = ViewportClient.GetPreviewWorld();
	if (!World || !BodySetup)
	{
		return;
	}

	const FQuat BodyRotation = GetBodyWorldRotation(BodySetup);
	const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
	const FColor BodyColor = bSelected ? FColor(255, 220, 60) : FColor(90, 210, 255);
	const FColor PrimitiveColor = FColor(255, 245, 120);

	for (int32 Index = 0; Index < static_cast<int32>(AggGeom.BoxElems.size()); ++Index)
	{
		const FKBoxElem& Box = AggGeom.BoxElems[Index];
		const bool bSelectedPrimitive = bSelected && SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Box && SelectedPrimitiveIndex == Index;
		const FQuat Rotation = (BodyRotation * Box.Rotation.ToQuaternion()).GetNormalized();
		DrawDebugOrientedBox(
			World,
			BodyLocalToWorld(BodySetup, Box.Center),
			FVector(Box.X, Box.Y, Box.Z) * 0.5f,
			Rotation,
			bSelectedPrimitive ? PrimitiveColor : BodyColor);
	}

	for (int32 Index = 0; Index < static_cast<int32>(AggGeom.SphereElems.size()); ++Index)
	{
		const FKSphereElem& Sphere = AggGeom.SphereElems[Index];
		const bool bSelectedPrimitive = bSelected && SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Sphere && SelectedPrimitiveIndex == Index;
		DrawDebugSphere(
			World,
			BodyLocalToWorld(BodySetup, Sphere.Center),
			Sphere.Radius,
			24,
			bSelectedPrimitive ? PrimitiveColor : BodyColor,
			0.0f);
	}

	for (int32 Index = 0; Index < static_cast<int32>(AggGeom.SphylElems.size()); ++Index)
	{
		const FKSphylElem& Capsule = AggGeom.SphylElems[Index];
		const bool bSelectedPrimitive = bSelected && SelectedPrimitiveType == EPhysicsAssetPrimitiveType::Capsule && SelectedPrimitiveIndex == Index;
		const FQuat Rotation = (BodyRotation * Capsule.Rotation.ToQuaternion()).GetNormalized();
		DrawDebugCapsule(
			World,
			BodyLocalToWorld(BodySetup, Capsule.Center),
			Capsule.Radius,
			Capsule.Length,
			Rotation,
			bSelectedPrimitive ? PrimitiveColor : BodyColor);
	}
}

void FPhysicsAssetEditorWidget::DrawConstraintDebug(const UPhysicsConstraintTemplate* Constraint, bool bSelected)
{
	UWorld* World = ViewportClient.GetPreviewWorld();
	if (!World || !EditingPhysicsAsset || !Constraint)
	{
		return;
	}

	const USkeletalBodySetup* ParentBody = EditingPhysicsAsset->FindBodySetup(Constraint->GetParentBoneName());
	const USkeletalBodySetup* ChildBody = EditingPhysicsAsset->FindBodySetup(Constraint->GetChildBoneName());
	if (!ParentBody || !ChildBody)
	{
		return;
	}

	const FVector ParentLocation = GetBodyWorldLocation(ParentBody);
	const FVector ChildLocation = GetBodyWorldLocation(ChildBody);
	const FColor Color = bSelected ? FColor(255, 120, 80) : FColor(200, 150, 255);
	DrawDebugLine(World, ParentLocation, ChildLocation, Color, 0.0f);
	DrawDebugPoint(World, ParentLocation, bSelected ? 0.08f : 0.05f, Color, 0.0f);
	DrawDebugPoint(World, ChildLocation, bSelected ? 0.08f : 0.05f, Color, 0.0f);
}
