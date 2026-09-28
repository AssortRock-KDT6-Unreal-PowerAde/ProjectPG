# 오브젝트·맵 배치·로봇/차량 인계 문서 (2026-09-17)

담당: 우현제 (오브젝트, 맵 디자인, 로봇 기믹). 원형 코드는 `E:\TestProject2` (개인 저장소), 팀 저장소 이식은 이 문서 기준으로.

한 줄 요약: **팀 캐릭터는 인터페이스 함수 4개만 구현하면 상자·바닥 아이템·시체·문·차량·로봇 전부 그대로 붙는다.** UI 는 컴포넌트 함수 3개(내용물 보기 / 하나 집기 / 전부 집기)만 부르면 된다.

---

## 1. 캐릭터 담당이 할 것

### 1-1. `IPGItemReceiver` 구현 (필수) — `Public/Objects/PGItemReceiverInterface.h`
오브젝트는 인벤토리를 모른다. "아이템 줄게 / 있어? / 써도 돼?" 만 묻는다.

```cpp
class ACustomPlayerCharacter : public ACharacter, public IPGItemReceiver
{
    // 가방이 꽉 차서 못 받으면 false. 오브젝트는 false 면 자기 상태를 안 바꾼다(상자 내용물 그대로 남음).
    virtual bool ReceiveItem_Implementation(FName ItemId, int32 Count) override { return Inventory->AddItemByID(ItemId, Count); }
    virtual bool HasItem_Implementation(FName ItemId, int32 Count) const override;   // 열쇠·연료·납품 확인
    virtual bool ConsumeItem_Implementation(FName ItemId, int32 Count) override;     // 열쇠 소모, 연료 주입 (서버에서만)
    virtual FName GetPlayerKey_Implementation() const override;                       // 플레이어별 탈출구·퀘스트 구분 키
};
```

### 1-2. 컴포넌트 붙이기 + 입력 연결
| 컴포넌트 | 붙이는 이유 | Enhanced Input 에서 부를 함수 |
|---|---|---|
| `UPGInteractionComponent` | F 상호작용 (시선 스윕, 프롬프트, 서버 RPC) | `BeginInteract()` (눌림) / `EndInteract()` (뗌, 유지형 취소) / `TakeItemFromTarget(int32 Slot)` (칸 하나 집기) / `CycleTarget(+1/-1)` (마우스 휠: 겹친 대상 고르기) |
| `UPGWeaponComponent` (선택) | 주운 무기 장착·공격(트레이스) | `Attack()`, `CycleNext()`, `Equip(FName)`; 아이템 받을 때 `NotifyItemReceived(ItemId)` 한 줄 |

참고 구현: `Private/Actors/LevelDesignValidationCharacter.cpp` (키 직접 읽는 임시 버전. `PollInteraction / PollLootSlots / PollWeapon` 세 함수가 위 표 그대로).

### 1-3. 캐릭터 사망 시
`UPGLootableComponent` 를 캐릭터에 붙여 두고 죽을 때 `Lootable->ActivateLoot(Seed)` 한 번. 그러면 시체가 F 대상이 되고 루팅 테이블(`LT_CorpseA/B/C`)이 굴러간다. 몬스터 `APGMonsterCharacter::Die()` 가 예시.

### 1-4. 장착 외형 — `UPGWearableComponent` (`Public/Objects/PGWearableComponent.h`)
팀 플레이어 캐릭터와 같은 **Quantum Modular Character** 기준. 이 컴포넌트를 캐릭터에 붙이고 `Equip(ItemId, OutPrevious)` 만 부르면 겉모습이 바뀐다.
- 본체 메시(머리) 하나만 애니메이션을 돌리고, 팔·셔츠·바지·조끼·파우치·권총집은 스켈레탈 파츠가 **LeaderPose** 로 따라간다. 모자·배낭은 스태틱 메시를 뼈에 붙인다. (옷을 스태틱 메시로 소켓에 붙이면 팔을 들 때 소매가 따로 논다.)
- 팀 `UEquipComponent` 와의 관계: 그쪽 = 무엇을 장착했나(데이터·능력치), 이쪽 = 어떻게 보이나. 장착이 바뀔 때 `Equip` / `Unequip(Slot)` 을 불러 주면 된다. 캐릭터 메시가 이미 Quantum 으로 조립돼 있으면 `bBuildQuantumBody = false`.
- 팩이 슬롯마다 메시 한 종류뿐이라 "다른 옷" = "다른 색". 색은 ItemId 로 온다 (`Public/Objects/PGWearableColors.h` 의 표):

