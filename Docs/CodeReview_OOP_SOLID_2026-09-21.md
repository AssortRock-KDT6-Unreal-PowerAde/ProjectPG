# 코드 검사 — 객체지향·SOLID·남은 최적화 (2026-09-21)

읽기 전용 검사. 코드는 고치지 않았다. 기준은 `team-merge` 브랜치 `08134ab`.
근거는 전부 `파일:줄` 이다(경로는 `Source/ProjectPG/` 아래). 확인 못 한 건 "미확인"이라 적었다.

## 5줄 요약

1. **잘 된 것**: 오브젝트 원형 9개 + 인터페이스 4개(`IInteractable`·`IPGRideable`·`IPGItemReceiver`·`IPGDeviceSignalTarget`)는 상속·다형성·인터페이스 분리·의존 역전이 모두 교과서대로 돼 있다. 면접에서 자신 있게 꺼낼 수 있는 부분이다.
2. **제일 큰 문제**: `WarZoneFootprintPreview.cpp` 7,074줄 한 클래스가 "배치 계산·시각화·시설 스트리밍·게임플레이 포인트·검증 13종·붕괴 연출·맵 경계" 일곱 가지를 다 한다. 한 책임 원칙(SRP) 위반이 뻔하고, 977줄짜리 함수도 있다.
3. **면접 전 고칠 것 ①**: 탈것 3종(`PGVehiclePawn`·`PGTankPawn`·`PGRobotCharacter`)에 `Mount/Dismount` 가 글자 그대로 3번 복사돼 있다 → `UPGRideComponent` 하나로 뽑기(반나절).
4. **면접 전 고칠 것 ②**: `WarZoneFootprintPreview` 에서 검증 13종(`Verify*`)만이라도 `UPGMapVerifier` 로 떼기(하루). 코드는 안 바뀌고 옮기기만 하므로 위험이 적고, "쪼갤 줄 안다"는 증거가 된다.
5. **면접 전 고칠 것 ③**: 설정 파일이 거의 비어 있다(`gc.*`·PSO 캐시·`net.*` 0건). 두 줄짜리 ini 변경으로 "첫 등장 끊김"과 "GC 스파이크"를 잡는 이야기를 만들 수 있다.

**팀원 코드(판정 제외)**: `Characters/Custom*`, `Monster/`, `Server/`, `UI/Inventory*`, `Components/MapGeneratorComponent`, `Actors/MapTile`, `GameModes/GameModePG` 는 보지 않았다. 다만 `Monster/PGMonsterCharacter.cpp:51-52` 의 애니 최적화 두 줄은 사용자 클래스에 없어서 검사 2에서 언급한다.

---

## 검사 1 — 객체지향 4대 원칙 + SOLID

### 한눈에

| 원칙 | 판정 | 한 줄 |
|---|---|---|
| 캡슐화 | 좋음 | 잔해·미사일 서브시스템은 속을 완전히 숨겼다. 탈것은 부품이 public 이다 |
| 상속 | 좋음(원형) / 나쁨(탈것) | 원형은 템플릿 메서드로 깔끔, 탈것은 복사·붙여넣기 |
| 다형성 | 좋음 | 상호작용 컴포넌트가 구체 클래스를 거의 모른다 |
| 추상화 | 보통 | 인터페이스 경계는 좋고, 맵 클래스는 추상화 없이 통짜 |
| SRP | **나쁨** | 7,074줄·2,338줄 클래스 두 개 |
| OCP | 보통 | 카탈로그 행은 열려 있고, enum switch 는 닫혀 있다 |
| LSP | 좋음 | 원형 9개가 부모 자리를 그대로 대신한다. 로봇=몬스터 상속만 애매 |
| ISP | **좋음** | 인터페이스 4개가 전부 작다(1~4 함수) |
| DIP | 보통 | 인벤토리 경계는 모범, 물리 유틸·피날레가 구체 클래스를 안다 |

### 캡슐화 (속을 감추기)

**잘 지킨 예**
- `Public/Common/PGDebrisSubsystem.h:41-69` — 밖에 여는 건 `Launch/Kick/IsDebris/QueueSupportCheck` 네 개뿐이고, 잔해 한 조각의 상태(`FPiece`, `:70`)와 풀 관리는 전부 `private:`(`:69`) 아래에 있다. 호출자는 잔해가 물리인지 근사 이동인지 모른다.
- `Public/Objects/PGInteractableActorBase.h:65-103` — `CurrentUser`(사용 중 잠금)는 `protected` 이고 바꾸는 길은 `TryBeginUse/EndUse`(`Private/Objects/PGInteractableActorBase.cpp:86-106`)뿐이다. 서버 권한 검사가 그 두 함수 안에 있어서 우회가 안 된다.

**어긴 예**
- `Public/Vehicle/PGTankPawn.h:34-158` — `public:` 구간에 충돌 상자 `Body`(`:76`), 메시, 카메라까지 부품이 다 노출돼 있다. 바깥에서 `Body` 를 바꿔도 막을 수 없다. 반면 `Public/Finale/PGBattleshipActor.h:157` 은 부품을 전부 `protected` 로 두었다 — 같은 사람이 쓴 코드인데 규칙이 다르다.
- `Public/Common/PGDebrisSubsystem.h:61` `static bool bCollapseFall` — public 전역 스위치. `Private/Common/PGPhysicsUtil.cpp:268` 이 이 값을 읽어 동작을 바꾼다. 인자로 넘기면 될 것을 전역 상태로 해결했다(`TGuardValue` 로 감싸긴 했다, `PGDebrisSubsystem.cpp:811`).
- `Public/Robot/PGRobotCharacter.h:71` `bool bRideable = true;` public 필드. `ConfigureAsBoss/ConfigureAsRideable`(`Private/Robot/PGRobotCharacter.cpp:99,108`)가 이미 있는데 필드도 열려 있어 둘이 어긋날 수 있다.

**면접 한 문장**: "잔해 시스템은 호출자가 '날려라'만 말하고, 물리를 쓸지 근사 이동을 쓸지는 서브시스템이 예산을 보고 스스로 정합니다."

### 상속

