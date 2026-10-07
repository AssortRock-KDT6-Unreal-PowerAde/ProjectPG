# 화면(UI)·아이템 고치는 곳 안내 (2026-10-06)

원칙: **보이는 것(배치·색·글자·숫자)은 WBP·BP·표에서, 코드(C++)는 동작만.**
그래서 아래 대부분은 에디터에서 열어 고치고 저장하면 끝(빌드 필요 없음).
더 짧은 목록은 `Docs/LobbyItemsExtensionPoints.md`.

> 주의: WBP 안의 **칸 이름은 바꾸지 말 것.** 코드가 이름으로 찾아 쓴다(예: `StatusText`, `CancelButton`).
> 이름을 바꾸면 그 부분만 조용히 동작을 안 한다(오류는 안 남).

---

## 0. 학원 PC 에서 받기

1. 팀 저장소에서 이 브랜치 받기: `git fetch` → `git switch 맵-쪼개기-작업`
2. 이 브랜치에 들어 있는 것(깃허브로 같이 옴, 약 14MB): 화면 WBP, 아이템 아이콘(`Content/PG/UI/ItemIcons`, `ItemIcons_Custom`),
   글꼴 Pretendard(`Content/PG/UI/Fonts`), 아이템 바닥 모양(`Content/PG/Characters·Weapons·Props`), 타이틀 레벨, 표.
3. 깃허브에 **없는 것**(구글 드라이브): 에셋 팩(Downtown_West, Factory_Pack_V1, Modular_Rural_Cabin, GV_FreeShrubsPack, Fab, AE_BR_Props, AK-47 등).
   팩은 팀 저장소에 안 올린다(`.git/info/exclude`).
4. 빌드 후 실행하면 타이틀(`L_Title`)부터 뜬다.

---

## 1. 화면별 WBP

모든 WBP 위치: `/Game/PG/Blueprint/UI/`

### 1-1. 타이틀·메인 메뉴 (기획서 1.1)
| 고치고 싶은 것 | 어디 |
|---|---|
| 버튼 4개(캐릭터·게임 시작·옵션·종료) 모양·위치 | `WBP_Lobby` |
| 버튼 이름 칸(바꾸지 말 것) | `CharacterBtn`, `GameStartBtn`, `OptionBtn`, `ExitBtn` |
| 캐릭터 화면으로 갈 때 카메라 이름·옮겨 가는 시간 | `WBP_Lobby` 의 **Lobby Camera** 칸 |
| 배경 마을·조명·카메라 자리 | 레벨 `/Game/PG/Level/L_Title` (`Title_Camera`, `Title_CharacterCamera`, `Title_Sun` …) |
| 캐릭터 서는 자리 | `L_Title` 의 `Title_CharacterSpot`(태그 `LobbyCharacterSpot`) |
| 서 있는 캐릭터 종류 | `/Game/PG/Blueprint/GameMode/BP_GameMode` 의 **Default Pawn Class** (게임 캐릭터와 같은 것) |

### 1-2. 캐릭터·인벤토리 화면 (기획서 1.2.1)
| 고치고 싶은 것 | 어디 |
|---|---|
| 화면 전체 배치(장비·주머니·가방·창고 자리, 아래 탭, 뒤로가기) | `WBP_CharacterWidget` |
| 뒤 배경 어둡기 | `WBP_CharacterWidget` 의 `PlanBackground` 색 |
| 검색 칸·종류 고르기 위치 | `WBP_CharacterWidget` 의 `SearchBox`, `FilterCombo` |
| 종류 고르기 목록 글자(전체·무기·방어구…) | `WBP_CharacterWidget` 의 **Filter Labels** 칸 (순서: 전체 → 무기·방어구·소비·퀘스트·가방·기타) |
| 뒤로가기 때 카메라 | `WBP_CharacterWidget` 의 **Lobby Camera** 칸 |
| 장비 칸 9개 배치, 캐릭터 보이는 자리 | `WBP_Equip` (`CharacterView` = 캐릭터 자리, 지금 투명) |
| 장비 칸 하나 모양 | `WBP_EquipSlot` |
| 장비 칸 아이콘 여백 | `WBP_EquipSlot` 의 `IconScale` 슬롯 Padding(아이콘은 비율대로 맞춰 들어감) |

