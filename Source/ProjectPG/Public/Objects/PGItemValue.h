// 아이템 등급·가치 표. "이 아이템은 무슨 등급이고, 재화(Money) 몇 개짜리이고, 맵 바닥에 얼마나 자주 떨어지나"를 말한다.
//
// 기획서 v0.4 14쪽(상점 UI): 구매가 = 기본가치 x1.1~1.5, 판매가 = 기본가치 x0.6~0.8,
// 거래는 "판 것의 가치 > 산 것의 가치"일 때만 성립하고 차액을 재화로 돌려준다.
// 그런데 기획서에 곱할 기준값인 "기본가치" 자체가 없다. 그 빠진 값을 채우는 표가 이 파일이다.
//
// [9/22 개편] 표 자체를 데이터로 뺐다. 사용자: "그냥 데이터 테이블로 처리해. 테이블 자체를 수정하면 되잖아."
//  - 원본은 Docs/DT_PGItemValue.csv 다. 등급·값·이름·바닥 루팅 비중·바닥 메시를 여기서 고치면 코드를 안 고쳐도 된다.
//  - 읽는 순서: ① 설정에 DataTable 애셋이 지정돼 있으면 그것(패키징 빌드용) ② CSV 파일 ③ 둘 다 실패하면 cpp 의 예비 표.
//    ③으로 떨어지면 경고를 남긴다 — 예비 표로 조용히 돌면 CSV 를 고쳐도 반영이 안 되는 이유를 아무도 모른다.
//  - Tools/check_item_tables.py 가 CSV·예비 표·루팅 테이블·카탈로그가 서로 맞는지 빌드 없이 검사한다.
//
// 등급은 세 칸이다: 노말 / 레어 / 에픽. (사용자 9/22: "너무 잘게 나누지 말자. 노말 레어 에픽 정도면 되겠지.")
//  - 무기는 같은 총을 세 등급으로 나눈다. 성능은 같고 이름·색·값만 다르다. ItemId 는 옷 색 변형과 같은 방식으로
//    원래 ID 가 노말(Pistol), 뒤에 붙은 것이 변형(Pistol_Rare, Pistol_Epic)이다. BaseItemId 열이 원래 총을 가리킨다.
//  - 무기가 아닌 아이템은 값 구간으로 등급을 매긴다(GetGradeRange). 스모크와 검사 스크립트가 어긋남을 잡는다.
//
// 기준점은 재화다: Money 한 개 = 1. 기획서 21쪽이 재화를 아이템 중 하나로 취급하므로 재화 자신이 표의 한 행이다.
//
// 쓰는 쪽:
//  - 상점: GetBuyPrice / GetSellPrice / EvaluateTrade. 시드가 같으면 같은 답이 나오는 순수 함수라 서버가 다시 계산해 검증한다.
//  - 맵 바닥 루팅(UPGWorldLootSpawner): LootWeight 가 0 보다 큰 행만 후보가 된다.
//  - 바닥 아이템(APGFloorItemActor): 등급 색 테두리(GetGradeOverlayMaterial), 이름(GetDisplayName).
//  - UI: GetItemValue / GetItemGrade 로 칸에 값·등급을 그린다. UI 는 읽기만 한다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DeveloperSettings.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Objects/PGObjectTypes.h"
#include "PGItemValue.generated.h"

class UMaterialInterface;
class UStaticMesh;

// 아이템 등급. 값과 따로 적어 두고 스모크가 "값이 자기 등급 구간 안에 있나"를 검사한다(무기 제외 — 무기는 같은 총끼리 노말<레어<에픽).
// 왜 값에서 자동으로 계산하지 않았나: 자동이면 "좋아 보이는 것이 실제로 비싸다"가 그냥 참이 되어 검사할 것이 없다.
//
// 주의: 오브젝트 카탈로그의 Tier(0~3)는 "어느 등급 자리에 스폰되는 상자인가"이지 아이템 등급이 아니다. 서로 다른 축이다.
UENUM(BlueprintType)
enum class EPGItemGrade : uint8
{
	Normal UMETA(DisplayName = "노말"),
	Rare   UMETA(DisplayName = "레어"),
	Epic   UMETA(DisplayName = "에픽")
};