**잘 지킨 예**
- `Public/Objects/PGInteractableActorBase.h:10` 주석 그대로: 자식은 `HandleInteract / CanInteractInternal / GetPromptInternal` 세 개만 채운다. 부모의 `Interact_Implementation`(`Private/Objects/PGInteractableActorBase.cpp:50-65`)이 권한 검사 → 가능 여부 → 처리 → 델리게이트 순서를 고정한다(템플릿 메서드). 원형 9개(`ItemContainerActor.h:62-64`, `PGDoorActor.h:51-53`, `PGDeviceActor.h:57-59`, `PGExtractionZoneActor.h:86-88`, `PGFloorItemActor.h:42-44`, `PGQuestObjectActor.h:46-48`, `PGServiceInteractionActor.h:53-55`, `PGDestructibleActor.h:51-53`, `PGTransformNPCActor.h:139-141`)가 예외 없이 같은 세 함수를 덮어쓴다.
- 상속 대신 조합을 쓴 곳: 잠금은 `UPGLockComponent`(`Public/Objects/PGLockComponent.h:15`, 76줄)를 상자·문이 같이 붙인다. 시체 루팅은 `UPGLootableComponent`(`Public/Objects/PGLootableComponent.h:20`)를 캐릭터에 붙인다 — 팀원 캐릭터 클래스를 상속하지 않아도 된다.

**어긴 예**
- **탈것 3종 복사·붙여넣기**. `Mount()` 가 `Private/Vehicle/PGVehiclePawn.cpp:191`, `Private/Vehicle/PGTankPawn.cpp:167`, `Private/Robot/PGRobotCharacter.cpp:183` 에 거의 같은 30줄로 세 번 있다(권한 검사 → `Seat->TryEnter` → 탑승자 숨김·충돌 끄기 → 붙이기 → `UnPossess/Possess` → `bDismountKeyWasDown = true`). `Dismount()` 도 `:253 / :195 / :219` 로 세 번. 상태 필드도 세 헤더에 똑같이 있다(`RiderPawn`·`bDismountKeyWasDown`·`MountedTime`: `PGVehiclePawn.h:102,104,113`, `PGTankPawn.h:186,212,213`, `PGRobotCharacter.h:124,126,130`). 세 클래스의 부모가 `AWheeledVehiclePawn / APawn / ACharacter` 로 다르니 공통 부모는 못 만들고, **`UPGRideComponent` 로 뽑는 게 맞다**(`UPGSeatComponent` 가 이미 있으니 그 위에 얹는다).
- `RamMonster` 도 `PGTankPawn.cpp:511` 과 `PGVehiclePawn.cpp:333` 에 두 번(속도 문턱·쿨다운·피해 계산만 숫자가 다르다).
- `Public/Actors/TacticalTileRoadStraight.h:16` 과 `Public/Actors/TacticalTileActor.h:41` 은 둘 다 `AActor` 직계이고 서로 모른다. 전자는 `TacticalTileTestGameMode.cpp` 에서만 쓰는 옛 버전이다. `Public/Actors/Tile.h:10` `ATile` 은 참조 0건.

**면접 한 문장**: "행동이 같고 데이터만 다른 것은 상속이 아니라 카탈로그 행으로, 부모가 다른 것끼리 공유하는 행동은 상속이 아니라 컴포넌트로 — 탈것 탑승이 후자인데 아직 못 옮겼습니다."

### 다형성

**잘 지킨 예**
- `Private/Objects/PGInteractionComponent.cpp:106-110` — 대상이 무엇이든 `ImplementsInterface(UInteractable)` 만 보고 `Execute_GetInteractionPrompt / Execute_CanInteract` 를 부른다. 상자·문·차·탱크·로봇·변신 NPC 가 한 코드 경로로 돈다. `:384-386` 서버 쪽도 같다.
- `Private/Objects/PGDeviceActor.cpp:129` — 장치는 `IPGDeviceSignalTarget::Execute_OnDeviceSignal` 만 부르고, 받는 쪽이 문(`PGDoorActor.h:29`)인지 다른 장치(`PGDeviceActor.h:32`)인지 모른다.

**어긴 예**
- `Private/Objects/PGInteractionComponent.cpp:111-112` — 유지 시간(`GetInteractSeconds`)을 얻으려고 `Cast<APGInteractableActorBase>` 를 한다. 인터페이스에 없는 정보를 구체 부모로 뚫어서 가져오는 것이다. 탈것은 유지 시간을 가질 수 없게 된다. `IInteractable` 에 `GetHoldSeconds()` 를 넣으면 해결.
- `:117-124` — 시체는 `IInteractable` 을 안 쓰고 `FindComponentByClass<UPGLootableComponent>` 로 따로 처리한다. 두 갈래 코드 경로. 컴포넌트가 소유 액터 대신 인터페이스를 구현하게 하면 한 경로로 합쳐진다.
- `Private/Debug/PGConsoleCommands.cpp` 의 `IsA<APGVehiclePawn> / IsA<APGRobotCharacter> / IsA<AItemContainerActor>` 사슬 — 디버그 코드라 허용 범위.

**면접 한 문장**: "플레이어 쪽 상호작용 코드는 상자가 뭔지 모릅니다. 인터페이스 세 함수만 부르고, 새 오브젝트가 생겨도 그 코드는 안 바뀝니다."

### 추상화

**잘 지킨 예**
- `Public/Objects/PGItemReceiverInterface.h:1-6` — "오브젝트는 '아이템을 주고 싶다'고만 말한다." 오브젝트 코드가 인벤토리 헤더를 한 줄도 포함하지 않는다. 팀 캐릭터(`Public/Characters/CustomPlayerCharacter.h:22`)와 검증용 캐릭터(`Public/Actors/LevelDesignValidationCharacter.h:21`), 테스트 폰(`Public/Objects/PGObjectSmokeTest.h:27`) 셋이 같은 인터페이스를 구현해 서로 바꿔 끼워진다.
- `Public/Common/PGPhysicsUtil.h` 의 `namespace PGPhysicsUtil` — `TryKnockProp` 한 함수 뒤에 대리 액터 생성·예산·잔해 위임이 숨어 있다.

