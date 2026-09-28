// 팀 아이템 표(ItemTable)에 없는 우리 아이템(연료통·옷 색 변형·무기·탄약·약품 등)의 "대체 행". (2026-09-20)
//
// 왜: 팀 인벤토리(UInventoryComponent)는 아이템을 넣을 때 UItemSubSystem::GetItem 으로 표에서 칸 크기(GridSize)를 찾는데,
//     팀 표에는 1001·1003 두 줄뿐이라 연료통을 주워도 "표에 없음"으로 실패했다(PG.GiveItem given=false 의 원인 중 하나).
//     옷은 색마다 ItemId 가 따로라(Pants_Black…) 표에 손으로 넣으면 수십 줄이 되고, 색이 늘 때마다 표도 고쳐야 한다.
//     그래서 표에 없을 때만 오브젝트 쪽 정보(착장 표·이름 규칙)로 행을 만들어 준다. 팀 표에 같은 ID 가 생기면 그쪽이 우선이다.
// 팀 코드 변경은 UItemSubSystem::GetItem 의 "못 찾음" 자리 한 줄뿐이다.
#pragma once

#include "CoreMinimal.h"

struct FItemTableRow;

namespace PGItemRowFallback
{
	// 숫자만으로 된 ID(팀 서버 아이템 1001 등)는 만들지 않는다 — 그건 팀 표가 정답이어야 한다. 그 외에는 항상 행을 돌려준다.
	// 돌려주는 포인터는 게임이 끝날 때까지 유효하다(한 번 만든 행은 캐시에 계속 둔다).
	PROJECTPG_API const FItemTableRow* Find(FName ItemID);
}