| 슬롯 | ItemId (원래색 / 색 변형) |
|---|---|
| Top 셔츠 (항상 입음) | `Shirt` / `Shirt_Red` / `Shirt_Olive` / `Shirt_Black` |
| Bottom 바지 (항상 입음) | `Pants` / `Pants_Black` / `Pants_Khaki` |
| Vest 방탄조끼 | `Armor_Vest` / `Armor_Vest_Black` / `Armor_Vest_Olive` |
| Cap 모자 | `Helmet` / `Helmet_Black` / `Helmet_Red` |
| Pouch 가슴 파우치 | `ChestPouch` / `ChestPouch_Black` |
| Holster 권총집 | `Holster` / `Holster_Black` |
| Backpack (팩에 없음 → 구운 배낭을 등에 붙임) | `Backpack` / `Backpack_Black` / `Backpack_Olive` |
| Shoes (팩에 없음 → 겉모습 없음) | `Shoes` / `Shoes_Black` / `Shoes_Brown` |

- 색 머티리얼·바닥 메시는 `Tools/make_quantum_wearables.py` 가 만든다(팩 머티리얼에 색 파라미터가 없어서 복사본에 Desaturation·Tint 를 끼움). 에셋은 `/Game/PG/Characters/Quantum`.
- 바닥·상자·시체에서 나오는 옷은 오브젝트 쪽이 시드로 색을 정해서 준다. 검증 캐릭터는 주우면 바로 입고, 입고 있던 것은 발 앞에 떨군다(`ReceiveItem_Implementation` 참고).

---

## 2. UI 담당이 부를 것

### 2-1. 상호작용 프롬프트
`UPGInteractionComponent`: `OnTargetChanged(Actor, Prompt)` 델리게이트, `GetCurrentPrompt()`, `GetHoldProgress()` (유지형 게이지 0~1).

예시 구현: `Public/UI/PGInteractionPromptWidget.h` — 화면 가운데 `[F] 갈색 신발 줍기` + 게이지 + 겹친 대상 목록을 C++ 위젯 트리로 그린다(위젯 블루프린트 없음). 글씨체는 `PGUiFont::Get(크기, 굵기)` — `Content/PG/UI/Fonts` 의 Pretendard(OFL) 파일을 직접 읽는다. 패키징 시 그 폴더를 "Additional Non-Asset Directories to Copy" 에 추가할 것.

**겹친 대상 목록 (기획 3.3.7)**: 시선이 닿은 지점 둘레 90cm 의 상호작용 대상이 전부 후보가 된다. `OnCandidatesChanged(Candidates, SelectedIndex)` 로 목록과 선택 번호를 받고, 줄 글자는 `GetCandidatePrompt(i)`. 후보가 1개면 목록을 안 그려도 된다. 고르기는 전부 로컬이고 서버 RPC 는 원래 대상을 인자로 받으므로 네트워크는 그대로.

### 2-2. 루팅 창 (상자·시체 공통) — 타르코프식 "칸"
상자 `AItemContainerActor::GetStorage()` 와 시체의 `UPGLootableComponent` 는 같은 컴포넌트.

| 함수 | 역할 |
|---|---|
| `GetContents()` → `TArray<FPGItemStack>` (ItemId, Count) | 칸 그리기. **리플리케이트됨** — 클라에서 그대로 읽으면 됨 |
| `UPGInteractionComponent::TakeItemFromTarget(Slot)` | 칸 하나 집기 (클라→서버 RPC, 거리·칸 유효성 서버 검증) |
| F 한 번 더 (`BeginInteract`) | 남은 것 전부 집기 |
| `OnLooted`, `OnEmptied` 델리게이트 | 창 갱신 / 닫기 |
| `TryBeginUse / EndUse` | 한 사람만 열게 잠금 (열려 있는 동안 다른 플레이어에겐 "사용 중") |

흐름: 상자 F → 열림(뚜껑 애니) + 내용물이 상자 칸에 **남음** (인벤으로 안 감) → UI 가 `GetContents` 로 그림 → 칸 클릭 = `TakeItemFromTarget(i)`.

### 2-3. 아이템 가치·상점 거래 (2026-09-20 추가)
`UPGItemValueLibrary` (`Public/Objects/PGItemValue.h`). **전부 static 순수 함수** — 월드도 액터도 안 본다. UI 는 읽기만 하고, 서버는 같은 시드로 다시 계산해 검증한다.

기획서에 배율만 있고(구매 x1.1~1.5, 판매 x0.6~0.8) 곱할 **기본가치**가 없어서 그 표를 코드로 채운 것이다. 기준점은 재화: `Money` 한 개 = 1 (기획 21쪽이 재화를 아이템으로 본다).