**어긴 예**
- `Public/Actors/WarZoneFootprintPreview.h:236-632` — `private` 필드 200개 남짓, `UPROPERTY` 94개. "맵 시각 레이어"라는 개념이 클래스 하나에 통째로 들어 있어 추상화 단계가 없다(아래 SRP).
- `Private/Actors/WarZoneFootprintPreview.cpp:982-987` — Tick 안에 `4000.0f`(2셀 크기), `6000.0f`(막이 반경) 같은 의미 있는 숫자가 이름 없이 있다. 같은 파일 `:213-235` 에는 `DesignCellSize`·`LakeSurfaceZ` 등 이름 붙인 상수가 21개 있으니 규칙은 알고 있는데 다 못 옮긴 상태.
- 매직 넘버가 특히 많은 파일: `PGBattleshipActor.cpp`(실수 리터럴 285줄, 이름 붙인 상수 0), `PGFinaleDirector.cpp:577` `StateTimer > 20.0f` 같은 상태 시간 제한이 맨몸 숫자. 반대로 `PGPhysicsUtil.cpp:42-58` 은 콘솔 변수 8개로 튜닝값을 다 빼 두어 대비된다.

**면접 한 문장**: "상자는 '가방'이 뭔지 모르고 인터페이스 네 함수만 압니다. 그래서 팀 인벤토리 없이도 스모크 테스트 38개가 돌아갑니다."

### SRP — 한 클래스 한 책임

**잘 지킨 예**
- `UPGLockComponent`(76줄), `UPGSeatComponent / UPGFuelComponent`(`Public/Objects/PGVehicleComponents.h`), `UPGMissileSubsystem`(미사일 풀만), `UPGDebrisSubsystem`(잔해만). 각각 한 가지만 한다.

**어긴 예 1 — `AWarZoneFootprintPreview` (`Private/Actors/WarZoneFootprintPreview.cpp`, 7,074줄, 멤버 함수 60개)**
`Tick`(`:889-992`)이 한 프레임에 붕괴 연출, 에디터 미리보기 그리기, 맵 경계 되밀기, 시설 로드 로그, **검증 13종**(`:947-963`), 길찾기 막이 갱신, 검증 AI 이동을 순서대로 다 부른다. 가장 큰 함수: `BuildTileDesignPlacements` 977줄(`:4180-5157`), `SpawnRuntimeBlueprintTiles` 805줄(`:5379-6184`), `TryReserveFootprint` 593줄(`:994-1587`), `BuildLightweightWorldVisuals` 455줄(`:2366-2821`).

**쪼개는 제안** (이름은 예시, 옮기는 함수는 실제 이름):

| 새 클래스 | 종류 | 옮길 함수·데이터 |
|---|---|---|
| `UPGTileLayoutBuilder` | 순수 C++ 클래스(UObject 아님) | `TryReserveFootprint`, `ReserveFacility`, `BuildTileDesignPlacements`, `BuildShoreTransitionMap`, `GetSurfaceElevationForCell`, `GetFacilityAccessEdges`, `GetFootprintCenter`, `FacilityPlacements`·`TileDesignPlacements`·`LayoutHash`. 입력은 `TileByCell` 맵, 출력은 배치 배열. **테스트 가능**해진다 |
| `UPGTileVisualPacker` | ActorComponent | `SpawnRuntimeBlueprintTiles`, `BuildLightweightWorldVisuals`, `BuildElevatedFacilityTerrain`, `BuildBorderMountains`, `BuildDistanceFog`, `BuildMapBoundaryWall`, `ConfigureProxyMesh`, `Show*Proxy`, HISM 포인터 20여 개 |
| `UPGFacilityStreamer` | ActorComponent | `LoadFacilityDesignLevel`, `AreAllFacilityLevelsLoaded`, `VerifyDesignLevelSeparation`, `FacilityDesignLevelInstances`, `FacilityLoadRequestTimeSeconds`. 검사 2의 "거리별 언로드"도 여기로 |
| `UPGGameplayPointService` | WorldSubsystem | `BuildGameplayPointMarkers`, `ResolveGameplayPointSafety`, `RebuildGameplayPointHash`, `BroadcastLevelDesignPointsWhenReady`, `GetLevelDesignPoints`, `OnLevelDesignPointsBuilt`. 스포너·전투 스포너가 이걸 보게 되면 `AWarZoneFootprintPreview` 를 `Cast` 할 일이 없어진다 |
| `UPGNavigationDressing` | ActorComponent | `BuildPCGDressingGraph`, `VerifyPCGDressing`, `RefreshNavigationBlockerRegion`, `NavigationInvoker`, `*NavigationBlockers`, Tick 의 플레이어 셀 갱신 |
| `UPGMapVerifier` | ActorComponent, `#if !UE_BUILD_SHIPPING` | `Verify*` 13개, `StartSinglePlayerValidation`, `TryIssueSinglePlayerValidationMove`, `VerifySinglePlayerValidation`, `VerifyLocalPerformance`, `ValidationAICharacter`, `bLogged*` 15개, `Performance*`. **가장 먼저 뗄 것** — 코드를 안 바꾸고 옮기기만 하면 되고, 출하 빌드에서 빠진다 |
| `UPGRegionCollapse` | ActorComponent | `CollapseRegion`, `TickRegionCollapse`, `FinishRegionCollapse`, `FPGCollapseBatch`, `Collapse*` 필드 |
| `UPGMapBoundaryComponent` | 플레이어 폰에 붙임 | `EnforceMapBoundary`, `Boundary*` 필드. 맵이 아니라 폰의 책임이다 |
| 남는 `AWarZoneFootprintPreview` | Actor | `BeginPlay` 의 재시도 타이머, 위 컴포넌트를 순서대로 부르는 오케스트레이션, `DrawReservation`·`DrawDesignScalePreview` 를 `#if WITH_EDITOR` 로 |

**어긴 예 2 — `APGBattleshipActor` (`Private/Finale/PGBattleshipActor.cpp`, 2,338줄)**
`Tick`(`:2263-2302`)이 하위 Tick 9개(갑판 화물·선체 수납·착륙 장치·추락 받기·격납고 문·함교·좌석·조타·시야)를 부른다. `BuildInterior` 275줄(`:397-672`), `MeasureHull` 179줄(`:148-327`).