### 1-3. 아이템 칸
| 고치고 싶은 것 | 어디 |
|---|---|
| 칸 한 개 색·테두리 | `WBP_Slot` (`BackGround` 색, `SlotBorder` 색), 기본색 = **Default Slot Color** |
| 칸 묶음(창고·주머니·가방) | `WBP_InventoryGrid` |
| 칸 크기(픽셀) | `WBP_InventoryGrid` 의 **Tile Size** |
| 칸·아이템을 어떤 WBP 로 만들지 | `WBP_InventoryGrid` 의 **Slot Widget Class**, **Item Widget Class** |
| 검색에 안 맞는 아이템 흐림 정도 | `WBP_InventoryGrid` 의 **Filtered Out Opacity** (0 = 안 보임, 1 = 그대로) |
| 칸 위의 아이템(그림·개수 글자) | `WBP_ItemWidget` (`ItemIcon`, `TextStackCount`) |
| 아이콘 비율·여백 | 아이콘은 늘어나지 않고 칸 안에 비율대로 맞춰 들어간다(`IconCanvas` > `IconScale` > `ItemIcon`, 이름 바꾸지 말 것). 여백 = `WBP_ItemWidget` 의 **Icon Padding**, 돌린 아이템은 그림도 90° 돈다 |
| 마우스 올리고 설명 창 뜨기까지 시간 | `WBP_ItemWidget` 의 **Tooltip Delay Seconds** (기획서 1초) |

### 1-4. 아이템 설명 창 (마우스 1초)
| 고치고 싶은 것 | 어디 |
|---|---|
| 창 모양·색·글꼴 | `WBP_ItemTooltip` (`NameText`, `DescriptionText`, `StatNameText`, `StatValueText`) |
| 종류 이름(무기·방어구…), 장비 칸 이름(머리·상의…) | `WBP_ItemTooltip` 의 **Type Names**, **Slot Names** |
| 설명이 없을 때 문장 | **Equip Description Format** (`{Type} · {Slot} 칸에 장착한다.`), **Plain Description Format** |
| 마우스에서 떨어진 거리 | **Mouse Offset** |
| 아이템마다 진짜 설명 | 아이템 표 `ItemTable` 의 **Description** 칸(채우면 위 문장 대신 이걸 씀) |

### 1-5. 우클릭 메뉴
| 고치고 싶은 것 | 어디 |
|---|---|
| 버튼 모양·순서 | `WBP_ItemContextWidget` |
| 버튼 이름 칸(바꾸지 말 것) | `OpenButton`(가방 열기), `EquipButton`, `UnEquipButton`, `UseButton`, `DropButton`(버리기), `RotateButton`(돌리기), `SplitButton`(나누기), `CancleButton` |
| 어떤 아이템에 어떤 버튼이 보이나 | 코드 `ItemContextWidget.cpp` 의 `UpdateButtonState` (종류별) |

### 1-6. 매칭 화면 (기획서 1.3.1)
| 고치고 싶은 것 | 어디 |
|---|---|
| 화면 모양 | `WBP_Matching` (`StatusText`, `CancelButton`, `MatchProgress`) |
| 상태별 글자(매칭중… / 방에 들어가는 중… / 실패 …) | `WBP_Matching` 의 **State Texts** |
| 취소·실패 뒤 닫히기까지 | **Close Delay Seconds** |
| 방 찾는 시간, 최대 인원, 게임 맵 | `Config/DefaultGame.ini` `[/Script/ProjectPG.SessionSubSystem]` `SearchSeconds`, `MaxPlayers`, `GameMapPath` |

### 1-7. 옵션 화면
| 고치고 싶은 것 | 어디 |
|---|---|
| 화면 모양(지금 검정 판) | `WBP_Option` |
| 목록 글자·값 | **Window Mode Labels**, **Quality Labels**, **Frame Limits**(0 = 제한 없음), **No Frame Limit Label** |

---

## 2. 아이템 데이터

| 고치고 싶은 것 | 어디 |
|---|---|
| 이름·종류·장비 칸·쌓이는 수·크기(칸)·바닥 모양·아이콘 | `/Game/PG/Table/ItemTable` (크기 = GridSize, 예: 방탄조끼 2003 = 2×2) |
| 처음 갖고 시작하는 짐 | `/Game/PG/Table/StarterInventoryTable` (Container: Stash 창고 / Pocket 주머니 / Equip 바로 장착) |
| 창고·주머니 크기 | `Config/DefaultGame.ini` `[/Script/ProjectPG.InventorySubSystem]` `StashSize=(X=10,Y=10)`, `PocketSize=(X=5,Y=4)` |
| 가방 안 칸 수 | `/Game/PG/Table/BackpackTable` |
| 맵 상자 자리에 나오는 아이템·확률 | `/Game/PG/LevelDesign/Data/DT_LootSpawn` (Weight 무게, MinTier 최소 등급, 개수 범위) |
| 바닥에 놓인 아이템 모습 | `/Game/PG/Blueprint/Item/BP_WorldItem` (메시 없을 때 대신 모양, 최대 크기) |

