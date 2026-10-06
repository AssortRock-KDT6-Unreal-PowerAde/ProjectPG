// 아이템 아이콘 만들기 도구(에디터 전용). 게임 안에서는 쓰지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ItemIconTools.generated.h"

class UStaticMesh;
class UTexture2D;

// 아이템 아이콘 도구.
// 왜 있나: 아이템 표 50줄의 아이콘이 전부 엔진 기본 그림(초록 얼굴)이었다. 손으로 50장을 찍는 대신,
//          아이템 바닥 메시(WorldMesh)를 에디터 썸네일 그리기로 찍어 텍스처 에셋으로 저장한다.
// 쓰는 곳: Tools/wbp/make_item_icons.py 가 표의 줄마다 부르고, 만든 텍스처를 표의 Icon 칸에 넣는다.
// 에디터 명령줄(-run=pythonscript)에서 그림을 그리려면 -AllowCommandletRendering 이 필요하다.
UCLASS()
class UItemIconTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Mesh 를 Size×Size 로 찍어 PackagePath(예: /Game/PG/UI/ItemIcons/T_Icon_1001) 에 텍스처로 저장한다.
	// 배경(썸네일 바탕색)은 투명으로 바꾼다 — 인벤토리 칸 색이 비쳐 보이게.
	// OrbitPitch: 카메라 내려다보는 각도(-90 = 바로 위). 바닥 아이템 메시는 바닥에 눕혀 만든 것이라 비스듬히 찍으면
	//             구겨진 덩어리로 보였다(10/6 방탄조끼) → 바로 위에서 찍으면 모양이 알아보기 쉽다.
	UFUNCTION(BlueprintCallable, Category = "PG|Editor")
	static UTexture2D* RenderMeshIcon(UStaticMesh* Mesh, const FString& PackagePath, int32 Size = 128, float OrbitPitch = -89.0f, float OrbitYaw = 0.0f);
};