| 새 클래스 | 옮길 것 |
|---|---|
| `FPGShipHullBuilder` (순수 C++) | `BuildHull`, `MeasureHull`, `MeasureHullSliceLocal`, `FindHullPartLocal`, `BuildInterior`, `AddPart*`, `AddSolidBox`, `AddWalkBox`, `BuildInteriorLights`, `LogHullInventory`, `LogPartsOutsideHull` |
| `UPGHangarDoorComponent` | `BuildHangarDoor`, `TickHangarDoor`, `OpenHangar/CloseHangar`, `CountPawnsInHangar`, `HangarDoor*` 필드 14개 |
| `UPGShipHelmComponent` | `TickSeat`, `TickHelm`, `TickHelmDrive`, `BuildBridge`, `TickBridge`, `TickBridgeView`, `PlaceOrbitCamera`, `ApplyBridgeViewCamera`, `DrawCrosshair`, `View*`·`Helm*` 필드 |
| `UPGShipWeaponsComponent` | `BuildCannons`, `FireCannon`, `FireMissile`, `Explode`, `Cannon*`·`Missile*`·`Muzzle*` |
| `UPGDeckCargoComponent` | `HoldPhysicsPawns`, `ReleaseHeldPawns`, `TickDeckCargo`, `TickCatchFallers`, `SpawnDeckVehicle`, `DeckCargo` |
| `UPGShipRetractComponent` | `TickHullRetract`, `TickLandingGear`, `BuildSternApron`, `DeployElevator/RetractElevator`, `RetractParts`·`LandingGear`·`SternAprons` |
| 남는 `APGBattleshipActor` | `FlyInFrom`, `CruiseTo`, `TickCruise`, 호버 흔들림, `Health/TakeDamage`, 델리게이트 |

**어긴 예 3 — `UPGObjectSettings`** (`Public/Objects/PGObjectSpawnerSubsystem.h`): 프로젝트 설정 "ProjectPG Objects" 인데 카테고리 집계가 Combat 14 + Combat|Faction 13 + Finale 1 + 차량·로봇 설정까지 들어 있다. `UPGCombatSettings` 로 나누면 이름과 내용이 맞는다.

**면접 한 문장**: "맵 클래스는 프로토타입 속도를 위해 한 파일에 쌓았고, 지금은 책임 7개가 보입니다. 검증부터 떼는 순서까지 정해 두었습니다."

### OCP — 수정 없이 확장

**잘 지킨 예**
- 카탈로그 행 `FPGObjectCatalogRow`(`Public/Objects/PGObjectTypes.h:197` `TSoftClassPtr<AActor> ActorClass`) — `Private/Objects/PGObjectSpawnerSubsystem.cpp:169-171` 이 행에 클래스가 적혀 있으면 그걸 쓰고, 없을 때만 원형 기본 클래스를 고른다. **코드를 안 고치고** 새 오브젝트(블루프린트 자식 포함)를 DataTable 한 줄로 추가할 수 있다.
- 콘솔 변수 14개(`PGPhysicsUtil.cpp:42-58`, `PGDebrisSubsystem.cpp:23-28`, `PGTankPawn.cpp:29`) — 빌드 없이 튜닝.

**어긴 예**
- `Private/Objects/PGObjectSpawnerSubsystem.cpp:149-161` `GetDefaultClassForArchetype` 의 `switch(Archetype)` — 원형이 하나 늘면 enum(`PGObjectTypes.h:18`)과 이 switch 두 곳을 고쳐야 한다. `TMap<EPGObjectArchetype, TSubclassOf<AActor>>` 를 `UPGObjectSettings` 에 두면 닫힌다.
- `Private/Actors/WarZoneFootprintPreview.cpp:2178, 5096, 5827` — `switch (Placement.Visual)` 가 **세 곳**. 타일 종류(`ETileDesignVisual`)를 하나 추가하면 세 switch 를 다 찾아 고쳐야 한다. 시설 쪽도 `:191, 6664` 두 곳. 타일 종류별 설정을 구조체 표(`TMap<ETileDesignVisual, FTileVisualDesc>`) 하나로 모으면 한 곳만 고친다.
- `Private/Finale/PGFinaleDirector.cpp:86-151`(`EnterState` switch) + `:527-605`(Tick 의 `if (State == …) else if …` 사슬) — 상태 하나 추가 = 두 곳 수정. 상태가 7개뿐이라 지금은 괜찮지만, 면접에서 "상태 패턴으로 바꿀 수 있다"고 말할 수 있어야 한다.
- `Private/Objects/PGDoorActor.cpp:166` `switch (Motion)` — 문 움직임 5종. 종류가 적고 한 곳뿐이라 허용.

**면접 한 문장**: "오브젝트 152개는 클래스를 안 늘리고 카탈로그 행으로 추가합니다. 반면 타일 종류는 switch 세 곳을 고쳐야 해서, 표 하나로 모으는 게 다음 일입니다."

### LSP — 자식이 부모 자리를 대신해도 안 깨짐

**잘 지킨 예**
- 원형 9개 전부 부모의 `Interact_Implementation` 흐름(`PGInteractableActorBase.cpp:50-65`)을 덮어쓰지 않고 훅 세 개만 채운다. 스모크 테스트(`Private/Objects/PGObjectSmokeTest.cpp:380-388`)가 `IInteractable::Execute_*` 로 9종을 같은 방식으로 돌려 38/38 통과 — 치환 가능성의 실증이다.
- `IPGItemReceiver` 구현 3종(팀 캐릭터·검증 캐릭터·테스트 폰)이 서로 바꿔 끼워져도 오브젝트 코드는 모른다.

**어긴(애매한) 예**
- `Public/Robot/PGRobotCharacter.h:23` `APGRobotCharacter : public APGMonsterCharacter` — 탈 수 있는 로봇이 "몬스터"다. `bRideable`(`:71`)에 따라 `CanInteract`(`Private/Robot/PGRobotCharacter.cpp:162`)·`GetKnockRatio`·`CanKnockProps`(`PGRobotCharacter.h` protected 오버라이드)가 뒤집히고, 전투 스포너(`Private/Combat/PGCombatSpawner.cpp:204,227`)가 로봇을 따로 다룬다. 부모(몬스터 AI)가 "이건 적"이라고 가정하는 자리에 "플레이어 탈것"이 들어가는 셈이다. 실용적 선택(몬스터의 체력·죽음·넉백을 재활용)이지만, "보스 로봇"과 "탈것 로봇"을 두 클래스로 나누는 게 원칙에 맞다.
- `Public/Objects/PGTransformNPCActor.h:21` — 변신 NPC 가 `APGInteractableActorBase` 를 상속하면서 부모의 `MeshComponent`(정적 메시)를 `NoCollision` 으로 꺼 두고(`Private/Objects/PGTransformNPCActor.cpp:81`) 스켈레탈 메시 3개를 따로 단다(`:61-73`). 부모의 `ApplySoftMesh` 경로가 의미 없어진다. 깨지진 않지만 "안 쓰는 부모 부품"이 남는다.