### 2-1. 아이콘
- 아이콘 위치: `/Game/PG/UI/ItemIcons/T_Icon_<번호>` (바닥 모양을 위에서 찍은 것), `/Game/PG/UI/ItemIcons_Custom/` (손으로 만든 그림).
- **아이콘 하나를 그림으로 바꾸기**: `Tools/icons_custom/T_Icon_<번호>.png`(배경 투명, 256×256 권장)를 넣고 아래 명령 실행.
  그림이 있으면 그림이 먼저 쓰인다(방탄조끼 2003 이 이렇게 들어감).
- 찍은 아이콘은 그림 크기가 아이템 칸 비율과 같다(칸 하나 64픽셀, 4×2 → 256×128). 물건의 가장 얇은 쪽에서 찍는다(총은 옆모습).
  방향만 틀리면 `make_item_icons.py` 맨 위 **OVERRIDES** 에 아이템 번호로 `flip_x`(좌우)·`flip_y`(위아래)·`axis`(보는 쪽)를 적고 다시 돌린다.
- 모델이 없거나 찍어도 알아보기 힘든 것(활·물·전투식량·문서·레시피·희귀 재료·드래곤 비늘·절단기·가방·방탄조끼)은 그림(`Tools/icons_custom`)으로 들어가 있다.
- **아이콘 전부 다시 찍기**(아이템을 추가했거나 바닥 모양을 바꿨을 때):
  ```
  UnrealEditor.exe <프로젝트>\ProjectPG.uproject -ExecutePythonScript="<프로젝트>/Tools/wbp/make_item_icons.py" -RenderOffScreen -unattended -nosplash
  ```
  에디터가 화면 밖에서 떠서 찍고 스스로 닫힌다(3분쯤). 에디터가 이미 켜져 있으면 끄고 실행.

---

## 3. 다시 만들기 스크립트(처음 모양으로 되돌릴 때)

| 스크립트 | 하는 일 |
|---|---|
| `Tools/wbp/build_matching.py`, `build_option.py` | 매칭·옵션 WBP 처음 모양 |
| `Tools/wbp/restyle_inventory.py` | 캐릭터 화면·칸 색·우클릭 버튼·설명 창·로비 버튼을 기획서 모양으로 |
| `Tools/wbp/setup_lobby_flow.py` | 시작 짐 표·화면 등록(BP_GameInstance) |
| `Tools/level/build_title_level.py` | 타이틀 레벨(배경·카메라·캐릭터 자리) |
| `Tools/wbp/icon_fit.py` | 아이템 칸·장비 칸 아이콘을 비율 지키게(늘어나지 않게) |
| `Tools/wbp/check_bps.py` | PG 블루프린트 전부 다시 컴파일해 오류 확인 |

실행: `UnrealEditor-Cmd.exe <프로젝트>\ProjectPG.uproject -run=pythonscript -script="<스크립트 경로>" -unattended -nosplash -nullrhi` (에디터 끈 상태).
**주의:** 손으로 고친 WBP 위에 다시 돌리면 그 스크립트가 만지는 부분은 스크립트 값으로 돌아간다.

---

## 4. 새 화면을 더할 때
1. C++ 부모가 필요하면 `UUserWidget` 자식을 만들고 칸은 `meta=(BindWidgetOptional)` 로(이름으로 붙음).
2. WBP 를 그 부모로 만든다.
3. 화면 종류 목록 `EUIType`(`Core/UIManagerSubSystem.h`) **맨 끝에** 이름 추가(중간에 끼우면 BP 에 저장된 값이 꼬일 수 있다).
4. `BP_GameInstance` 이벤트 그래프의 `Register UIClass` 줄 끝에 노드 하나 추가(종류 + WBP).
5. 여는 곳에서 `UUIManagerSubSystem::OpenUI(EUIType::새이름)`.

---

## 5. 확인
- 화면 없이 버튼 눌러 보기·스크린샷(개발 빌드):
  `-PGClick=Character@10+Tooltip@14 -PGShot=12+15 -PGQuitAt=17` → `Saved/Screenshots/`
- 2명 매칭: 게임 2개를 `-PGClick=GameStart@6` 로 띄우면 1번은 방장, 2번은 접속.
- 맵이 바뀌지 않았나: `-PGMapSeed=12345` → `layout_hash=035300A1`, `point_hash=34F20671`, `item_hash=4D8A5D0B`.