| 함수 | 역할 |
|---|---|
| `GetItemValue(ItemId)` → `int32` | 한 개당 기본가치. 색 변형(`Pants_Black`)은 원래색(`Pants`) 값을 물려받는다. 모르는 아이템은 0 |
| `GetStackValue(FPGItemStack)` / `GetTotalValue(TArray<FPGItemStack>)` | 묶음·소지품 전체 가치. **전체 가치가 상점 진열을 정하는 입력**(기획 30쪽) |
| `GetItemGrade(ItemId)` → `EPGItemGrade` | 잡템/일반/고급/희귀/최상급. 값 구간과 묶여 있고 스모크가 어긋남을 잡는다 |
| `IsTradable(ItemId)` | 퀘스트 아이템·모르는 아이템은 false → 거래 자체가 막힌다 |
| `GetBuyPrice(ItemId, ShopSeed)` / `GetSellPrice(ItemId, ShopSeed)` | 기획 14쪽 배율. **상점 시드 + ItemId 로 고정**되어 한 상점 안에서 값이 흔들리지 않는다 |
| `EvaluateTrade(Sold, Bought, ShopSeed)` → `FPGTradeResult` | 거래 판정. `bValid`(판 가치 > 산 가치), `Difference`(차액 = 돌려줄 재화), `RejectedItemId`(거래 금지 아이템) |

- **서버 담당**: 클라이언트가 보낸 거래는 `EvaluateTrade` 를 **상점 시드로 다시 돌려** `bValid` 와 `Difference` 만 믿으면 된다. 진열 목록을 복제할 필요 없이 시드만 보내면 되는 이유다.
- **UI 담당**: 칸에 `GetItemValue` / `GetItemGrade` 를 그리고, 거래 버튼 활성 여부는 `EvaluateTrade(...).bValid` 그대로 쓴다. 표시 문구 `거래가치: +12,345` 의 숫자가 `Difference`.
- 표 원본은 코드(`PGItemValue.cpp::GetAllRows`), `Docs/DT_PGItemValue.csv` 는 내보낸 것. 값마다 **왜 그 값인가**가 `Reason` 에 한 줄씩 적혀 있다.
- 검증: `PG.ItemValues [상점시드]` — 가치표 + 루팅 테이블 기대가치를 로그로 찍는다. 스모크의 `item_value_*` / `item_trade_*` 검사도 같은 내용을 본다.


---

## 3. 서버 담당이 알 것

- **상태 변경은 전부 서버.** 클라이언트 경로는 컴포넌트의 `Server*` RPC 하나씩: `ServerInteract / ServerBeginUse / ServerEndUse / ServerTakeItem` (상호작용), `ServerEquip / ServerAttack` (무기), `ServerDismount / ServerSetDriveInput` (차량·로봇). 서버는 거리·칸 번호를 다시 검사한다.
- 리플리케이트 필드: 상자 `bIsOpen`, 루팅 `bActivated / Contents / CurrentUser`, 몬스터 `bDead / Health / bDormant / Faction`, 무기 `EquippedItemId`.
- **맵 생성 후 자동 배치 흐름** (`Private/GameModes/GameModePG.cpp::HandleLevelDesignPointsBuilt`):
  1. `UPGObjectSpawnerSubsystem::SpawnFromLevelDesignPoints` — Loot 포인트에 상자 + 옆 바닥에 무기·탄약·소비품 흩뿌리기, Exit → 탈출구, Quest → 퀘스트 아이템
  2. `PGCombatSpawner::SpawnFromPoints` — 워존 3등분(세력 구역) → 보스 로봇 → 탈것 로봇 → 몬스터(세력 A/B/C + 리스폰 기록) → 플레이어 스폰 옆 차량
  3. `PGLevelDoorConverter::ConvertLevelDoors` — 레벨에 놓인 문 메시를 열리는 문 액터로
  전부 **맵 시드**로 정해진다 (같은 시드 = 같은 배치). 데디케이트 서버에서 그대로 돎 (`NM_Client` 면 아무것도 안 함).
- 설정은 `Config/DefaultGame.ini` `[/Script/ProjectPG.PGObjectSettings]` (아래 표).

---

## 4. 몬스터 담당이 알 것

`APGMonsterCharacter` 는 **임시** (팀 몬스터 클래스가 나오기 전 검증용). 넘겨받을 규칙 세 가지:

