// 맵 정보 창구 — "이번 판 맵" 에 대해 다른 시스템이 물어볼 것만 모은 인터페이스(IPGMapInfo)와, 그것을 찾아 주는 월드 서브시스템.
//
// 왜 만들었나(2026-09-26 SOLID — 의존 역전): 스포너·월드 루팅·피날레·드래곤·콘솔 명령 6곳이
//   GetActorOfClass(AWarZoneFootprintPreview) + Cast 로 맵 액터를 직접 찾아 썼다. 그래서
//   ① 맵 액터 헤더(필드 200여 개)가 바뀔 때마다 그 6곳이 다시 컴파일되고,
//   ② 맵을 만드는 방식을 바꾸거나(예: 전용 서버에서 설계도만 받는 클라 쪽 맵) 시험용 가짜 맵을 넣을 수 없었고,
//   ③ 매번 월드 전체에서 액터를 찾았다.
//   이제 읽는 쪽은 "맵 정보" 라는 약속(IPGMapInfo)만 알고, 누가 그 약속을 지키는지는 모른다.
// 쓰는 법: if (IPGMapInfo* Map = UPGMapInfoSubsystem::FindMap(this)) { Map->GetLevelDesignPoints() ... }
// 맵 쪽: BeginPlay 에서 Register, EndPlay 에서 Unregister (AWarZoneFootprintPreview 가 한다).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Subsystems/WorldSubsystem.h"
#include "LevelDesign/PGLevelDesignTypes.h"
#include "PGMapInfo.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UPGMapInfo : public UInterface
{
	GENERATED_BODY()
};

class PROJECTPG_API IPGMapInfo
{
	GENERATED_BODY()

public:
	// 시작·탈출·괴물·전리품·임무 후보 자리(데이터만). 다 만들어지기 전에는 비어 있거나 만드는 중이다.
	virtual const TArray<FLevelDesignPoint>& GetLevelDesignPoints() const = 0;
	// 게임 지점이 확정됐나(안전 검사까지 끝남). 스포너는 이게 true 가 된 뒤에 물건을 놓는다.
	virtual bool AreLevelDesignPointsBuilt() const = 0;
	// 확정될 때 한 번 알림.
	virtual FOnLevelDesignPointsBuilt& OnLevelDesignPointsBuiltEvent() = 0;
	// 워존 바닥 칸들의 가운데(월드 좌표).
	virtual TArray<FVector> GetWarZoneCellCentres() const = 0;
	// 맵 가운데(월드 좌표). 경계·거리 계산의 기준.
	virtual FVector GetMapCentre() const = 0;
	// 한 구역을 박살내 구덩이로 만든다(피날레). 맵이 연출을 못 하면 아무것도 안 해도 된다.
	virtual void CollapseRegion(const FVector& WorldCentre, float RadiusCm, float Seconds) = 0;
	virtual bool IsCollapsing() const = 0;
};

UCLASS()
class PROJECTPG_API UPGMapInfoSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 이 월드의 맵. 없으면 nullptr(맵을 아직 안 만들었거나 맵이 없는 레벨).
	static IPGMapInfo* FindMap(const UObject* WorldContext);

	void RegisterMap(UObject* MapObject);
	void UnregisterMap(UObject* MapObject);
	IPGMapInfo* GetMap() const;

private:
	// 인터페이스 포인터만 들고 있으면 GC 가 객체를 모른다 — 약한 포인터로 살아 있는지 확인한 뒤에만 넘긴다.
	TWeakObjectPtr<UObject> MapObject;
};
