// 전함 무기 — 주포·미사일 발사와 폭발.
// 2026-09-26 SOLID(한 책임): 전함 액터(APGBattleshipActor)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 전함 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 전함 액터에 그대로 두고 Ship-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 전함 액터의 비공개 멤버를 읽어야 해서 전함 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGBattleshipActor.h"
#include "PGShipWeapons.generated.h"

UCLASS(Transient)
class UPGShipWeapons : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGBattleshipActor* InShip) { Ship = InShip; }
	virtual UWorld* GetWorld() const override { return Ship ? Ship->GetWorld() : nullptr; }

	// 미사일을 쏜다. 조준점 근처의 대상을 쫓아간다(없으면 조준점으로 곧장).
	// 주포(즉시 명중)와 나눈 이유: 하늘을 나는 드래곤은 즉시 명중으로 맞히면 공중전이 되지 않는다.
	bool FireMissile(const FVector& AimPoint, AActor* AimActor, AController* Shooter);
	// 주포를 쏜다. AimPoint 는 조준점(월드). 장전 중이면 false.
	// 조종석에 선 사람이 화면 가운데로 겨눈 곳에 쏜다 — 탱크 주포(APGTankPawn::FireMainGun)와 같은 방식이다.
	bool FireCannon(const FVector& AimPoint, AController* Shooter);
	void BuildCannons();                           // 포구 자리와 빔 메시
	void SpawnCannonImpactFlash(const FVector& Where, bool bBig); // 주포 탄착 번쩍임(.cpp 주석)
	// 멀티(9/27): 주포 한 발의 그림(빔 + 탄착 번쩍임). 서버가 MulticastCannonFx 로 모든 컴퓨터에서 부른다.
	void PlayCannonFx(int32 MuzzleIndex, const FVector& Impact, bool bBig);
	// 부풀며 사라지는 빛나는 구. 주포 탄착과 추락 폭발이 같이 쓴다(.cpp 주석 참고).
	void SpawnBlastBall(const FVector& Where, float MaxRadiusM, float Seconds);
	void Explode(const FVector& Location, AActor* DirectHit, AController* Shooter);

private:
	UPROPERTY()
	TObjectPtr<APGBattleshipActor> Ship;

	TArray<FVector> MuzzleLocals;
	int32 NextMuzzle = 0;
	double LastCannonTime = -100.0;
	double LastMissileTime = -100.0;
	FString LastTargetName;
};
