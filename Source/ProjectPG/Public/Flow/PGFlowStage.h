// 흐름 화면(타이틀·로비·결과) 뒤에 까는 작은 3D 무대. (2026-09-22)
//
// 왜 코드로 짓나: L_Title / L_Lobby / L_Scoreboard 는 빈 레벨이다. 에디터에서 하나하나 배치하면 세 레벨을 따로 손봐야 하고,
//   팀 저장소로 옮길 때 .umap 머지 충돌이 난다. 그래서 게임모드가 시작할 때 풀밭·나무·바위·차·캐릭터·빛·카메라를 스폰한다.
//   레벨에 이미 빛(해·하늘빛·대기·안개)이 있으면 그것은 건드리지 않고 없는 것만 채운다.
//
// 화면마다 다른 것은 "카메라 자리"와 "분위기"뿐이다:
//   - 로비: 해 질 녘 풀밭, 왼쪽에 세워 둔 험비, 가운데 캐릭터 전신.
//   - 타이틀: 같은 풀밭인데 더 어둡고 붉다. 캐릭터 뒤에서 차 잔해가 불타고 연기가 오른다. 카메라가 천천히 흘러간다.
//     Content/Movies/PG_TitleLoop.mp4 가 있으면 3D 대신 그 영상을 깐다(영상 재생은 타이틀 위젯이 한다).
//   - 결과: 로비와 같은 무대를 뒤쪽이 흐려지게(피사계 심도) + 채도를 빼서 어둡게 본다.
//
// 에셋은 쓰기 전에 디스크에 있는지 확인하고, 없으면 경고만 남기고 그 조각을 빼고 짓는다(팩이 없는 PC 에서도 화면은 뜬다).
#pragma once

#include "CoreMinimal.h"
#include "Flow/PGRunSubsystem.h" // EPGFlowScreen

class ACameraActor;
class UPointLightComponent;
class UWorld;

// 지은 무대에서 매 프레임 움직일 것들. 액터는 레벨이 들고 있으니 여기서는 약한 포인터만 쥔다.
struct PROJECTPG_API FPGFlowStageHandles
{
	TWeakObjectPtr<ACameraActor> Camera;
	// 타이틀의 불빛. 불꽃처럼 밝기를 흔든다.
	TWeakObjectPtr<UPointLightComponent> FireLight;
	float FireBaseIntensity = 0.0f;
	// 카메라가 흘러가도 늘 이 점(캐릭터 가슴께)을 본다.
	FVector CameraBase = FVector::ZeroVector;
	FVector LookAt = FVector::ZeroVector;
};

namespace PGFlowStage
{
	// 무대를 짓고 카메라를 돌려준다. 실패한 조각은 건너뛰므로 카메라는 늘 생긴다(월드가 없을 때만 빈 값).
	PROJECTPG_API FPGFlowStageHandles Build(UWorld* World, EPGFlowScreen Screen);

	// 매 틱: 타이틀 카메라를 천천히 흘리고, 불빛을 깜빡인다. 로비는 숨 쉬듯 아주 조금만 움직인다.
	PROJECTPG_API void Animate(FPGFlowStageHandles& Stage, EPGFlowScreen Screen, float Seconds);

	// 타이틀 배경 영상(Content/Movies/PG_TitleLoop.mp4). 있으면 true 와 절대 경로.
	PROJECTPG_API bool FindTitleVideo(FString& OutFullPath);
}
