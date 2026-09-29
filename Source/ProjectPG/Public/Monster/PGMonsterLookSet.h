// 몬스터 모양표(데이터 에셋). 종류 이름(Slime, Cactus, Beholder, Rampage, ChestMonster) → 메시·애니·몸 크기 한 벌. (2026-09-23 블루프린트 분리)
//
// 왜 데이터 에셋인가: 원래 APGMonsterCharacter::GetPresetVisuals 안에 경로가 글자로 적힌 표였다. 몬스터 모델을 하나 바꾸려 해도
//   코드를 고쳐 빌드해야 했다. 몬스터는 종류가 "클래스" 가 아니라 "이름" 으로 나뉘어서(전투 배치가 이름을 고른다) 블루프린트 자식 대신
//   이름 → 모양 표 하나를 에디터에서 여는 데이터 에셋(DA_PGMonsterLooks)으로 뺐다.
// 어느 에셋을 쓰나: 설정(ProjectPG Visuals > Monster Look Set). 비어 있으면 이 클래스의 기본값(원래 코드 표와 같다)을 쓴다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Monster/PGMonsterCharacter.h"
#include "PGMonsterLookSet.generated.h"

UCLASS(BlueprintType)
class PROJECTPG_API UPGMonsterLookSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPGMonsterLookSet();

	// 종류 이름 → 모양. 이름은 전투 설정(세력별 프리셋 목록)과 콘솔 PG.SpawnMonster 가 쓰는 것과 같아야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	TMap<FName, FPGMonsterVisuals> Looks;

	// 지금 쓰는 모양표: 설정에 에셋이 있으면 그것, 없으면 기본값(클래스 기본 객체). 게임 중에만 부른다(에셋을 읽는다).
	static const UPGMonsterLookSet* GetActive();

	// 원래 코드 표(9/23 전 GetPresetVisuals 내용 그대로). 기본값을 채우는 데 쓰고, 생성자처럼 에셋을 읽으면 안 되는 곳도 쓴다.
	static bool BuildDefaultLook(FName Preset, FPGMonsterVisuals& Out);

	// 모양표의 메시·애니 전부를 뒤에서(비동기) 읽어 판이 끝날 때까지 쥐고 있는다. 판 시작(로딩 화면) 때 모든 컴퓨터에서 한 번 부른다.
	// 왜(9/28 4060 측정): 몬스터가 처음 보이는 순간 메시를 그 자리에서 읽어(LoadSynchronous) 화면이 멈췄다 — 로봇으로 중앙 건물을
	//   부수던 접속자 화면이 4.2초 멈춤(Rampage). 몬스터 코드(형님 담당)는 그대로 두고, 미리 읽어 두면 그 자리 읽기가 바로 끝난다.
	static void PreloadAll();
};