// 아이템 종류. 등급 규칙이 종류마다 달라서 둔다(무기만 같은 총을 세 등급으로 나눈다).
UENUM(BlueprintType)
enum class EPGItemCategory : uint8
{
	Money,
	Ammo,
	Material,
	Food,
	Medical,
	Throwable,
	Wear,
	Tool,
	Key,
	Weapon,
	Quest
};

USTRUCT(BlueprintType)
struct PROJECTPG_API FPGItemValueRow : public FTableRowBase
{
	GENERATED_BODY()

	// 루팅 테이블·카탈로그가 쓰는 것과 같은 이름(Rifle_AK, Bandage ...). 옷 색 변형(Pants_Black)은 여기 없다 — 원래색 행을 물려받는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue")
	FName ItemId;

	// 등급 변형의 원래 아이템. Pistol_Rare → Pistol. 변형이 아니면 자기 자신.
	// 무기 성능·바닥 메시·탄약은 원래 아이템 것을 그대로 쓴다 — 등급은 이름·색·값만 바꾼다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue")
	FName BaseItemId;

	// 화면에 보일 이름("권총 (레어)"). 비어 있으면 ItemId 를 그대로 보인다.
	// FText 가 아니라 FString 인 이유: CSV 에 한글을 그냥 적어도 가져오기가 되게 하려고(FText 는 따옴표·NSLOCTEXT 형식을 따진다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue")
	EPGItemCategory Category = EPGItemCategory::Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue")
	EPGItemGrade Grade = EPGItemGrade::Normal;

	// 한 개당 기본가치. 재화(Money) 한 개가 1이다. 탄약은 한 발당 값이라 30발 묶음은 x30 이 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue", meta = (ClampMin = "0"))
	int32 BaseValue = 0;

	// 상점에 내놓을 수 있나. 퀘스트 아이템처럼 false 인 것은 값이 0이고 등급 구간 검사에서도 빠진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue")
	bool bTradable = true;

	// ---- 맵 바닥 루팅(UPGWorldLootSpawner). 상자 안 루팅 테이블과는 별개다. ----

	// 같은 등급 안에서의 비중. 0 이면 맵 바닥에 안 떨어진다(금고·보스 전용, 메시 없는 것).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue|WorldLoot", meta = (ClampMin = "0.0"))
	float LootWeight = 0.0f;

	// 한 자리에 놓이는 개수 범위.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue|WorldLoot", meta = (ClampMin = "1"))
	int32 LootMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue|WorldLoot", meta = (ClampMin = "1"))
	int32 LootMax = 1;

	// 한 판 상한. -1 = 없음, -2 = 설정의 MaxFuel(연료통).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue|WorldLoot")
	int32 LootCap = -1;

	// 자기 바닥 메시가 없어 남의 카탈로그 행 메시를 빌리는 것(권총탄 → OBJ-033 소총탄 더미).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue|WorldLoot")
	FName LootMeshObjectId;

	// 카탈로그 행 없이 바닥에 떨어질 때(버리기·등급 무기) 쓰는 메시. 변형은 비워 두면 원래 아이템 것을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue|WorldLoot")
	TSoftObjectPtr<UStaticMesh> FloorMesh;

	// 총이면 같이 떨어뜨릴 탄. 총만 주우면 쏠 수가 없다(9/22 "총 옆에 총알 같이 스폰되게").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue|WorldLoot")
	FName AmmoItemId;

	// 왜 이 값·이 등급인가. PG.ItemValues 가 찍는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|ItemValue")
	FString Reason;
};