**면접 한 문장**: "상자 9종은 어느 것을 넣어도 상호작용 컴포넌트가 똑같이 돕니다. 로봇을 몬스터의 자식으로 둔 건 시간 때문이고, 원칙대로면 보스와 탈것을 분리해야 한다는 걸 압니다."

### ISP — 안 쓰는 인터페이스 강요 금지

**잘 지킨 예 (이 프로젝트에서 가장 잘 된 원칙)**
- `Public/Interaction/Interactable.h:26-33` 3함수, `Public/Interaction/PGRideable.h:24` **1함수**, `Public/Objects/PGDeviceSignalInterface.h:20-21` 1함수, `Public/Objects/PGItemReceiverInterface.h:26-40` 4함수. 전부 작다.
- `PGRideable.h:3-5` 주석이 분리 이유를 정확히 적었다: "탄 사람 문구를 `GetInteractionPrompt` 에 섞으면 남이 탄 차를 본 다른 플레이어에게도 '하차'가 떴다." — 인터페이스를 쪼갠 실제 버그 사례라 면접 답으로 그대로 쓸 수 있다.
- 문·상자는 `IPGDeviceSignalTarget` 을 구현하지만(`PGDoorActor.h:21`) 바닥 아이템·탈출구는 구현하지 않는다(`PGFloorItemActor.h:16`, `PGExtractionZoneActor.h:27`). 필요한 것만 붙였다.

**어긴 예**
- 뚜렷한 위반 없음. 굳이 꼽으면 `Public/Objects/PGDestructibleActor.h:21` — 파괴물이 `IInteractable` 을 상속받지만 `CanInteractInternal`(`Private/Objects/PGDestructibleActor.cpp`)이 "도구 필요" 모드일 때만 true 고 나머지는 항상 false 다. 대부분의 파괴물엔 "상호작용" 인터페이스가 죽은 채로 붙어 있다. 큰 문제는 아니다.

**면접 한 문장**: "탈것은 '밖에서 보는 사람'과 '탄 사람' 문구를 인터페이스 두 개로 나눴습니다. 합쳐 뒀더니 남의 차에 '하차'가 떠서 나눈 겁니다."

### DIP — 구체 대신 추상에 의존

**잘 지킨 예**
- 오브젝트 → 인벤토리: `IPGItemReceiver` 경계(위). 오브젝트 소스 어디에도 `InventoryComponent.h` 포함이 없다.
- 오브젝트 → 스포너: 스포너는 `TSubclassOf<AActor>` 와 `Cast<APGInteractableActorBase>`(`PGObjectSpawnerSubsystem.cpp:184`) 로 **부모**만 안다. 자식 캐스트는 `AItemContainerActor`(시드 주입)·`APGFloorItemActor`(색 변형) 두 곳뿐(`:187, 190`).

**어긴 예**
- `Private/Common/PGPhysicsUtil.cpp:513` — 범용 물리 유틸이 `IsA<AWarZoneFootprintPreview>()` 로 맵 클래스를 안다. `:481` 은 `Cast<APGDoorActor>` 로 문을 특별 취급. 유틸이 상위 게임 클래스를 포함하는 방향 역전이다. 태그(`PGTerrain`·`PGProtected`, `PGPhysicsUtil.h`)가 이미 있으니 맵 액터에도 태그를 붙이고 `IsA` 를 지우면 된다.
- `Private/Finale/PGFinaleDirector.cpp:344, 394` — 피날레가 `Cast<AWarZoneFootprintPreview>` 로 맵 크기·산 반경을 묻는다. 위 SRP 제안의 `UPGGameplayPointService`(또는 `IPGMapExtentProvider` 인터페이스)를 보게 하면 끊어진다.
- `Public/Vehicle/PGTankPawn.h:180` `RamMonster(APGMonsterCharacter*)`, `Private/Vehicle/PGTankPawn.cpp:448`·`PGVehiclePawn.cpp:333`·`PGFlightKitComponent.cpp` — 사용자 탈것이 **팀원 클래스** `APGMonsterCharacter` 를 직접 캐스트한다. 팀 합치기 때 몬스터 클래스가 바뀌면 탈것 4파일이 같이 깨진다. `TakeDamage`(엔진 가상 함수)만 쓰고, 체력·반지름은 `IPGRammable` 인터페이스로 묻는 게 맞다.
- 하드코딩 에셋 경로: `WarZoneFootprintPreview.cpp` 80건, `TacticalTileActor.cpp` 46건(`:34-36` `ConstructorHelpers::FObjectFinder` — 에셋을 옮기면 **CDO 생성 시점에 크래시**), `ProceduralFacilityActor.cpp` 33건. 반면 `Public/Objects/PGTransformNPCActor.h` 는 메시·애니 9개를 전부 `UPROPERTY(EditAnywhere) TSoftObjectPtr` 로 노출하고 코드는 기본값만 준다 — 이게 목표 형태다. 같은 사람이 두 방식을 다 썼으니 "왜 타일 쪽은 못 옮겼나"에 답을 준비할 것(답: 타일은 900개 인스턴스 패킹이라 CDO 시점 로드가 편했다, 이제 `UPGTileVisualSettings` DataAsset 하나로 모으면 된다).

**면접 한 문장**: "상자는 인벤토리를 인터페이스로만 알아서 팀 인벤토리와 테스트 폰이 그대로 바꿔 끼워집니다. 물리 유틸이 맵 클래스를 아는 건 반대 방향이라 태그로 바꿀 계획입니다."

---

## 검사 2 — 오픈월드 최적화: `OptimizationBacklog_2026-09-21.md` 에 **없는 것만**

백로그의 "이미 되어 있는 것"과 "남은 것"(탱크 틱·몬스터 틱 등급·`DropSupportedItems`·시설 언로드·비동기 프리로드·잔챙이 5개)은 뺐다. 목표: RTX 4060 8GB / 60fps.