| 규칙 | 위치 | 내용 |
|---|---|---|
| 표적 판정 | `MonsterAIController::IsValidTarget` (함수 1개) | 플레이어 조종 폰 = 표적. **다른 세력 몬스터 = 표적**(세력 충돌). 같은 세력·세력 없음·시체 = 아님 |
| 세력 배치 | `PGCombatSpawner.cpp::AssignWarZoneSectors / PlaceMonsters` | 워존을 중심 각도로 120°씩 3등분, 구역마다 세력 하나(시드로 돌림). A 구역 작은 몬스터 떼, B 구역 크리처(램페이지), C 구역 보스 로봇. 워존 밖 전부 A |
| 세력별 손맛·리스폰 | `PGCombatSpawner.cpp::ApplyFactionTuning`, `APGMonsterRespawner` | A 체력 0.7·공격 0.7·60초 리스폰, B 180초, C 리스폰 없음·크기 1.3. 리스폰은 자리 주변 30m 에 플레이어 없을 때만 |

팀 몬스터 클래스로 바꾸는 법: 설정 `MonsterClass` 에 지정 (단 `APGMonsterCharacter` 파생이어야 함). 파생이 아니면 `PGCombatSpawner::SpawnMonster` **한 함수**만 팀 클래스로 바꾸면 나머지(배치·세력·리스폰)는 그대로.

---

## 5. 설정 (`DefaultGame.ini` → 프로젝트 세팅 "ProjectPG Objects")

| 항목 | 기본 | 뜻 |
|---|---|---|
| `bAutoSpawnFromLevelDesignPoints` | True | 맵 생성 후 자동 배치 켬 |
| `GroundLootChance / Min / Max / Radius` | 0.5 / 1 / 3 / 220 | 상자 옆 바닥 루팅 |
| `bAutoSpawnCombat` | True | 몬스터·보스·로봇·차량 자동 배치 |
| `MonsterCount` / `WarZoneMonsterCount` | 8 / 12 | 워존 밖 A / 워존 A 구역 |
| `FactionBCount` / `FactionCCount` | 3 / 0 | B 구역 크리처 수 / C 구역 몬스터 수(보스 외) |
| `FactionA/B/CPresets` | A=슬라임·선인장·비홀더·상자괴물, B=Rampage | 외형 프리셋 |
| `FactionA/BRespawnSeconds`, `RespawnPlayerClearRadius` | 60 / 180 / 3000 | 리스폰 |
| `FactionBScale` / `FactionCScale` | 5 / 1.3 | 세력별 몸집 배율(발 높이·사거리·속도 같이) |
| `FactionBSightRadius` / `FactionBLeashRadius` | 3500 / 6000 | B 크리처 시야 35m, 영역 60m: 스폰 자리에서 60m 넘게 끌려 나가면 표적을 버리고 돌아감, 24m 안으로 들어오면 다시 싸움. 영역 밖 대상은 처음부터 안 쫓음 |
| `BossRespawnSeconds` / `RideableRobotRespawnSeconds` | 300 / 120 | 보스·탈것 로봇 리스폰. 빈 AI 포인트 중 플레이어에서 먼 곳에 랜덤 |
| `BossFaction` | C | 보스 로봇 세력. None 이면 플레이어만 상대 |
| `bSpawnBoss`, `BossScale` | True / 8 | 보스 로봇 |
| `RideableRobotCount`, `RideableRobotScale` | 1 / 8 | 탈것 로봇 (노란 Skin3, 변기로 잠복) |
| `VehicleCount`, `VehiclePresets` | 2 / SportsCar·Pickup·SUV·Hatchback | 차량 |

---

## 6. 콘솔 명령 (PIE) — `Private/Debug/PGConsoleCommands.cpp`

| 명령 | 용도 |
|---|---|
| `PG.GoTo boss\|robot\|vehicle\|monster\|box\|item` | 가장 가까운 대상 앞으로 순간이동 (맵이 넓어서) |
| `PG.ShowFactions [초]` | 몬스터 머리 위 세력 글자 (A 초록 / B 노랑 / C 빨강) + 체력 |
| `PG.ShowAllObjects` | 카탈로그 전부 앞에 격자로 |
| `PG.SpawnMonster [preset]`, `PG.SpawnRobot [boss\|ride] [scale]`, `PG.SpawnVehicle [preset]` | 낱개 스폰 |
| `PG.GiveItem <ItemId> [n]` | 아이템 주기 (IPGItemReceiver 경유) |
| `PG.SpawnCombatFromPoints`, `PG.SpawnObjectsFromPoints`, `PG.ConvertLevelDoors` | 자동 배치 수동 실행 |
| `PG.ObjectSmokeTest` | 스모크 테스트 |
| `PG.DebugHud 0/1` | 임시 HUD (HP / 무기 / 인벤 / 보는 상자 칸) |
| `PG.DebugCombat 0/1` | 공격 판정 그리기(로봇 스윕 캡슐, 무기 트레이스 선). 기본 0 |