// 프로젝트 설정 > Game > ProjectPG Item Grades.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ProjectPG Item Grades"))
class PROJECTPG_API UPGItemGradeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 표 DataTable 애셋(행 타입 FPGItemValueRow). 지정하면 CSV 보다 먼저 쓴다.
	// 왜 따로 두나: Docs 폴더의 CSV 는 에디터·PIE 에서만 읽힌다. 패키징하면 Docs 가 안 따라가므로 그때는 CSV 를 애셋으로 가져와 여기 넣는다.
	UPROPERTY(Config, EditAnywhere, Category = "Table", meta = (RowType = "/Script/ProjectPG.PGItemValueRow"))
	// 9/28: 기본값을 에셋(DT_PGItemValue)으로 — 패키징 빌드에서도 같은 표를 쓰고, 값은 에디터에서 고친다.
	TSoftObjectPtr<UDataTable> ItemValueTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/PG/Blueprint/Visual/DT_PGItemValue.DT_PGItemValue")));

	// 프로젝트 폴더 기준 CSV 경로. 명령줄 -PGItemTableCsv=<경로> 가 있으면 그게 우선이다.
	UPROPERTY(Config, EditAnywhere, Category = "Table")
	FString ItemValueCsv = TEXT("Docs/DT_PGItemValue.csv");

	// 바닥 아이템에 덧그리는 등급 테두리 재질. 벡터 파라미터 GradeColor 하나를 받는다(Tools/make_grade_overlay.py 가 만든다).
	// 왜 덧그리기(오버레이)인가: 총마다 원래 재질이 다르고(실사 AK, AR70 팩, Quantum 권총) 색을 바꿀 파라미터도 제각각이다.
	// 원래 재질은 그대로 두고 위에 얇은 색 테두리만 한 겹 그리면 어떤 메시든 같은 방법으로 등급 색이 보인다.
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	TSoftObjectPtr<UMaterialInterface> GradeOverlayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Props/FloorItems/Materials/M_PGGradeOverlay.M_PGGradeOverlay")));

	// 등급 색. 노말은 테두리를 안 그린다(맨 재질 = 회색 느낌). 레어 파랑, 에픽 보라 — 비교한 게임들이 공통으로 쓰는 순서다.
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	FLinearColor RareColor = FLinearColor(0.10f, 0.45f, 1.0f, 1.0f);

	UPROPERTY(Config, EditAnywhere, Category = "Look")
	FLinearColor EpicColor = FLinearColor(0.65f, 0.15f, 1.0f, 1.0f);

	// 테두리를 이 거리(cm)까지만 그린다. 덧그리기는 한 번 더 그리는 것이라 멀리 있는 것까지 그리면 4060 에서 아깝다.
	UPROPERTY(Config, EditAnywhere, Category = "Look", meta = (ClampMin = "0.0"))
	float OverlayMaxDrawDistance = 4000.0f;
};

// 거래 판정 결과. 서버가 클라이언트에게 받은 거래를 다시 계산해 이 구조체를 만들고 bValid 만 믿는다.
USTRUCT(BlueprintType)
struct PROJECTPG_API FPGTradeResult
{
	GENERATED_BODY()

	// 기획 14쪽: "판 것의 가치 > 산 것의 가치"일 때만 거래 버튼이 활성된다. 같으면 성립하지 않는다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|ItemValue")
	bool bValid = false;

	// 상점이 쳐 주는 값의 합(판매가 x0.6~0.8 적용 뒤).
	UPROPERTY(BlueprintReadOnly, Category = "PG|ItemValue")
	int32 SoldValue = 0;

	// 상점이 받는 값의 합(구매가 x1.1~1.5 적용 뒤).
	UPROPERTY(BlueprintReadOnly, Category = "PG|ItemValue")
	int32 BoughtValue = 0;

	// 판 - 산. 양수면 그만큼 재화로 돌려준다(기획 14쪽 "거래가치: +12,345").
	UPROPERTY(BlueprintReadOnly, Category = "PG|ItemValue")
	int32 Difference = 0;

	// 거래 금지 아이템이 섞여 있었으면 그 ItemId. 이때 bValid 는 항상 false 다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|ItemValue")
	FName RejectedItemId;
};