| # | 무엇 | 이 프로젝트 어디 | 얻는 것 | 작업량 | 면접 값어치 |
|---|---|---|---|---|---|
| 1 | **설정 파일이 비어 있다**: `Config/*.ini` 에 `gc.*`·`s.AsyncLoadingThreadEnabled`·`r.PSOPrecache*`·`r.ShaderPipelineCache*`·`net.*`·`[SystemSettings]` 전부 **0건**(전체 grep). `DefaultEngine.ini:10-33` 렌더 블록과 `:119-123` `[ConsoleVariables]`(텍스처 풀만)이 전부다. `DefaultGame.ini` 15줄, 패키징 섹션 없음 | `Config/DefaultEngine.ini`, `DefaultGame.ini` | (a) `gc.TimeBetweenPurgingPendingKillObjects`(기본 60초)·`gc.NumRetriesBeforeForcingGC` — 런타임 HISM 900개 + 시설 레벨 파괴가 한 번에 정리되는 스파이크 완화. (b) `bShareMaterialShaderCode=True` + PSO 캐시 번들 — 런타임 패킹 HISM 은 재질별로 첫 프레임에 PSO 를 컴파일한다(`WarZoneFootprintPreview.cpp:5649-5663`) → "첫 등장 끊김"의 원인. 둘 다 `PG.PerfSweep` 로 전후 측정 가능 | 반나절(ini 4~6줄 + 측정) | **상** — "왜 ini 를 손댔나"를 수치로 답할 수 있다 |
| 2 | **탈것이 네트워크 거리 컬링·휴면 없음**: `PGTankPawn.cpp:35`·`PGVehiclePawn.cpp:70` 은 `bReplicates = true` 만 있고 `NetCullDistanceSquared`·`SetNetDormancy` 없음. 전함(`PGBattleshipActor.cpp:70`, 5km)·드래곤(`PGDragonBoss.cpp:88`, 3km)은 일부러 늘렸다. `SetNetDormancy`·`bOnlyRelevantToOwner`·`ReplicationGraph` 프로젝트 전체 0건 | `PGTankPawn.cpp:33-37`, `PGVehiclePawn.cpp:68-72`, `PGBoothActor.cpp:49`, `PGInteractableActorBase.cpp:17-19` | 600m 맵에 세워 둔 차·탱크·오브젝트 수백 개가 모든 클라이언트의 관련성 집합에 남는다. 세워 둔 탈것·안 만진 상자에 `DORM_Initial`, 탈것에 `NetCullDistanceSquared` 1.5km → 서버 복제 비용 감소 | 반나절 | **중상** — 멀티 계약 문서(`ProceduralLevelServerHandoff.md`)와 이어지는 이야기 |
| 3 | **스켈레탈 애니 최적화가 몬스터에만**: `VisibilityBasedAnimTickOption`·`bEnableUpdateRateOptimizations` 는 팀원 코드 `Monster/PGMonsterCharacter.cpp:51-52` 딱 한 곳. 사용자 클래스 `PGRobotCharacter.cpp`(없음), `PGDragonBoss.cpp:87`(`bComponentUseFixedSkelBounds` 만), `PGFlightKitComponent.cpp:81`, `PGTransformNPCActor.cpp:78` 은 안 보일 때도 포즈를 계산한다 | 위 4파일 생성자 | 드래곤(스케일 25, 2.5초 선회)이 화면 밖에서도 뼈대를 푼다. 두 줄 복사로 끝 | 1시간 | 중 — 작지만 "왜 여긴 안 했나"를 안 물어보게 한다 |
| 4 | **`TG_PostPhysics` 미사용**: `TickGroup` 설정은 `PGWeaponComponent.cpp:25` 한 곳뿐. 전함 `TickCatchFallers`(`PGBattleshipActor.cpp:1764`)·`TickDeckCargo`(`:1706`)는 물리 폰을 잡는데 기본 `TG_PrePhysics` 라 **지난 프레임** 위치를 읽는다 | `PGBattleshipActor.cpp:63` 생성자 | 갑판 화물이 한 프레임 늦게 따라오는 떨림 제거. 성능이 아니라 정확도 | 1시간 | 중 — "틱 그룹을 아느냐"에 답이 된다 |
| 5 | **Significance Manager 미사용** (`grep Significance` 0건, Build.cs 모듈 없음) | 백로그 1·2번(탱크·몬스터 틱 등급)을 각 클래스에 손으로 넣는 대신 `USignificanceManager` 등록 한 곳(`PGObjectSpawnerSubsystem` 스폰 직후)에서 탱크·차·로봇·드래곤 틱 간격을 거리로 한꺼번에 정한다 | 백로그 1·2 를 **더 적은 코드**로 해결 + 엔진 표준 이름 | 하루(백로그 1·2 포함) | **상** — 업계 표준 용어를 쓸 수 있다. 단, 백로그 1의 "탱크 첫 틱 중력" 주의는 그대로 적용 |
| 6 | **인스턴스 커스텀 데이터 미사용**: `SetCustomDataValue`·`NumCustomDataFloats` 0건. 패킹 키가 `메시\|충돌\|그림자\|컬거리\|재질경로×슬롯`(`WarZoneFootprintPreview.cpp:5623-5635`)이라 재질이 다르면 HISM 이 갈린다. 지면 3종은 아예 HISM 이 따로다(`:589-593` Ground/WarZoneGround/TransitionGround) | `:5484` 가 이미 슬래브 4종을 `UnifiedGround` 재질 하나로 합치고 있다 — 같은 방법을 지면 3종에 적용하고 색·거칠기 차이는 커스텀 float 1개로 | 청크당 지면 배치 3 → 1(드로우콜). 단 `NumCustomDataFloats>0` 은 900 타일 × 인스턴스 버퍼 비용 — 측정 후 결정 | 하루 | 중 — 마스터 재질을 만들어야 해서 프로그래머 설명 거리는 짧다 |
| 7 | **시설 레벨 로드 우선순위 없음**: 로드 호출은 `WarZoneFootprintPreview.cpp:6279` 한 곳, `SetPriority`·`bShouldBlockOnLoad` 0건. `s.LevelStreamingComponentsRegistrationGranularity`·`s.LevelStreamingActorsUpdateTimeLimit` ini 0건 | `:6279` 직후 `SetPriority()`(6×6 다운타운 먼저), ini 2줄 | 6×6 다운타운이 1×2 검문소와 같은 순위로 경쟁. 등록 시간 제한을 올리면 시설 등장 때 프레임이 갈라지는 대신 몇 프레임 더 걸린다 — 트레이드오프를 말할 수 있다 | 2시간 | 중 — 백로그 4번(거리 언로드)과 묶어 "스트리밍 정책"으로 말하면 상 |
| 8 | **비동기 트레이스 0건** (`AsyncLineTrace*`·`AsyncSweep*`·`FTraceDelegate` 전무). 다만 가장 의심됐던 `PGInteractionComponent.cpp:140,156,171`(스윕 1 + 오버랩 2)은 **`:33-36` 0.1초 간격으로 이미 묶여 있다** — 프레임당이 아니다. 실제 매 프레임 후보: `PGFlightKitComponent.cpp:594`(구 오버랩)·`:674`(스윕), 컴포넌트 틱 매 프레임(`:32`); `PGFinaleDirector.cpp:220` 반경 90m 오버랩 | `PGFlightKitComponent.cpp:594,674` | 비행 중 매 프레임 두 쿼리를 `AsyncOverlapByChannel` 로. 얻는 건 작다(쿼리 수가 적다) | 반나절 | 하 — 수치가 안 나올 가능성이 커서 "검토했고 안 했다"로 |
| 9 | **새로 찾은 전체 순회 2건**(백로그 `:1906`·`:196` 과 다른 것): (a) `PGTransformNPCActor.cpp:153` 8초 반복 타이머 → `:234` `GetAllActorsOfClass(APGFloorItemActor)` — 연료를 안 주면 판 내내 8초마다 바닥 아이템 전체 순회. (b) `PGBattleshipActor.cpp:1774` `TActorIterator<APawn>` 초당 4회(`:1768-1771`) | (a) 스포너의 `SpawnedActors` 목록에서 찾기 / (b) `GetPlayerControllerIterator`(`:1923` 에서 이미 쓰는 방식) | 둘 다 작지만 백로그 3번(격자 해시)과 같은 패턴이라 한 문장으로 묶인다 | 1시간 | 하 |
| 10 | **`Verify*` 두 함수의 조기 종료 순서**: `WarZoneFootprintPreview.cpp:2866-2872`·`:3037-3043` 이 `bLogged*` 검사 **전에** `FacilityPlacements` 를 순회한다. 검증이 끝난 뒤에도 0.1초마다 배열 두 번 훑음 | 두 함수 첫 줄로 `bLogged*` 검사 이동 | 미미. 하지만 SRP 제안의 `UPGMapVerifier` 로 옮길 때 같이 정리 | 10분 | 하 |
| 11 | **Animation Budget Allocator** 미사용 (플러그인 꺼짐) | 없음 — 화면에 스켈레탈 30개 이상일 때만 의미 | "검토했고 규모가 안 돼서 안 했다" | 0 | 하 (질문 대비용) |
| 12 | **가려짐 최적화 미사용**: `bUseAsOccluder`·`bAffectDistanceFieldLighting`·`MinDrawDistance` 0건. `r.GenerateMeshDistanceFields=True`(`DefaultEngine.ini:13`)라 900 타일 소품 전부가 거리장 씬에 들어간다 | `:5649` 패킹 HISM 생성 직후 작은 소품에 `bAffectDistanceFieldLighting=false`, 큰 시설 메시에 `bUseAsOccluder=true` | Lumen/DFAO 가 읽는 전역 거리장 축소. 4060 에서 GI 비용의 일부 | 2시간 + 측정 | 중 |

