// 판 밖 창고(스태시)·통계·지난 판 기록의 로컬 저장 파일.
//
// 왜 SaveGame 인가: 기획서는 창고를 "판 밖에서만 손대는 것" 으로 두고 마스터 서버가 갖는다고 했다. 지금은 서버가 없어
//   (팀 WebSocket 서버는 로그인·인벤토리 조회만 있고 판 결과 반영이 없다) 로컬 파일로 같은 역할을 흉내 낸다.
//   나중에 서버로 바꿀 자리는 UPGRunSubsystem::LoadStash / SaveStash 두 곳뿐이다 — 이 클래스는 그때 지워도 된다.
//
// 파일 위치: Saved/SaveGames/PGStash.sav (슬롯 이름은 PGRunSubsystem 의 StashSlotName).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Flow/PGRunTypes.h"
#include "PGStashSaveGame.generated.h"

UCLASS()
class PROJECTPG_API UPGStashSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 파일 형식 번호. 필드를 바꾸면 올리고, 읽을 때 옛 번호면 버리거나 옮긴다.
	UPROPERTY()
	int32 Version = 1;

	// 창고 아이템. 같은 ItemId 는 한 줄로 합쳐 둔다(AddToStash 가 합친다).
	UPROPERTY()
	TArray<FPGItemStack> Stash;

	UPROPERTY()
	FPGPlayerStats Stats;

	// 최근 판부터 앞에. 개수는 PGRunSubsystem 의 MaxRunHistory 로 자른다.
	UPROPERTY()
	TArray<FPGRunRecord> RunHistory;
};