UCLASS()
class PROJECTPG_API UPGItemValueLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// 표 전체. 처음 부를 때 DataTable 애셋 → CSV → 예비 표 순서로 한 번 읽어 둔다.
	static const TArray<FPGItemValueRow>& GetAllRows();

	// 표를 다시 읽는다(콘솔 PG.ReloadItemTable). CSV 를 고친 뒤 에디터를 안 껐다 켜도 되게.
	// 주의: 전에 FindRow 로 받아 둔 포인터는 이 뒤로 쓰면 안 된다(값으로 복사해 둔 것은 괜찮다).
	static void ReloadTable();

	// 지금 표가 어디서 왔나("DataTable:/Game/..." / "CSV:<경로>" / "Fallback"). 스모크와 로그용.
	static FString GetTableSource();

	// cpp 에 적어 둔 예비 표. CSV 를 못 읽었을 때만 쓴다. 검사 스크립트가 CSV 와 같은지 본다.
	static TArray<FPGItemValueRow> BuildFallbackRows();

	// 색 변형(Pants_Black)이면 원래색(Pants) 행을 돌려준다. 등급 변형(Pistol_Rare)은 자기 행이 있다. 없는 ItemId 면 nullptr.
	static const FPGItemValueRow* FindRow(FName ItemId);

	// 한 개당 기본가치. 모르는 아이템은 0 — 값을 0으로 쳐서 공짜로 주는 쪽이 터무니없는 값으로 거래가 성립하는 것보다 낫다.
	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static int32 GetItemValue(FName ItemId);

	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static int32 GetStackValue(const FPGItemStack& Stack);

	// 소지품 전체 가치. 기획 30쪽 상점 진열 결정의 입력이다.
	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static int32 GetTotalValue(const TArray<FPGItemStack>& Items);

	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static EPGItemGrade GetItemGrade(FName ItemId);

	// 등급 변형이면 원래 아이템(Pistol_Epic → Pistol), 아니면 그대로.
	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static FName GetBaseItemId(FName ItemId);

	// 표에 적힌 이름. 없으면 빈 글자(부르는 쪽이 다른 이름 규칙으로 넘어간다).
	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static FText GetDisplayName(FName ItemId);

	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static FText GetGradeText(EPGItemGrade Grade);

	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static FLinearColor GetGradeColor(EPGItemGrade Grade);

	// 그 등급의 테두리 재질(GradeColor 를 넣은 것). 노말이거나 재질 애셋이 없으면 null.
	// 등급마다 하나만 만들어 모든 바닥 아이템이 같이 쓴다 — 아이템마다 만들면 수백 개가 된다.
	static UMaterialInterface* GetGradeOverlayMaterial(EPGItemGrade Grade);

	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static bool IsTradable(FName ItemId);

	// 상점에서 살 때 내는 값 (기본가치 x1.1~1.5). 배율은 상점 시드 + ItemId 로 고정되므로
	// 한 상점 안에서 값이 흔들리지 않고, 서버가 같은 시드로 다시 계산해 검증할 수 있다.
	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static int32 GetBuyPrice(FName ItemId, int64 ShopSeed);

	// 상점에 팔 때 받는 값 (기본가치 x0.6~0.8). 구매가와 다른 소금을 써서 둘이 같이 움직이지 않게 한다.
	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static int32 GetSellPrice(FName ItemId, int64 ShopSeed);

	// 거래 판정. 서버 검증용 순수 함수 — 월드도 액터도 안 본다.
	UFUNCTION(BlueprintPure, Category = "PG|ItemValue")
	static FPGTradeResult EvaluateTrade(const TArray<FPGItemStack>& Sold, const TArray<FPGItemStack>& Bought, int64 ShopSeed);

	// 무기가 아닌 아이템의 등급 구간. 에픽의 위쪽은 없으므로 OutMax 에 MAX_int32 가 온다.
	static void GetGradeRange(EPGItemGrade Grade, int32& OutMin, int32& OutMax);

	// DataTable 가져오기용 CSV 로 지금 표를 쓴다. 예비 표에서 CSV 를 다시 만들 때 쓴다.
	static bool ExportCsv(const FString& Path);
};