**우선순위 제안(면접 값어치 × 작업량)**: 1(ini) → 5(Significance, 백로그 1·2 흡수) → 2(네트 휴면) → 3(애니 두 줄) → 7(스트리밍 우선순위).

---

## 면접 전에 고치면 좋은 것 3개 — 구체 순서

1. **`UPGRideComponent` 추출** (반나절, 위험 낮음)
   `Mount/Dismount/RiderPawn/bDismountKeyWasDown/MountedTime/DismountPromptTimer` 를 컴포넌트로. 탈것마다 다른 건 (a) 탑승자를 붙일 컴포넌트(`GetMesh()`/`Body`/`GetRootComponent()`), (b) 탑승 후 카메라 피치(-10/-8/없음), (c) 탑승 후 후처리(`SetParked(false)`, `MaxWalkSpeed`) — 세 개를 델리게이트나 가상 함수 훅으로 넘기면 된다. 검증: PIE 에서 차·탱크·로봇 각각 타고 내리기, `PG.ObjectSmokeTest` 0 failures 유지.

2. **`UPGMapVerifier` 분리** (하루, 위험 낮음)
   `Verify*` 13개와 `bLogged*` 플래그를 그대로 옮긴다. 원본 클래스 멤버를 읽어야 하니 `friend` 대신 필요한 getter 5~6개(`GetTileDesignPlacements()` 등)를 판다. 성공 기준: 같은 시드(`-PGMapSeed`)로 검증 로그가 이전과 동일.

3. **ini 최적화 4~6줄 + `PG.PerfSweep` 전후 수치** (반나절)
   위 표 1번. 수치 하나(예: "첫 30초 최대 프레임 시간 X→Y ms")가 있으면 면접에서 코드 설명보다 강하다.

---

## 확인 못 한 것(미확인)

- 실제 PIE 프레임 수치. 이 문서는 정적 검사만이고, 검사 2의 "얻는 것"은 코드 근거에 따른 예상이다. 4060 실측은 백로그 문서대로 아직 없다.
- `Config.zip` 은 열지 않았다(규칙).
- `Content/` 의 블루프린트·머티리얼 내부(나나이트 켜짐 여부, 재질 슬롯 수)는 보지 않았다 — 표 6번의 "청크당 3→1" 은 코드상 HISM 선언 수 기준이다.

---

## 9/28 추가 — 그 뒤에 한 것과 아직 약한 곳

**한 것**
- 9/26: 맵 액터에서 검사·붕괴·꾸미기를 협력 객체(`UPGMapVerifier`·`UPGRegionCollapse`·`UPGMapVisualBuilder`)로, 배·드래곤도 같은 방식으로 떼어 냈다. 로봇 보스/탑승은 역할 객체(`UPGRobotRole`)로(개방-폐쇄).
- 9/28: 가장 긴 함수 네 개를 이름 붙은 단계 함수로 나눴다. 동작은 그대로 두고 같은 시드 두 개의 로그를 나누기 전·후 비교로 확인했다.

