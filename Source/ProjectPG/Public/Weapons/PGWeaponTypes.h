// 무기 데이터.
//
// 총·활·근접무기는 "선을 긋고 처음 맞은 것에 피해를 준다"는 점이 같다(팀 결정: 투사체·객체 풀 안 씀).
// 다른 건 사거리·피해·간격·탄약뿐이라 무기마다 클래스를 만들지 않고 행 하나로 갈랐다.
// 오브젝트 카탈로그(FPGObjectCatalogRow)와 같은 원칙이다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PGWeaponTypes.generated.h"

class UStaticMesh;

UENUM(BlueprintType)
enum class EPGWeaponKind : uint8
{
	Melee UMETA(DisplayName = "근접 (짧은 트레이스)"),
	Bow   UMETA(DisplayName = "활·투척 (긴 트레이스, 탄약 선택)"),
	Gun   UMETA(DisplayName = "총 (긴 트레이스, 탄약 필수)")
};

USTRUCT(BlueprintType)
struct FPGWeaponDef : public FTableRowBase
{
	GENERATED_BODY()

	// 인벤토리 아이템 ID. 오브젝트 카탈로그의 ItemId와 같은 이름을 쓴다(Rifle_AR70, Axe, Bat ...).
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EPGWeaponKind Kind = EPGWeaponKind::Melee;

	// 손에 들었을 때 보이는 메시. 비어 있으면 안 보이지만 공격은 된다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UStaticMesh> Mesh;

	// 트레이스 길이(cm). 근접은 팔 길이, 총은 사실상 무제한.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "10.0"))
	float Range = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Damage = 30.0f;

	// 연속 공격 최소 간격(초). 서버가 검사하므로 클라이언트가 빨리 눌러도 소용없다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.05"))
	float AttackInterval = 0.5f;

	// 트레이스 굵기. 근접은 굵게(빗나감 방지), 총은 가늘게.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float TraceRadius = 10.0f;

	// 한 발에 소모할 아이템. None이면 탄약 없이 쏜다. 자기 자신을 넣으면 투척 무기가 된다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName AmmoItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 AmmoPerShot = 1;

	// 한 번 쏠 때 나가는 탄 수. 1 이면 보통 총, 샷건은 여러 발(산탄). Damage 는 탄 한 발당.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 PelletCount = 1;

	// 탄이 퍼지는 원뿔 반각(도). 0 이면 조준점 그대로. 샷건은 가까울수록 많이 맞고 멀면 흩어진다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float SpreadDegrees = 0.0f;

	// 캐릭터 스켈레탈 메시의 소켓(또는 본) 이름. 없으면 몸 앞에 그냥 붙인다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName AttachSocket = TEXT("hand_r");

	// 3인칭 손 위치 보정·방향. 무기 컴포넌트가 몸 기준(bHoldInBodySpace)이면 둘 다 "캐릭터 정면 기준"이다:
	//   회전 0 = 총구가 캐릭터 정면(+X), 손잡이 아래. 위치 = 손에서 메시 피벗까지(앞 +X, 오른쪽 +Y, 위 +Z, cm).
	//   피벗이 메시 가운데인 총은 손잡이가 뒤·아래에 있으므로 위치를 앞·위로 준다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector AttachLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FRotator AttachRotation = FRotator::ZeroRotator;

	// 1인칭(카메라 앞)에 붙일 때의 위치·회전. 메시마다 기준축이 달라 무기별로 둔다.
	// 값은 PIE에서 PG.WeaponTune 으로 맞춘 뒤 RegisterDefaultWeapons 에 옮겨 적는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector FirstPersonLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FRotator FirstPersonRotation = FRotator::ZeroRotator;

	// 메시 크기 보정. 팩마다 단위가 달라 실제보다 크게 들어오는 것이 있다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.01"))
	float Scale = 1.0f;
};
