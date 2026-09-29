// 입는 장비(착장) 표. ItemId 하나가 "어느 슬롯에 / 어떤 메시를 / 어떤 색 머티리얼로" 입히는지 말해 준다.
//
// 캐릭터는 팀 플레이어 캐릭터와 같은 Fab "Quantum Modular Character Free Sample" 기준이다.
// 이 팩은 슬롯마다 메시가 한 종류뿐이라 "다른 옷" = "같은 메시의 다른 색"이다. 색 머티리얼은 Tools/make_quantum_wearables.py 가 만든다.
//
// 색은 ItemId 로 구분한다: Pants(팩 원래색) / Pants_Black / Pants_Khaki ...
// 왜 ItemId 에 색을 넣었나: 오브젝트↔캐릭터 약속인 IPGItemReceiver::ReceiveItem(ItemId, Count) 를 안 바꿔도 되고,
// 인벤토리에서도 색이 다른 옷은 다른 아이템으로 따로 쌓인다.
//
// 쓰는 쪽:
//  - 바닥 아이템(APGFloorItemActor): SetItem 에서 FloorMesh + Material 을 입는다.
//  - 캐릭터: UPGWearableComponent::Equip(ItemId) 가 이 표를 보고 파츠 메시를 붙이고 머티리얼을 바꾼다.
//  - 스폰·루팅: PickColorVariant 로 색을 고른다(시드 고정).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PGWearableColors.generated.h"

class UDataTable;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;

// 몸의 어느 자리에 입는가. 한 자리에 하나만 입는다(새로 입으면 입고 있던 것은 벗겨진다).
UENUM(BlueprintType)
enum class EPGWearSlot : uint8
{
	None,
	Top,      // 셔츠. 팩에 맨몸 상체가 없어서 항상 무언가 입고 있어야 한다
	Bottom,   // 청바지. 위와 같은 이유로 항상 입는다
	Vest,     // 방탄조끼
	Cap,      // 모자 (스태틱 메시를 머리뼈에 붙임)
	Pouch,    // 가슴 파우치
	Holster,  // 허벅지 권총집
	Backpack, // 팩에 없음. 구운 배낭 스태틱 메시를 등에 붙임
	Shoes,    // 팩에 신발 모듈이 없어 겉모습 없음(아이템으로만 존재)
};

// 9/23 블루프린트 분리: 데이터 테이블(DT_PGWearables)의 행으로도 쓴다. 행 이름 = ItemId.
USTRUCT(BlueprintType)
struct PROJECTPG_API FPGWearableColor : public FTableRowBase
{
	GENERATED_BODY()

	// 이 색의 ItemId (예: Pants_Black)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	FName ItemId;

	// 같은 옷의 원래색 ItemId (Pants / Shirt / Armor_Vest ...). 루팅 테이블·카탈로그는 이 이름을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	FName BaseItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	EPGWearSlot Slot = EPGWearSlot::None;

	// UI 에 보일 이름 ("검정 청바지")
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	FText DisplayName;

	// 입었을 때와 바닥에 있을 때 같이 쓰는 0번 머티리얼
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	TSoftObjectPtr<UMaterialInterface> Material;

	// 바닥에 놓였을 때 메시
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	TSoftObjectPtr<UStaticMesh> FloorMesh;

	// 입었을 때: 스켈레탈이면 몸 메시를 따라 움직이고(LeaderPose), 스태틱이면 AttachBone 에 붙는다. 둘 다 없으면 겉모습 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	TSoftObjectPtr<USkeletalMesh> WornSkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	TSoftObjectPtr<UStaticMesh> WornStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	FName AttachBone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	FTransform AttachOffset;

	// true 면 AttachOffset 을 뼈 축이 아니라 "몸 메시 공간"(Z 위, -Y 등 뒤, +X 캐릭터 오른쪽)으로 적는다. 붙일 때 레퍼런스 포즈의 뼈 위치로 바꿔 넣는다.
	// 왜: 이 스켈레톤(MetaHuman 식)은 척추뼈의 X 축이 척추를 따라 위를 향한다. 뼈 축으로 "뒤로 20cm"를 적었더니 실제로는 아래·옆으로 가서
	//     배낭이 오른쪽 엉덩이에 붙었다. 몸 공간으로 적으면 뼈 축을 몰라도 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	bool bOffsetInBodySpace = false;
};

UCLASS()
class PROJECTPG_API UPGWearableColorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ItemId 가 입는 장비면 true 와 정보를 돌려준다.
	UFUNCTION(BlueprintPure, Category = "PG|Wearable")
	static bool FindWearableColor(FName ItemId, FPGWearableColor& OutColor);

	// 같은 옷의 색 전부(원래색 포함). 상점·UI 에서 목록이 필요할 때.
	UFUNCTION(BlueprintPure, Category = "PG|Wearable")
	static TArray<FPGWearableColor> GetColorsForBase(FName BaseItemId);

	// 원래색 ItemId(Pants 등)를 시드로 색 하나로 바꾼다. 옷이 아니거나 이미 색이 정해진 ItemId 면 그대로 돌려준다.
	// 같은 시드 = 같은 색 (맵 시드 결정성 유지).
	UFUNCTION(BlueprintPure, Category = "PG|Wearable")
	static FName PickColorVariant(FName ItemId, int64 Seed);

	// 아이템 이름을 사람이 읽는 글자로. 옷이면 착장 표의 DisplayName, 아니면 등급 표(PGItemValue)의 DisplayName, 둘 다 없으면 ItemId 그대로.
	UFUNCTION(BlueprintPure, Category = "PG|Wearable")
	static FText GetItemDisplayName(FName ItemId);

	// 바닥에 떨어졌을 때 메시. 착장이면 착장 표의 FloorMesh, 아니면 등급 표의 FloorMesh(등급 변형은 원래 아이템 것).
	// 없으면 null — 그 경우 카탈로그 행의 메시를 쓴다.
	UFUNCTION(BlueprintPure, Category = "PG|Wearable")
	static TSoftObjectPtr<UStaticMesh> FindItemFloorMesh(FName ItemId);

	// 같은 아이템의 겉모습 변형(연료통 빨강·초록·노랑). ItemId 는 하나로 두고 바닥 모양만 바꾼다 — 탈출구가 "Fuel" 하나로 요구하므로.
	// 변형이 없는 아이템이면 null. Variant 는 아무 수나 넣어도 개수로 나눈 나머지를 쓴다.
	static TSoftObjectPtr<UStaticMesh> FindItemFloorMeshVariant(FName ItemId, int32 Variant);

	// 지금 쓰는 착장 표: 설정(ProjectPG Visuals > Wearable Table)에 데이터 테이블(DT_PGWearables)이 있으면 그 행(순서대로),
	// 없으면 코드 기본 표. 행 순서가 색 뽑기(PickColorVariant) 결과를 정하므로 표에서 순서를 바꾸면 같은 시드에서 다른 색이 나온다.
	static const TArray<FPGWearableColor>& GetAllColors();

	// 코드 기본 착장 표(9/23 블루프린트 분리 전 GetAllColors 내용 그대로).
	static TArray<FPGWearableColor> BuildDefaultColors();

	// 도구용: 코드 기본 착장 표를 데이터 테이블에 행으로 적는다. DT_PGWearables 를 처음 만들 때 한 번(Tools/wbp/make_wearable_table.py). 적은 행 수.
	UFUNCTION(BlueprintCallable, Category = "PG|Wearable|Tools")
	static int32 WriteDefaultColorsToTable(UDataTable* Table);
};