| 함수 | 전 | 후 | 단계 |
|---|---|---|---|
| `SpawnRuntimeBlueprintTiles` | 814줄 | 223줄 | 도우미 `PGTileSpawnSteps`(워존 타일 고르기 등), 액터 묶기·호숫가 칸·코드 시설 멤버 |
| `TryReserveFootprint` | 596줄 | 약 70줄 | 격자 읽기 → 워존 시설 → 출구 검문소 → 테마 구역 → 짓기 |
| `BuildGameplayPointMarkers` | 468줄 | 약 40줄 | 타일 칸 → 추가 시작 지역 → 가장자리 출구 → 시설 안 |
| `BuildLightweightWorldVisuals` | 446줄 | 약 120줄 | 칸 땅판(`FPGGroundBatches`) → 시설 땅판 → 호숫가 돌·갈대 → 호숫가 메시 |

- 나누다 찾은 것: 워존 388칸 중 12칸의 타일 종류가 달라졌다. 식은 글자 그대로 같고, 파이썬으로 같은 식을 계산하면 나눈 뒤 값이 맞았다 — 원래 빌드가 묶음 안 칸 번호를 식과 다르게 계산하고 있었다(원인 미확정, 컴파일러 최적화 추정).

**아직 약한 곳 → 같은 날 해결(아래 "9/28 오후")**
1. **맵 짓는 클래스(`AWarZoneFootprintPreview`)가 여전히 많은 일을 한다(단일 책임).** 파일은 책임별로 나눴지만 한 클래스다. 나눈다면 "설계도 만들기(레이아웃)" 와 "세우기(타일·시설 스폰)" 를 각각 협력 객체로 빼고, 맵 액터는 순서만 부르게 한다.
2. **소품 부수기(`PGPhysicsUtil::TryKnockProp`)가 "이 종류면 이렇게" 를 한 함수에 모은다(개방-폐쇄).** 부술 수 있는 종류(묶음 인스턴스·통째 부서지는 문·레벨 메시·상자)가 늘면 이 함수를 고쳐야 한다. 나눈다면 종류마다 "부수기 처리기" 를 등록하고 함수는 맞는 처리기를 찾아 부르게 한다. 멀티 부수기 흐름(먼저 부수기·서버 알림 묶음·잔해 예산)과 붙어 있어 시연 전에는 바꾸지 않았다.

### 9/28 오후 — 위 두 가지 해결

| 무엇 | 어떻게 | 같은지 확인 |
|---|---|---|
| 맵 클래스 한 책임 | 세우기(타일·코드 시설 스폰, 시설 레벨, 둔덕, 길찾기 막이 — 8함수)를 `UPGMapTileSpawner`, 게임 지점(9함수)을 `UPGGameplayPointBuilder` 로. 맵 액터는 설계도 만들기와 부르는 순서만. 9/26 협력 객체와 같은 방식(레벨에 저장되는 칸·상태는 액터에 두고 `Map->`) | 시드 12345·1790045299 로그 비교 다른 줄 0, 서버·접속자 `layout_hash` 같음, 시작 자리·방향 같음, 스모크 64/0 |
| 부수기 개방-폐쇄 | `TryKnockProp` 은 공통 검사 뒤 처리기 표(`KnockHandlers`: 이미 잔해 / 묶음 인스턴스 / 통째로 부서지는 문 / 상자 / 레벨 메시)를 순서대로 묻는다. 새 종류 = 처리기 하나 + 표 한 줄 | 중앙 건물 부수기 시험: 서버가 부순 순서·자리 1~140번째 같음, 접속자 140/137 같음, 잔해 151 같음. 상자 밀림 시험, 스모크 64/0 |

지금 맵 액터의 협력 객체: 검사(`UPGMapVerifier`) · 붕괴(`UPGRegionCollapse`) · 꾸미기(`UPGMapVisualBuilder`) · 세우기(`UPGMapTileSpawner`) · 게임 지점(`UPGGameplayPointBuilder`).

### 9/28 저녁 — 빠뜨렸던 긴 함수 바로잡기

- 오전에 "가장 긴 함수 넷" 이라고 적었는데 **틀렸다.** 줄 수를 재던 도구가 여러 줄에 걸친 함수 머리를 못 읽어서, 그보다 긴
  `BuildTileDesignPlacements`(1,017줄, 칸마다 타일 고르기)를 빠뜨렸다. 함수 경계를 중괄호로 세는 도구로 다시 쟀다.
- 나눈 것(동작 그대로):

| 함수 | 전 | 후 | 방법 |
|---|---|---|---|
| `BuildTileDesignPlacements` | 1,017줄 | 12줄 | 단계 구조체 `FPGLayoutPlanner`(Layout.cpp 안, 헤더에는 friend 한 줄): 시설 칸 예약 → 지형 덩어리 → 맵 범위 → 시설 진입로 → 호수·추가 시작 지역 → 시작/출구 길 잇기 → 워존 길 보장 → 칸마다 타일(길·시작·자연은 `Apply*Visual`) → 해시·검사 → 추가 시작 지역 겉모습 → 요약. 세 곳이 똑같이 쓰던 "길 거슬러 올라가 잇기" 는 `ConnectPathBack` 하나로 |
| `UPGRegionCollapse::CollapseRegion` | 404줄 | 약 106줄 | 무너질 칸 고르기(`SelectCollapseCells`) / 떨어질 조각 모으기(`GatherCollapseBatches`) / 그 위 것 치우기(`ClearCollapsedContents`) |

- 확인: 같은 시드 둘(12345, 1790579599)의 설계도 로그를 나누기 전·후 비교 — 49줄·41줄 같음, layout_hash 035300A1·AC96BD2D 그대로,
  전용 서버 + 접속자 2명 layout_hash 같음. 붕괴는 같은 시드·같은 자리 — 칸 93·벽 44·조각 8,447·묶음 94 가 서버·접속자 모두 나누기 전과 같음. 스모크 64/0.
- 지금 가장 긴 함수는 301줄(`APGExitDressingActor::SpawnCheckpoint`, 검문소 꾸미기 — 소품을 차례로 놓는 목록)이고, 200줄 넘는 것은 모두
  "무엇을 어디에 놓나" 를 차례로 적은 조립 함수다(배 내부·타일 재구성·드래곤 상태 전환 등). 분기가 얽힌 판단 함수는 아니다.
