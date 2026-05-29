#pragma once

namespace physx
{
	class PxShape;
}

class UPrimitiveComponent;

namespace PhysXShapeUtils
{
	void SetupFilterData(physx::PxShape* Shape, UPrimitiveComponent* Comp);
	bool ShouldUseTriggerShape(UPrimitiveComponent* Comp);
	void FinalizeShape(physx::PxShape* Shape, UPrimitiveComponent* Comp);
}
