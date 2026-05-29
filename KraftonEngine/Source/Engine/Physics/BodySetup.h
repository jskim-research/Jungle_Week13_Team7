#pragma once

#include "Object/Object.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

#include "Source/Engine/Physics/BodySetup.generated.h"

USTRUCT()
struct FKBoxElem
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Center")
	FVector Center = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Rotation")
	FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="X")
	float X = 100.0f;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Y")
	float Y = 100.0f;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Z")
	float Z = 100.0f;
};

USTRUCT()
struct FKSphereElem
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Center")
	FVector Center = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Radius")
	float Radius = 50.0f;
};

USTRUCT()
struct FKSphylElem
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Center")
	FVector Center = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Rotation")
	FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Radius")
	float Radius = 50.0f;

	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Length")
	float Length = 100.0f;
};

USTRUCT()
struct FKAggregateGeom
{
	GENERATED_BODY()

	TArray<FKBoxElem> BoxElems;

	TArray<FKSphereElem> SphereElems;

	TArray<FKSphylElem> SphylElems;

	bool IsEmpty() const
	{
		return BoxElems.empty() && SphereElems.empty() && SphylElems.empty();
	}
};

UCLASS()
class UBodySetup : public UObject
{
public:
	GENERATED_BODY()

	FKAggregateGeom& GetAggGeom() { return AggGeom; }
	const FKAggregateGeom& GetAggGeom() const { return AggGeom; }

	bool HasSimpleCollision() const { return !AggGeom.IsEmpty(); }

private:
	UPROPERTY(Edit, Save, Category="Collision", DisplayName="Aggregate Geometry", Type=Struct)
	FKAggregateGeom AggGeom;
};