---

## 7. 검증

에디터 없이 (GPU 안 씀, 학원컴 OK):
```
UnrealEditor-Cmd.exe E:\TestProject2\ProjectPG.uproject "/Engine/Maps/Entry?game=/Script/ProjectPG.PGObjectTestGameMode" -game -nullrhi -unattended -nosound -nosplash -log -PGObjectSmokeTest
```
→ 로그 `PGObjectSmokeTest summary: passed=46 failed=0`. 상자 열기→칸→하나 집기→전부 집기, 문/열쇠, 탈출구, 시체 루팅, 옷 색 표(에셋 존재·시드 고정·버린 옷 색), 겹친 대상 후보·휠, 카탈로그 104행 검사.

자산 스크립트 (`Tools/`, 커맨드렛 `-run=pythonscript -script=...`, **PowerShell 에서** 실행 — Git Bash 는 `/Game/..` 경로를 바꿔치기함):
- `cap_texture_size.py` — 팩 텍스처 최대 2048 (VRAM. 컴마다 로컬 자산이라 학원컴에서 다시 한 번)
- `enable_nanite.py` — 팩 스태틱 메시 나나이트 일괄 (`PG_NANITE_APPLY=0` 이면 목록만)
- `fix_nanite_material_flags.py` — 나나이트 메시가 쓰는 머티리얼에 `bUsedWithNanite` 켜고 저장
- `make_floor_item_meshes.py` — 하의·신발·가방 바닥 메시(`/Game/PG/Props/FloorItems`). Survival_Character 착용 부품 스켈레탈을 Geometry Scripting 으로 스태틱으로 굽고 삼각형 축소(`PG_FLOOR_ITEM_TRIS`, 기본 500). 다시 돌리면 같은 경로에 덮어씀

---

## 8. 파일 지도

```
Public/Objects/        오브젝트 원형 (상자·문·바닥아이템·탈출구·퀘스트·상점) + IPGItemReceiver + 상호작용/루팅 컴포넌트 + 설정
Public/Interaction/    IInteractable (F 대상 인터페이스)
Public/Monster/        임시 몬스터 (세력 enum 포함) + AI 컨트롤러
Public/Robot/          로봇 (보스 / 탑승, 변기 잠복)
Public/Vehicle/        Chaos 차량 (탑승, 밀쳐내기, 뒤집힘 복구) + 바퀴 애님
Public/Weapons/        무기 정의 + 무기 컴포넌트 (트레이스 공격)
Public/Combat/         PGCombatSpawner (맵 배치), PGMonsterRespawner (리스폰)
Public/Common/         PGPhysicsUtil (소품 밀쳐내기 대리 액터), PGKeyPolling (임시 입력)
Private/Debug/         PG.* 콘솔 명령
Private/GameModes/GameModePG.cpp   자동 배치 진입점
Docs/DT_PGObjectCatalog.csv, DT_PGLootTables.csv   카탈로그·루팅 테이블 (코드 RegisterDefaultCatalog 가 원본, CSV 는 내보낸 것)
```

---

## 9. 알려진 한계 / 다음 후보

- 차량 연료 필요(기획 3.3.7), 차량·헬기·선박 탈출구 없음.
- 램페이지·로봇은 스켈레탈이라 나나이트 대상 아님 → LOD 로 (에디터).
- VRAM: RTX 5060 Ti 에서 PIE 9.6GB. 8GB 컴은 텍스처 2K 스크립트 + `r.Streaming.PoolSize=1500` + VSM 끄기 필요.
- 탑승 로봇 카메라 거리: `APGRobotCharacter::RideCameraDistanceFactor / RideCameraHeightFactor` (0.45 / 0.6). 사람이 탄 로봇도 걸으며 소품을 날린다.
- 빈 차량은 주차 상태(네 바퀴 핸드브레이크)라 사람이 밀어도 안 굴러간다. 캐릭터는 차 지붕에 올라설 수 없다.
- 보스가 하늘로 튕겨 날아간 현상 원인 미확인. 재현되면 로그 `launched:` 줄(밟고 있던 대상)을 볼 것.
- 로봇 보스·탑승은 기획서에 없는 개인 기믹. `bSpawnBoss=False`, `RideableRobotCount=0` 으로 끔.
