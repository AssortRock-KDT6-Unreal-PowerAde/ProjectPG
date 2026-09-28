// 서버 → 그 사람 화면 안내(2026-09-27 멀티).
//
// 왜: 화면 안내(UPGAnnounceSubsystem)는 "이 컴퓨터의 화면"에 그린다. 탈출구·피날레·드래곤처럼 서버에서 일어나는 일이
//   안내를 부르면 전용 서버에는 화면이 없어 아무에게도 안 보였다(탈출 카운트다운 5·4·3·2·1, "연료통 필요", 피날레 경고 등 — 멀티 점검 A3·B).
// 어떻게: 사람마다 플레이어 컨트롤러에 이 부품을 하나 붙이고(서버에서, 복제), 서버가 Client RPC 로 "이 줄을 띄워라" 를 그 사람에게만 보낸다.
//   부르는 쪽은 AnnounceTo / CountdownTo / AnnounceToAll 만 쓴다 — 이 컴퓨터 사람(혼자 하는 판·듣기 서버 방장)이면 바로 그린다.
// 왜 컨트롤러에 붙이나: 캐릭터는 탈것에 타면 조종이 풀려 RPC 가 그 사람에게 안 간다. 컨트롤러는 접속 내내 그 사람 것이다.
//   컨트롤러 클래스(팀 CustomPlayerController)는 고치지 않고 실행 중에 부품만 더한다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Flow/PGRunTypes.h"
#include "PGPlayerMessageComponent.generated.h"

class AController;
class APawn;
class APlayerController;

UCLASS(ClassGroup = (PG))
class PROJECTPG_API UPGPlayerMessageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGPlayerMessageComponent();

	// ---- 서버(또는 혼자 하는 판)에서 부른다 ----
	// 그 사람 화면에 안내 줄들을 띄운다.
	static void AnnounceTo(AController* Controller, const TArray<FText>& Lines);
	// 그 사람 화면 가운데 카운트다운 한 줄(빈 글자 = 지움).
	static void CountdownTo(AController* Controller, const FText& Line);
	// 접속한 모든 사람에게.
	static void AnnounceToAll(const UObject* WorldContext, const TArray<FText>& Lines);
	// 폰을 조종하는 사람의 컨트롤러. 폰이 탈것이면 그 탈것을 조종하는 사람.
	static APlayerController* ResolvePlayer(const APawn* Pawn);
	// 멀티(9/27): 이 사람의 판 결과(탈출·사망·시간 초과)를 그 사람 컴퓨터로 보낸다 — 창고·통계는 그 사람 컴퓨터에 저장되고
	//   결과 화면도 거기서 연다. 이 컴퓨터 사람이면 바로 넘긴다.
	static void SendRunResult(APlayerController* PlayerController, const FPGRunRecord& Record);
	// 판 시작 때 미리 붙여 둔다 — 막 붙인 부품은 클라이언트에 생기기 전이라 첫 RPC 가 사라질 수 있다.
	static UPGPlayerMessageComponent* Ensure(APlayerController* PlayerController);

protected:
	UFUNCTION(Client, Reliable)
	void ClientAnnounce(const TArray<FText>& Lines);
	UFUNCTION(Client, Reliable)
	void ClientCountdown(const FText& Line);
	UFUNCTION(Client, Reliable)
	void ClientRunFinished(const FPGRunRecord& Record);

	// 막 붙인 부품이면 잠깐 기다렸다 보낸다(위 Ensure 주석).
	void SendAnnounce(const TArray<FText>& Lines);
	void SendCountdown(const FText& Line);
	double CreatedAt = 0.0;
};
