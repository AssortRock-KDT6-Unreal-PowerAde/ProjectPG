// 겉모습 블루프린트 연결 (프로젝트 설정 > Game > ProjectPG Visuals). (2026-09-23 블루프린트 분리 2단계)
//
// 왜 있나: 소품·무대 액터의 모델·재질·이펙트를 코드에 경로로 적어 두면, 모델 하나 바꾸는 데도 코드를 고쳐 빌드해야 한다.
//   그래서 각 C++ 액터에 "모델 칸"(UPROPERTY, 기본값 = 원래 쓰던 에셋)을 열고, 그 액터의 블루프린트 자식(BP_PG...)에서 에셋을 고르게 했다.
//   이 설정은 "게임이 어느 블루프린트를 쓸지" 만 적는다. 비어 있거나 못 읽으면 C++ 클래스(원래 모습)를 그대로 쓴다 — 옮기는 중에도 안 깨지게.
// 화면(UI) WBP 연결은 ProjectPG Flow > Screens 에 있다(UPGFlowSettings).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PGVisualSettings.generated.h"

class UPGMonsterLookSet;
class UPGEffectSet;
class UPGFlowStageSet;
class UPGTitleIntroSet;
class UPGMapVisualSet;
class UDataTable;
class UStaticMesh;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ProjectPG Visuals"))
class PROJECTPG_API UPGVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPGVisualSettings();

	static const UPGVisualSettings& Get() { return *GetDefault<UPGVisualSettings>(); }

	// 설정에 적힌 블루프린트 클래스를 읽어 돌려준다. 비었거나, 못 읽었거나, Fallback 의 자식이 아니면 Fallback(C++ 클래스)을 돌려준다.
	static UClass* ResolveActorClass(const TSoftClassPtr<AActor>& Designed, UClass* Fallback);

	// 외진 보상 거점(추락 헬기 + 통신 캠프). 부모가 PGRemoteOutpostActor 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> RemoteOutpostClass;

	// 출구 검문소 꾸밈(펜스·문틀·초소·컨테이너 등). 부모가 PGExitDressingActor 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> ExitDressingClass;

	// 변신 여고생(시작 지점·차에서 돌아옴·구출 인트로·콘솔 모두). 부모가 PGTransformNPCActor 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> TransformNPCClass;

	// 피날레 전함. 부모가 PGBattleshipActor 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> BattleshipClass;

	// 피날레 드래곤. 부모가 PGDragonBoss 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> DragonBossClass;

	// 코드로 짓는 시설(창고·막사·참호 등, 아직 레벨로 안 만든 것). 부모가 ProceduralFacilityActor 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> ProceduralFacilityClass;

	// 탱크. 부모가 PGTankPawn 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> TankClass;

	// 차(스폰 차량·갑판 차·변신 결과 기본). 부모가 PGVehiclePawn 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> VehicleClass;

	// 로봇(보스·탑승용). 부모가 PGRobotCharacter 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> RobotClass;

	// 바닥 아이템(연료 폭발 모양 등). 부모가 PGFloorItemActor 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> FloorItemClass;

	// 상점 부스(부스 킷 폴더). 부모가 PGBoothActor 인 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> BoothClass;

	// 날으는 차 비행 키트(부스터·빔). 부모가 PGFlightKitComponent 인 컴포넌트 블루프린트.
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<UActorComponent> FlightKitClass;

	// 소리 담당(와이즈로 실제 소리를 내는 블루프린트). 부모가 PGSoundRouter 인 블루프린트. 비면 소리 없음(신호만 지나간다).
	UPROPERTY(Config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> SoundRouterClass;

	// 여러 곳이 같이 쓰는 이펙트(미사일·잔해 먼지). 데이터 에셋. 비어 있으면 코드 기본값.
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	TSoftObjectPtr<UPGEffectSet> EffectSet;

	// 타이틀·로비·결과 화면 뒤 3D 무대에 놓는 것들(데이터 에셋). 비어 있으면 코드 기본값.
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	TSoftObjectPtr<UPGFlowStageSet> FlowStageSet;

	// 타이틀 인트로(구출·매복·전함과 드래곤)에 나오는 것들(데이터 에셋). 비어 있으면 코드 기본값.
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	TSoftObjectPtr<UPGTitleIntroSet> TitleIntroSet;

	// 무기 표(데이터 테이블, 행 = FPGWeaponDef: 총 모양·손에 드는 자리·성능). 비어 있으면 코드 기본 표(원래와 같다).
	UPROPERTY(Config, EditAnywhere, Category = "Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/ProjectPG.PGWeaponDef"))
	TSoftObjectPtr<UDataTable> WeaponTable;

	// 착장 표(데이터 테이블, 행 = FPGWearableColor: 옷·모자·가발·배낭 색마다 메시·재질·붙는 자리). 비어 있으면 코드 기본 표.
	UPROPERTY(Config, EditAnywhere, Category = "Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/ProjectPG.PGWearableColor"))
	TSoftObjectPtr<UDataTable> WearableTable;

	// 바닥 연료통 색 변형(아이템은 "Fuel" 하나, 모양만 돌려 쓴다). 첫 번째가 기본. 기본값 = 빨강·초록·노랑.
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	TArray<TSoftObjectPtr<UStaticMesh>> FuelCanMeshes;

	// 맵을 지을 때 게임 중에 읽는 것들(무너짐 먼지·풀밭 장식·호숫가 배·타일 풀/바위 등, 데이터 에셋). 비어 있으면 코드 기본값.
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	TSoftObjectPtr<UPGMapVisualSet> MapVisualSet;

	// 몬스터 모양표(데이터 에셋). 비어 있으면 코드 기본 표(원래와 같다).
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	TSoftObjectPtr<UPGMonsterLookSet> MonsterLookSet;

	// 편의 함수: 클래스별로 설정의 블루프린트를 고른다(없으면 C++ 클래스).
	static UClass* TankSpawnClass();
	static UClass* VehicleSpawnClass();
	static UClass* RobotSpawnClass();
};
