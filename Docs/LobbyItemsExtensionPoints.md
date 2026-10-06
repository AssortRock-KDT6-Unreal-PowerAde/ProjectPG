# 로비·아이템·맵 — 팀원이 바꿔 끼우는 곳 (2026-10-06)

코드 없이(빌드 없이) 바꿀 수 있는 곳만 모았다. "C++ 은 동작, 보이는 것·숫자·글자는 BP·WBP·표·ini" 가 원칙.

## 캐릭터
| 바꾸고 싶은 것 | 고칠 곳 |
|---|---|
| 게임·로비 캐릭터 | `/Game/PG/Blueprint/GameMode/BP_GameMode` 의 Default Pawn Class 하나만. 로비(타이틀) 캐릭터도 이걸 따라간다 |
| 로비 캐릭터를 다른 게임모드 기준으로 | `DefaultGame.ini` `[/Script/ProjectPG.LobbyUIFlowController]` `CharacterSourceGameMode=` |
| 로비 캐릭터 서는 자리 | `L_Title` 의 `Title_CharacterSpot`(태그 `LobbyCharacterSpot`) 옮기기 |
| 로비 카메라(메뉴·캐릭터 화면) | `L_Title` 의 `Title_Camera`(태그 `LobbyCamera_Menu`)·`Title_CharacterCamera`(`LobbyCamera_Character`) |
| 카메라 옮겨 가는 시간·카메라 이름 | `WBP_Lobby`(캐릭터 버튼)·`WBP_CharacterWidget`(뒤로가기)의 Lobby Camera 칸 |

## 아이템
| 바꾸고 싶은 것 | 고칠 곳 |
|---|---|
| 아이템 이름·크기·장비 칸·바닥 메시·아이콘 | `/Game/PG/Table/ItemTable` |
| 아이콘 다시 찍기 | `Tools/wbp/make_item_icons.py` (에디터를 화면 밖으로 띄워 실행 — 파일 맨 위 주석) |
| 아이콘을 그림으로 바꾸기 | `Tools/icons_custom/T_Icon_<번호>.png` 를 넣고 위 스크립트 다시 실행(그림이 먼저 쓰인다) |
| 처음 갖고 시작하는 짐 | `/Game/PG/Table/StarterInventoryTable` (Container: Stash·Pocket·Equip) |
| 창고·주머니 크기 | `DefaultGame.ini` `[/Script/ProjectPG.InventorySubSystem]` `StashSize`·`PocketSize` |
| 상자 자리에 나오는 아이템·확률 | `/Game/PG/LevelDesign/Data/DT_LootSpawn` (무게·최소 등급·개수) |
| 시설 안 상자·몬스터 자리 | `/Game/PG/LevelDesign/Data/DT_FacilityPoints` |
| 바닥 아이템 모습(대신 모양·최대 크기) | `/Game/PG/Blueprint/Item/BP_WorldItem` |

## 화면(글자·숫자도 WBP 칸)
| 화면 | WBP | 글자·숫자 칸 |
|---|---|---|
| 로비 메뉴 | `WBP_Lobby` | 버튼 모양 |
| 캐릭터·인벤토리 | `WBP_CharacterWidget` | Filter(종류 목록 글자), Lobby Camera |
| 격자 | `WBP_InventoryGrid` | Filtered Out Opacity(검색에 안 맞는 아이템 흐림) |
| 아이템 칸 | `WBP_ItemWidget` | Tooltip Delay Seconds(설명 창 1초) |
| 설명 창 | `WBP_ItemTooltip` | Type Names·Slot Names·설명 문장·Mouse Offset |
| 매칭 | `WBP_Matching` | State Texts(상태별 글자)·Close Delay Seconds |
| 옵션 | `WBP_Option` | 화면 모드·품질 글자, Frame Limits |

## 매칭(리슨 서버)
`DefaultGame.ini` `[/Script/ProjectPG.SessionSubSystem]` — `GameMapPath`(게임 맵), `MaxPlayers`, `SearchSeconds`(방 찾는 시간).

## 맵
`/Game/PG/LevelDesign/DA_MapAssets`(타일 BP·시설 레벨·머티리얼·산·풀·위 표들), 맵 액터의 Design Numbers(마당 수·언덕 수·길찾기 반경).

## 확인 방법
- 같은 시드 비교: `-PGMapSeed=12345` → 로그 `layout_hash=035300A1`, `point_hash=34F20671`, `final_point_hash=04F47281`, `item_hash=4D8A5D0B`.
- 화면 없이 UI 돌려 보기: `-PGClick=Character@10+Tooltip@14 -PGShot=12+15 -PGQuitAt=17` (개발 빌드만, `UUiAutoTestSubSystem`).
- 블루프린트 전부 다시 컴파일: `Tools/wbp/check_bps.py`.
