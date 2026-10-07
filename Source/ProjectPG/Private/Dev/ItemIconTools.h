// 아이템 아이콘 만들기 도구(에디터 전용). 게임 안에서는 쓰지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ItemIconTools.generated.h"

class UStaticMesh;
class UTexture2D;

// 아이템 아이콘 도구.
// 왜 있나: 아이템 표 50줄의 아이콘을 손으로 찍는 대신, 아이템 바닥 메시(WorldMesh)를 찍어 텍스처 에셋으로 저장한다.
// 쓰는 곳: Tools/wbp/make_item_icons.py 가 표의 줄마다 부르고, 만든 텍스처를 표의 Icon 칸에 넣는다.
// 그림을 그려야 해서 에디터를 화면 밖으로 띄워 돌린다(명령줄 모드에서는 새까맣게 나온다 — 스크립트 맨 위 주석).
UCLASS()
class UItemIconTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Mesh 를 찍어 PackagePath(예: /Game/PG/UI/ItemIcons/T_Icon_1001) 에 텍스처로 저장한다.
	// 그림 크기 = 아이템 칸 수 × PixelsPerCell (예: 4×2 칸 → 256×128). 칸 비율과 같게 만들어 칸에 꽉 차게 보인다.
	// 물건은 가운데에, 둘레에 Padding(그림 짧은 변의 비율) 만큼 여백을 두고 비율을 지켜 들어간다.
	// 바탕은 처음부터 투명: 빈 미리보기 장면에서 물건만 찍고, 물건이 덮은 자리만 남긴다.
	//   (10/4~10/6 썸네일 방식은 회색 바탕을 색으로 지워서, 어두운 가방은 가방까지 지워졌다.)
	// 보는 방향: 물건의 가장 얇은 축 방향에서 본다 → 총은 옆모습, 옷은 위에서 본 모습.
	//   (위에서만 찍던 10/6 방식은 세워 만든 야구방망이가 동그라미로, AK 는 세로로 찍혔다.)
	// bFlipX / bFlipY: 찍은 그림을 좌우·위아래로 뒤집는다(총 손잡이가 위로 가는 등 방향만 틀릴 때, 스크립트의 아이템별 설정).
	// ViewAxis: 0 = 자동(가장 얇은 축), 1/2/3 = 메시의 X/Y/Z 축 + 쪽에서 본다, -1/-2/-3 = - 쪽에서(반대편에서) 본다.
	UFUNCTION(BlueprintCallable, Category = "PG|Editor")
	static UTexture2D* RenderItemIcon(UStaticMesh* Mesh, const FString& PackagePath, FIntPoint GridSize,
		int32 PixelsPerCell = 64, float Padding = 0.08f, bool bFlipX = false, bool bFlipY = false, int32 ViewAxis = 0);
};
