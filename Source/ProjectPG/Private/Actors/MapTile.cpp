// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/MapTile.h"

// Sets default values
AMapTile::AMapTile()
{
	PrimaryActorTick.bCanEverTick = false;

	_staticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	SetRootComponent(_staticMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> staticMesh(
		TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	if (!staticMesh.Succeeded())
		return;

	_staticMesh->SetStaticMesh(staticMesh.Object);

	_staticMesh->SetRelativeScale3D(0.1 * FVector::OneVector);
}

// Called when the game starts or when spawned
void AMapTile::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AMapTile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AMapTile::SetType(ETileType Type)
{FString materialPath = TEXT("");

	_type = Type;

	switch (_type)
	{
	case ETileType::Spawn:
		materialPath = TEXT("/Script/Engine.Material'/Game/PG/Material/MT_Spawn.MT_Spawn'");
		break;
	case ETileType::Exit:
		materialPath = TEXT("/Script/Engine.Material'/Game/PG/Material/MT_Exit.MT_Exit'");
		break;
	case ETileType::Road:
		materialPath = TEXT("/Script/Engine.Material'/Game/PG/Material/MT_Road.MT_Road'");
		break;
	case ETileType::Obstacle:
		materialPath = TEXT("/Script/Engine.Material'/Game/PG/Material/MT_Obstacle.MT_Obstacle'");
		break;
	case ETileType::WarZone:
		materialPath = TEXT("/Script/Engine.Material'/Game/PG/Material/MT_WarZone.MT_WarZone'");
		break;
	case ETileType::None:
		break;
	}

	UMaterial* Material = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *materialPath));
	if (!IsValid(Material))
		return;

	_staticMesh->SetMaterial(0, Material);
}

ETileType AMapTile::GetType() const
{
	return _type;
}
