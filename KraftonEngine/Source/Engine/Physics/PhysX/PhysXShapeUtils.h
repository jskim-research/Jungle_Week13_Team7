#pragma once

namespace physx
{
	class PxShape;
	class PxQuat;
}

class UPrimitiveComponent;

namespace PhysXShapeUtils
{
	// PxCapsuleGeometry long axis is +X; UCapsuleComponent / debug wire use +Z.
	physx::PxQuat GetCapsuleAxisCorrectionQuat();

	void SetupFilterData(physx::PxShape* Shape, UPrimitiveComponent* Comp);
	bool ShouldUseTriggerShape(UPrimitiveComponent* Comp);
	void FinalizeShape(physx::PxShape* Shape, UPrimitiveComponent* Comp);
}
