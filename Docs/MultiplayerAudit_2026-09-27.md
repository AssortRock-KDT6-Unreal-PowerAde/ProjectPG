# 멀티(전용 서버) 전체 점검 결과 — 2026-09-27

전용 서버 1 + 클라이언트 2~4 기준으로 우리 코드 전체를 네 구역(물건·상호작용 / 탈것·로봇 / 피날레·전투 / 화면 흐름·맵)으로 나눠 읽어서 찾은 문제. **코드를 읽어 확인한 것**이고, 표시(✔)한 것은 고쳤고 전용 서버 + 클라이언트로 확인했다 — 확인 방법은 아래 "확인한 방법". [~] 는 고쳐서 빌드했지만 아직 멀티로 확인 안 한 것. 표시 없는 것은 아직 안 됐다. 경로는 `Source/ProjectPG/` 기준.

한 줄 결론: **맵·시작 지역·캐릭터 배치까지는 된다. 그 위의 게임(물건·탈것·몬스터 모양·탈출·판 끝·피날레)은 대부분 혼자 하는 판 기준으로 짜여 있어 멀티에서 안 된다.**

공통 원인 다섯 가지 — 고칠 때 이 틀로 본다.

1. **서버에서만 한 겉모습**: 메시·재질·애니·이펙트·컴포넌트를 서버에서만 바꿈 → 클라는 기본 모습. 해결: "무엇으로 보여라" 값을 복제(`ReplicatedUsing` + OnRep 에서 같은 함수)하거나 NetMulticast.
2. **복제 안 되는 상태값**: 안내 문구·판정에 쓰는 값(`Kind`, `bRideable`, `InteractSeconds` …)이 서버에만 있음. 해결: `Replicated`(대부분 `COND_InitialOnly`).
3. **서버 화면에 띄우는 안내**: `UPGAnnounceSubsystem`·로딩 화면을 서버에서 부름 → 전용 서버엔 화면이 없다. 해결: 그 사람 컨트롤러로 Client RPC, 또는 복제 상태의 OnRep 에서 각자 띄움.
4. **"첫 번째 플레이어" 가정**: `GetFirstPlayerController()` — 전용 서버에서는 먼저 들어온 한 사람만. 해결: 모든 PlayerController, 또는 부른 사람 기준.
5. **입력·카메라를 서버에서 읽음**: 서버는 클라 키를 모른다. 해결: 조종하는 클라가 읽고 Server RPC 로 보냄, 카메라는 그 클라에서.

---

## A. 게임이 안 굴러가는 것 (먼저 고칠 것)

### A1. 판 끝·기록
- [✔] **판이 끝나면 서버가 맵을 바꿔 모두 튕긴다** — `Flow/PGRunSubsystem.cpp:341-348` → `OpenLevel`(:214). 누가 탈출·사망·시간 끝 → 3초 뒤 서버가 점수판 맵으로 가며 접속이 끊기고, 클라는 기본 맵에서 **혼자 하는 판**을 새로 시작한다. 고침: 멀티에서는 서버가 `OpenLevel` 하지 않는다. 사람별로 끝내고 그 사람에게 Client RPC(결과 표시 + 자기 컴퓨터에서 이동).
- [✔] **판 기록·창고가 서버 하나에 첫 사람 것만** — `GetRunPlayerController`=`GetFirstPlayerController`(:392-396), 아이템은 0번 사람 가방(:477-502), 저장은 서버 디스크(:580-593). 누가 탈출하든 판 전체가 끝남(:600), 0번 사람 죽음만 봄(:430-449), 0번이 나가면 "사망"으로 모두 튕김(:467-473). 고침: 기록을 사람별(PlayerState 또는 PC 키)로, 끝날 때 그 사람 컴퓨터로 보내 거기서 창고·통계 저장.
- [✔] 로비 "출격" 버튼이 서버에 접속하지 않고 혼자 하는 판을 연다 — `UI/PGFlowWidgets.cpp:946` → `StartGame`. 서버 접속은 형님 매칭(`Server/MatchmakingSubSystem.cpp:57-60`)뿐. **형님과 맞출 것.** → 9/28 우리 코드(`UPGRunSubsystem::StartGame`)만 고침: `-PGServer=주소` 면 바로 접속, 로그인돼 있으면 형님 매칭 `RequestGameStart`, 아니면 혼자. 확인: 로비 → 출격 → 전용 서버 접속 → 시작 지역(①만. 매칭 길은 웹 서버가 있어야 해서 형님 확인 대기).

### A2. 물건이 클라에서 안 보이고 못 씀
- [✔] **표(카탈로그)로 만든 물건의 겉모습·설정이 서버에만** — `Objects/PGInteractableActorBase.cpp:108-126`(`ApplyCatalogRow`), 문 `PGDoorActor.cpp:56-71`, 상자 뚜껑 `Actors/ItemContainerActor.cpp:84-90`, 출구 `PGExtractionZoneActor.cpp:124-145`, 부서지는 것 `PGDestructibleActor.cpp:24-32`. 호출은 서버 전용(`PGObjectSpawnerSubsystem.cpp:220,238-255`, `PGWorldLootSpawner.cpp:1047-1053`). 클라: 상자·문·잠긴 철문·차/헬기 출구·퀘스트 물건이 **안 보이고 F 로 못 잡는다**, 서버에는 충돌이 있어 보이지 않는 벽. `DisplayName/InteractSeconds/ObjectId/QuestTag` 도 복제 안 됨. 고침: 적용한 줄(행)을 복제(`COND_InitialOnly`)하고 OnRep 에서 겉모습 부분을 다시 적용. 잠금 상태는 복제값을 덮지 않게.
- [✔] 부스 종류(`PGBoothActor.h:59` `Kind`) 복제 안 됨 → 클라는 네 부스 모두 "상점" 모양, 벽 위치가 달라 되돌림. `Replicated`.
- [✔] 서비스 종류·사용 자리(`PGServiceInteractionActor.h:68`, `.cpp:29-35`) 복제 안 됨.
- [✔] 바닥 아이템 이름·메시(`PGFloorItemActor.cpp:52-58, 67-70`) — 클라는 전부 "아이템 줍기", 표에서 온 메시가 없다.
- [✔] 문·잠금·출구·부서지는 것·퀘스트·장치의 설정값 복제 안 됨(`PGDoorActor.h:85` Motion, `PGLockComponent.h:59`, `PGExtractionZoneActor.h:117-127`, `PGDestructibleActor.h:62-81`, `PGQuestObjectActor.h:50-65`, `PGDeviceActor.h:78-87`).
- [✔] **시설 안 문이 클라에서 유령 벽** — `Objects/PGLevelDoorConverter.cpp:46-55`: 서버가 레벨의 문 메시를 지우지만 복제 안 되는 액터라 클라에는 그대로 남음. 고침: 클라도 같은 규칙으로 원래 문 메시를 숨기고 충돌 끔.
- [✔] 누르고 있기(유지 시간) 복제·검사 안 됨 — 금고(5초)가 클라에선 바로 열림(`PGInteractableActorBase.h:104`, `PGInteractionComponent.cpp:329-336, 383-415`). 서버가 시작 시각을 재서 검사.
- [✔] "사용 중" 잠금이 풀리지 않을 수 있음 — 거절·거리 초과 경로에서 `EndUse` 빠짐(`PGInteractionComponent.cpp:404-408, 428-432`). 부스는 한 번 쓰면 판 끝까지 잠김(`PGServiceInteractionActor.cpp:96-110`).
- [✔] 서버 거리 검사가 물체 가운데 기준 450cm(`PGInteractionComponent.cpp:375-381`) — 헬기 출구(15m) 꼬리 쪽에서 거절. 가장 가까운 충돌면까지로, 거절도 알림.
- [✔] 가발 빔(보상) — `PGWigBeamComponent.cpp:53-56,112-115`: 클라의 "가발 썼나" 가 가방(서버에만)을 봐서 항상 거짓 → 못 쏨, 가발도 안 보임. 서버가 `bWigWorn` 복제.

### A3. 탈출
- [✔] **탈출 카운트다운·거절·취소 안내가 서버 화면에만** — `PGExtractionZoneActor.cpp:262-272, 328-340, 342-372`. 사람별 상태도 하나로 공유(:156,160). 고침: 그 사람에게 Client RPC, 상태는 사람별.

### A4. 판 시작
- [✔] **검증 단계가 첫 사람을 빼앗거나 0번 자리에 대머리 캐릭터를 남김** — `WarZoneFootprintPreview_PlayerStart.cpp:99-151`(`GetFirstPlayerController`). 멀티에서는 이 갈래를 건너뛰고 `PlaceJoinedPlayers` 에 맡긴다.
- [✔] **로딩 화면 걷기·"게임이 시작되었습니다"·마우스 모드 복귀가 서버에서만** — 같은 파일 :118-130, 141-150. 클라는 로딩 가림 없이 맵이 지어지고, 흐름 화면에서 왔으면 마우스를 눌러야 시야가 돈다(9/22 버그 재발). 고침: 클라가 맵을 다 짓고 자기 캐릭터를 받으면 스스로(또는 Client RPC).
- [✔] **전용 서버 실행 파일이 안 만들어진다** — 에디터 전용 함수가 가드 없이: `WarZoneFootprintPreview_GameplayPoints.cpp:150,154`(`SetActorLabel`, `SetFolderPath`), `_Verify.cpp:161,223`(`GetActorLabel`). `#if WITH_EDITOR` 또는 `GetActorNameOrLabel()`.

### A5. 몬스터·로봇 모양
- [✔] **모든 몬스터가 클라에서 슬라임 모양** — `Combat/PGCombatSpawner.cpp:522-526` 에서 서버가 `Visuals` 를 넣지만 복제 안 됨(`Monster/PGMonsterCharacter.h:188`). 캡슐 크기도 달라 떠 있거나 박힘. 다시 태어나는 몬스터도 같음(`PGMonsterRespawner.cpp:95`). 프리셋 이름을 복제(초기값만).
- [✔] **보스·탈것 로봇의 역할·크기가 클라에서 틀림** — `ConfigureAsBoss/Rideable`(`Robot/PGRobotCharacter.cpp:121-140`)이 서버에서만. 클라는 기본값(탈것, 배율 1) → 보스가 1배 크기로 땅에 박혀 "로봇 탑승" 이 뜸, 탈것 로봇은 겉모습·크기 틀림, 걸을 때 되돌림. `bRideable/BossScale/RideScale` 복제, `MaxWalkSpeed` 양쪽.

### A6. 탈것
- [✔] 타자마자 내림(탑승 F 가 하차로 읽힘) — 차·탱크·로봇 `NotifyControllerChanged`.
- [✔] 차 입력이 서버에서 0 으로 덮임 — 클라 차에도 입력.
- [✔] 변신차 색·카메라 통과 — `SetBodyPaint`(복제).
- [✔] **탱크가 운전자 화면에서 안 움직임** — 서버가 코드로 옮기는데 엔진은 "운전자 본인"에게 위치를 안 보낸다(`PGTankPawn.cpp:275-279,381,408`). 운전자에게도 위치를 보내 적용하거나 클라에서 같이 움직임(예측).
- [✔] **비행이 운전자 화면에서 안 보임 / 방향이 안 먹음 / 추진 신호 유실** — `Vehicle/PGFlightKitComponent.cpp:222,352,391-397,464-466`, `ServerSetThrust` 가 Unreliable(`.h:137`). 비행 상태 복제 + 클라 시선 방향 전송 + Reliable.
- [✔] 차 주차 브레이크가 클라 차에 계속 걸림(`PGVehiclePawn.cpp` `SetParked` 서버만) → 덜컹거림·바퀴 안 돎. 탑승자 OnRep 에서 양쪽.
- [✔] 차 R 키·자동 뒤집기 복구가 클라에서 안 됨(`Unflip` 서버만). `ServerUnflip`.
- [✔] 로봇 공격 동작이 아무에게도 안 보임(`PGRobotCharacter.cpp:375-377`). 멀티캐스트.
- [✔] 탑승·하차 때 시선·위치가 클라에 안 감(`PGRideHelpers.cpp:56-69`) → 내린 뒤 차 가운데에 멈춰 있다가 튐. `ClientSetRotation/ClientSetLocation`.
- [✔] 숨은 탑승자 충돌이 클라에서 켜져 있음(`PGRideHelpers.cpp:21`) → 차가 제 탑승자에 밀림. 양쪽에서 끔.
- [✔] 차 프리셋 메시(픽업·SUV…)가 클라에선 기본 차(`PGCombatSpawner.cpp:430-437`). 프리셋 이름 복제.

### A7. 피날레 (가장 큼)
- [✔] **전용 서버에서 아무도 조타석에 못 앉아 피날레가 "떠 있기" 단계에서 영원히 멈춘다** — `Finale/Battleship/PGBattleshipActor_Helm.cpp:307-316`(첫 플레이어 + `IsLocallyControlled` + 서버에서 F 읽기). 앉기 요청 Server RPC, `bSeated/SeatedPawn` 복제.
- [✔] 조종·상승·포 발사가 서버 키를 읽음(Helm.cpp:416-425, 467-472, 539-550). 앉은 클라가 입력·시선을 Server RPC 로. — W/D 조종, 오른쪽 버튼 4초에 59m 오름, 왼쪽 버튼 주포 4발(클라 화면에 빔) 확인.
- [✔] 조종석 카메라가 클라에서 멈춤(Helm.cpp:228-251, 349, 382-391).
- [✔] **클라에서 배 선체가 안 보인다**(숨김 해제가 서버에서만, `PGBattleshipActor.cpp:121-133`, Hull.cpp:297-298). `bShipHidden` 복제. + 배를 늘 보내게(`bAlwaysRelevant`) — 숨긴 액터는 엔진이 안 보내서 클라는 등장 순간에 처음 받아 조립하느라 렉이 걸렸다. 이제 클라도 숨긴 채 미리 조립 → 등장 때 보임.
- [✔] **드래곤이 클라에서 기본 자세(T포즈)로 싸운다**(`PGDragonBoss.cpp:386`, 상태 복제 안 됨 `.h:393`). 상태 + 애니 신호 복제.
- [✔] 배 위에 서 있으면 되돌림 가능성(발판이 사람마다 다른 로컬 액터·이름 없는 부품, Hull.cpp:30, 376). — 확인: 움직이는 배에 막 올라탄 직후 0.5초 동안 클라에서 최대 4m 되돌림 6번, 그 뒤 배가 계속 움직여도 0번(서버 "갑판 붙잡기"가 잡은 뒤). 남은 것: 올라타는 순간의 짧은 튐.
- [✔] (새로 찾음) 두 사람 시험: 조종석에 누가 앉으면 뒷문이 닫히는데, 경사판에 서 있던 사람이 문과 함께 22m 들려 선체 속에 끼었다(혼자 하는 판에는 없는 일). 문이 닫히기 시작할 때 문 쪽 사람을 안쪽 20m 바닥으로 옮긴다(`UPGShipDeck::MovePeopleOffRearDoor`). 확인: 배 안 바닥에 선 사람 — 배를 16초 몰고 돌고 오르내리는 동안 배 기준 최대 13cm, 되돌림 0 / 경사판에 선 사람 — 안쪽 바닥으로 옮겨진 뒤 제자리.
- [✔] 배 뒷문 열림/닫힘이 서버만(Deck.cpp:353-467) → 보이지 않는 벽. — 문 움직임을 모든 컴퓨터에서 돌린다(조건 값 `Motion`·`bHasArrivedOnce` 복제 추가). 앉을 때 닫힘을 클라에서 확인.
- [✔] (새로 찾음) 배의 "떠 있음/가는 중" 상태가 클라에 안 가서 클라에서는 배가 늘 서 있는 것으로 보였다 — 함교 글자·뒷문·비행 장비 착함이 틀렸다. 복제함.

## B. 보기 이상 (안내·이펙트)
- [✔] 피날레 경고 문구·드래곤 트로피 안내가 서버에만(`PGFinaleDirector.cpp:119-122`, `OnRep_State` 는 로그만, `PGDragonBoss.cpp:294-299`).
- [✔] 드래곤 브레스/내려찍기/추락 이펙트가 서버에만(브레스는 복제 값, 먼지·폭발은 방송) — 클라 확인.
- [✔] 배 무기·침몰 이펙트가 서버에만(주포 빔·탄착, 추락 폭발·불·먼지·흔들림을 방송으로) — 클라 화면에 주포 4발, 추락 먼지 7·불 3 확인.
- [✔] 소품 날리기(차·로봇·몬스터·배·드래곤이 치는 것)가 서버에서만 사라짐 → 클라에선 남아 있고 부딪힘(`Common/PGPhysicsUtil.cpp:460-558`, 호출처 다수). 위치 기준 멀티캐스트. — `Common/PGKnockRelay`(방송 전용 작은 액터) + `PGPhysicsUtil::ApplyRemoteKnock`. 클라는 스스로 부수지 않는다. 확인: 서버가 날린 것 5/5 를 클라가 같은 자리에서 찾아 지움.
- [✔] 비행 부스터·불꽃·빔이 아무에게도 안 보임(`PGFlightKitComponent.cpp:129-136,243-250,742-749`).
- [✔] 연료통 폭발 이펙트 서버만(`PGFloorItemActor.cpp:218-256`).
- [✔] 몬스터 공격·맞는 동작 서버만(`PGMonsterCharacter.cpp:416,496`). **형님 코드 — 사용자 허락(9/28)으로 `[멀티 임시수정 2026-09-28` 표시 달고 고침, 전달 문서에 적음.** 확인: 클라 "once anim on this screen — Slime_Attack01_ANIM". 로봇 공격 동작도 같이.
- [✔] 조종석 계기판·화면·조준선 클라에서 안 움직임(Helm.cpp:253-290, 623).
- [✔] 부스·검문소·원격 기지가 150m 밖에서 사라졌다 나타남(`bAlwaysRelevant` 없음). — 셋은 늘 보냄, 상호작용 물건·차·탱크·로봇은 복제 거리 900m. 클라가 받은 물건 473 → 3107(맵 전체).
- [✔] (새로 찾음) 맵 무너짐(피날레)이 서버에서만 — 땅·시설은 각자 지은 것이라 클라에는 그대로 남았다. 무너짐을 방송(`MulticastCollapseRegion`), 클라는 복제되는 액터는 안 지움. 확인: 클라 "collapsing on this screen".
- [✔] (새로 찾음) 숨은 탱크가 첫 시작 지역에만 — 지역마다 한 대(확인: 3대).
- [✔] 탱크 "하차" 안내가 계속 뜸(`PGTankPawn.cpp:160`), 다른 사람 탱크 뚝뚝 끊김(보간 없음). — **정정(9/28): "끊김" 은 실제로 안 고쳐져 있었다(보간 코드가 없었다).** 9/28 에 고침(아래).
- [✔] (9/28 사용자 PIE) 날아다니는 차·탱크가 클라에서 "버버벅" — 서버 위치가 올 때마다 순간이동. 받은 위치+속도로 매 프레임 부드럽게 따라가기(`Common/PGNetPoseSmoother.h`), 모는 본인·구경하는 사람 둘 다. 확인(클라 `PG.NetRideTest fly|tank mounted` 의 smoothness 줄): 비행 — 멈춘 프레임 95% → 11~13%, 한 프레임 최대 튐 238cm → 17~22cm(3번 모두 PASS) / 탱크 — 0%, 6cm. 비교 스위치 `PG.NetSmooth 0`. **정정(9/28 사용자 PIE: 날아다니는 차가 어색)**: 모는 사람이 서버를 따라가기만 하면 조작 반응이 두 배 늦었다(A 누르고 기수 5도까지 156ms). 모는 사람 화면에서 같은 비행 계산을 내 입력으로 바로 돌리고(미리 계산), 서버와의 차이는 여러 프레임에 나눠 흘려 넣는다. 확인: 반응 77~89ms(원래 순간이동 88~128ms), 멈춘 프레임 8~10%, 서버와 최대 차이 1.4~2.2m, 강제 순간이동 0. 비교 스위치 PG.NetSmooth 0/1/2.
- [✔] (9/28 시험 중 찾음) 비행 이륙 순간 바퀴가 땅에 묻힌 채 시작하면 서버에서 차가 제자리에 붙음(네 번 중 한 번) — 묻혀서 막히면 위로 빼 준다.
- [✔] (9/28 사용자 PIE) 로봇이 클라에서 떠 있음(변신 전후) — 엔진의 "캐릭터 움직임 부드럽게 하기" 가 처음 메시 자리로 되돌림. 모양을 바꾼 뒤 메시 자리를 다시 기억시킴(`CacheInitialMeshOffset`, 로봇 BeginPlay·몬스터 모양 부품). 확인: 고치기 전 클라 메시 높이가 서버보다 8.8m 위(24 → 902), 고친 뒤 같음(`PG.RobotAudit` vs `Client monster audit`).
- [✔] (9/28 사용자 PIE) 추가 시작 지역에 길이 없고, 에픽템 장소(외진 보상 거점) 바로 뒤에 섬 — 시작 지역을 길 깔기 전에 논리 칸으로 고르고, 워존까지 길을 "시작·출구 → 워존 길 보장" 과 같은 방법으로 깐다. 담장 입구는 그 길 쪽. 빈 모서리(보상 거점 자리)와 둘레 6칸은 시작 지역 후보에서 뺀다. 보상 거점은 모든 시작 지역을 보고 가장 먼 모서리. 확인(시드 3개, 서버+클라 2): 워존 길 연결 실패 0, 길 모양 어긋남 0, 서버=클라 맵, 보상 거점 ↔ 가장 가까운 시작 지역 7.5~9.6칸(150~190m, 전에는 5.5칸).
- [✔] (9/28 사용자 PIE) 차가 소품·벽에 박을 때 멈칫(1p·2p 모두) — 부수기를 서버만 해서, 클라 차는 서버 알림이 올 때까지 소품에 막혔다. 이 화면 사람이 모는 차·로봇이 부딪힌 소품은 클라가 먼저 부수고, 뒤에 온 서버 알림은 건너뛴다(`PGPhysicsUtil::NotePredictedKnock`). 몬스터 들이받기·걷어차기·얹힌 아이템·받침 무너짐은 서버만. 확인: 클라 "server confirmed a knock this screen already did", 같은 4초 주행 20m → 31~32m.
- [✔] (9/28 사용자 PIE) 날아다니는 차가 공중에서 "차량 하차" 안내 — 날 때는 바퀴 움직임을 꺼서 바퀴 속도가 늘 0("멈춤")으로 읽혔다. 날고 있으면 비운다. 확인: 클라 "rider prompt while flying = """.
- [✔] (9/28 사용자 PIE) 우주선(전함)·드래곤 움직임 버벅임 — 클라가 서버 위치로 순간이동. 부드럽게 따라가기(구경·조종석). 단, 이 화면 사람이 배 위에 서 있으면 엔진 그대로(받는 즉시 옮김) — 부드럽게 하면 서 있는 사람이 배를 못 따라왔다(57m). 확인: 조종석 화면 배 멈춘 프레임 96% → 0%, 배 위 사람 최대 13cm·되돌림 없음.
- [✔] (9/28 사용자 PIE) 탱크로 친 나무가 조금 기울다 가라앉음 — 넘어지던 나무를 탱크가 계속 밀며 다시 치면(Kick) 넘어지기를 끊고 "밀려 떨어지는 조각" 으로 바꿨다(멀티만의 문제 아님). 넘어지는 중이면 더 빨리 넘어가게만 한다. 확인: 서버·클라 모두 "Debris tip end → falls"(전에는 끝이 없었다). 클라가 먼저 부순 16개 = 서버가 부순 16개(이름·자리 일치).
- [✔] (9/28 사용자 PIE, 두 번째) 탱크로 외곽 가면 땅 밑으로 빠짐 — 서버에서 이미 빠졌다: 경계 되밀기는 중심만 안(2m)이면 두었는데 9m 탱크 앞쪽이 타일 밖 허공에 걸려 "귀퉁이 셋 미만 = 자유 낙하" 가 시작됐고, 되밀기는 가로만 되돌려 계속 빠졌다. 탈것은 몸 크기만큼 더 안쪽으로 되밀고(하늘 25m 위는 그대로 열림), 탱크는 귀퉁이 한두 개만 닿아도 그 높이로 선다. 클라 미리 계산도 떨어뜨리지 않는다. 확인(`PG.TankToEdge 500` → 바깥으로 주행): 탱크 35번 되밀림·클라 높이 218 유지.
- [✔] (확인 중 찾음) 땅 차는 모는 사람 기준이라 서버가 옮긴 자리(경계 되밀기·뒤집힌 차 세우기)가 그 화면에 안 갔다 — 옛 자리를 계속 보내 서버 차를 되돌려 311m 어긋남. 서버가 옮기면 `ClientResetCarPose`, 번호(Epoch)가 다른 옛 자리는 버린다. 확인: 가장자리 주행 18번 되밀림이 클라에도 그대로, 내린 자리 1.5m.
- [✔] (9/28 사용자 PIE) 날아다니는 차가 떨어질 때 알트탭 → 바닥 밑 — 창이 뒤로 가면 프레임이 크게 떨어져 긴 한 걸음에 얇은 바닥을 지나쳤고, 모는 사람 기준이라 서버가 그 자리를 믿었다. 서버 차 자리와 새 자리 사이에 땅이 끼어 있으면 거절하고 서버 자리로 되돌린다. 확인: `PG.CarPoseProbe` 거절, 클라 되돌림. 초당 10프레임 비행도 정상(이륙 때 바닥에 딱 닿아 막히던 것도 위로 빼게 — 이것도 확인 중 찾음).
- [✔] (9/28 사용자 PIE) 드래곤 등장 직전 프레임 드랍 — 화면 그리는 클라(`headless_multi.ps1 -ClientRender`)로 잼: ① 드래곤 동작을 이름으로 찾을 때 이미 읽은 것·없는 이름도 LoadObject 로 "읽기 다 끝날 때까지 기다리기"(FlushAsyncLoading) → 메모리에 있으면 그대로, 없는 이름은 목록만 확인. ② 땅 무너짐이 한 프레임에 땅 묶음 12개를 새 인스턴스 메시로 옮기며 그리기 준비 비용 → 3개로(`PG.Collapse.MovesPerFrame`). 결과: 등장 무렵 가장 긴 프레임 114ms → 67ms, 80ms 넘는 프레임 0, 평균 67 → 112fps. (드래곤 몸 보이지 않게 한 번 만들어 두기(재질 미리 준비)도 넣었지만 효과는 따로 재지 못했다.)
- [✔] (9/28 사용자 PIE) 날아다니는 차가 날다가 조금씩 멈칫 — 서버와의 작은 차이까지 매번 맞추느라 앞뒤로 당겼고, 서버 속도를 받은 간격으로 짐작해 불규칙할 때 흔들렸다. 1.5m·3도 안쪽 차이는 두고 넘친 만큼만 천천히(4/초), 서버 속도는 직접 받는다(DriverPoseVelocity). 확인(화면 그리는 클라, 서버 15fps 포함): 순항 중 속도가 절반 아래로 떨어진 프레임 0개, 강제 순간이동 0.
- 걷기 버벅임(9/28 사용자 PIE): 두 사람이 걷는 동안 서버 되돌림 0번(생성 때 1번 빼고) — 접속 문제가 아니라 한 PC 에서 서버+창 둘을 돌리는 부담으로 본다(PIE FPS 36). 확인은 PIE 콘솔 `stat unit`.
- [✔] 땅에서 달리는 차가 클라에서 가끔 툭 튐 — 모는 사람 컴퓨터와 서버의 차 물리가 조금씩 달라 엔진이 서버 자리로 끌어왔다. 모는 사람 화면을 기준으로: 자기 차 자리·속도를 1초 30번 서버로 보내고(20m 넘게 먼 값은 거절), 모는 사람 화면에는 서버 자리를 덮어쓰지 않는다(`APGVehiclePawn::ServerSetCarPose`, `PostNetReceivePhysicState`). 확인: 한 프레임 속도 급변 681km/h → 13~15km/h.
- [✔] 탱크 모는 사람 화면 반응이 한 박자 늦음 — 날아다니는 차와 같이 미리 계산(TickDrive 를 모는 사람 화면에서, 몬스터 들이받기는 서버만) + 서버 차이 흘려 넣기. 확인: A 반응 165ms → 81ms.
- [✔] (9/28 사용자 PIE) 드래곤 등장 때 프레임 드랍 — 드래곤 몸·동작 15개·불 효과를 등장 순간에 읽었다. 판 시작(로딩 화면) 때 모든 컴퓨터에서 뒤에서 미리 읽는다(`APGDragonBoss::PreloadAssets`, 디렉터 BeginPlay). 전함 도착 때 읽으면 0.55초 멈칫해서 로딩 때로 옮겼다. 확인(`PG.HitchLog`): 전함·드래곤 등장 때 80ms 넘는 프레임 서버·클라 0번.
- [✔] (9/28 사용자 PIE 경고) 상자(SM_box, 정밀 충돌)를 날릴 때 "피직스 시뮬레이션이 켜져 있어야 충격량 추가" — 물리를 못 켜는 모양이면 보이지 않는 상자 몸통(KnockBody)으로 날린다.
- [✔] (확인 중 찾음) 날아간 상자가 클라 화면에서는 제자리 — 서버만 루트를 물리 몸으로 바꿔 클라에는 받을 몸이 없었다(원래부터). bKnockedLoose 복제 → 클라도 같은 몸통을 만든다(MakeLooseBody). 확인: 서버 `PG.KnockBoxTest`(2초 4.5m), 클라 `PG.NetContainerWatch 10 60`(그 상자 6.5m, 246개 중 1개만), 경고 없음.

## C. 작은 것
- 전함 파편 발사 간격 검사 여유 없음(탱크·빔 몇 발 씹힘), 로봇 서버 틱 0.2초 유지, 여고생 키 기준이 첫 플레이어, 길찾기 막이가 첫 사람만, 서버에서 화면 설정(FPS 제한 등) 적용, 시험용 명령(`PG.Finale.Start`, `-PGDragonAutoKill`, `-PGOutpostShot`)의 첫 플레이어 가정.

## 이미 괜찮은 것
- 맵(설계도 복제로 같은 맵), 시작 지역 4곳·배치, 몬스터 대기·달리기·죽음 애니, 몬스터·배·드래곤 위치 복제, 탱크 포탑 방향 복제, 비행 부품이 클라에 생김, 복제 물건 생성은 모두 서버에서(중복 없음), 문·상자·폭발 소리.

## 고치는 순서(제안)
1. 빠르고 막히는 것: A4(검증 단계·로딩·안내·서버 빌드) → A2 물건 겉모습 복제 → A3 탈출 안내 → A5 몬스터·로봇 모양.
2. A6 탈것 나머지.
3. A1 판 끝·기록(사람별) — 형님 매칭·서버와 맞물림.
4. A7 피날레 — 제일 큼(배 조종 전체를 클라 입력 기준으로).
5. B·C.

## 확인한 방법(✔ 항목)
- 전용 서버 + 클라 1~2(`Tools/wbp/headless_multi.ps1 -Dedicated`), 시드 1790501797.
- 클라 로그: `Local player ready`(로딩 걷힘 자리), `Client object audit`(물건 473 중 472 모양 — 나머지 1은 원래 투명 창구),
  `Client level doors: done, hid 9`, `Client monster audit`(서버 프리셋과 같은 메시), `my run result from the server`.
- 클라 조작 시험 `PG.NetRideTest car|tank|fly [mounted]`(+ 서버 `-ServerCmd "PG.Delay 55 PG.SpawnVehicle fly, PG.Delay 62 PG.RideTest car stay fly"`):
  차 — 걸어가 F 로 타고 운전자 화면 4초 35m, 하차 후 땅·보임·충돌 PASS / 탱크 — 4초 13m, 하차 PASS / 비행 — 옆 79m·위 30m.
- 판 끝: 서버 `PG.Delay 60 PG.Flow.Finish extract` → 두 사람 각각 기록, 서버는 맵에 남고 두 클라가 각자 결과 화면으로.
- 로봇 역할·크기, 가발, 유지 시간, 창구 잠금, 원래 문이 보이는지(눈) 는 로그·코드로만 — PIE 눈 확인 대기.
- 시험 창은 숨김으로 뜬다(`headless_multi.ps1` -WindowStyle Hidden). 서버 전용 인자 `-ServerArgs`.
- 주포·오르기: 클라 `PG.NetRideTest helm`(W+D 6초 → 오른쪽+왼쪽 버튼 4초). 추락: 서버 `PG.Finale.WreckShip`. 연료통: 서버 `PG.ExplodeFuel`.
- 두 사람 배 위 시험: `-Clients 2 -ClientCmd "PG.NetRideTest helm" -Client2Cmd "PG.NetRideTest deck, p.NetShowCorrections 1"` + 서버 `"PG.Delay 45 PG.Finale.Start 0, PG.Delay 80 PG.Finale.Board 1, PG.Delay 110 PG.Finale.Bridge 0"`(경사판 시험은 `Board 1 ramp`). 시험 도구가 1번 접속을 확인한 뒤 2번을 띄운다(순서 고정). 서버 `-PGShipInsideLog` 로 밟은 바닥 기록.
- 배 위 되돌림: 클라 `-ClientCmd "p.NetShowCorrections 1"` + 서버 `-ServerArgs "-PGFinaleAutoLaunch" -ServerCmd "PG.Delay 45 PG.Finale.Start 0, PG.Delay 66 PG.Finale.Board"`, 클라 로그 `Client: Error for` 개수.
- 조종석: 클라 `PG.NetRideTest helm` + 서버 `-ServerCmd "PG.Delay 45 PG.Finale.Start 0, PG.Delay 120 PG.Finale.Bridge, PG.Delay 160 PG.Finale.Bridge"`
  → 클라가 F 로 앉음(서버 "took the helm seat"), 화면이 배로, W+D 6초에 배 135m·104도, F 로 일어서 화면이 몸으로 PASS. 클라에서도 뒷문 닫힘(CloseRearDoor).
- 드래곤: 서버 `-ServerArgs "-PGFinaleAutoLaunch -PGDragonAutoKill" -ServerCmd "PG.Delay 45 PG.Finale.Start 0"`
  → 클라 로그 `anim on this screen`(날기·공격·죽음 동작), `breath ON/off on this screen`, 경고 2줄·트로피 안내 2줄, `crash on this screen — blast=1`, 먼지 3번.
- 중앙 건물 부수기(9/28): 서버 `-ServerCmd "PG.Delay 100 PG.SmashCoreTest"` + 클라 `-ClientRender -ClientCmd "PG.NetSmashTest 15" -Client2Cmd "PG.NetSmashTest 25"`
  → 1번이 8배 로봇으로 중앙 건물을 지나며 부수고 2번은 70m 옆에서 본다. 1초마다 프레임·게임/렌더/GPU 시간·잔해 수·따라 하기 비용.
  ✔ 구경하는 사람에게 부서짐이 다 간다: 소품 알림을 하나씩 "신뢰 안 함" 으로 보내 1초에 2개만 가던 것(서버 160개 → 구경꾼 2개/초) → 프레임마다 묶어 보냄, 174/174.
  ✔ 부수는 사람 화면: 원인은 중앙 건물 레벨의 그림자 조명 52개(VSM 을 부서질 때마다 조명 수만큼 다시 그림). 시설 레벨이 뜰 때 점·스포트 조명 그림자를 끈다
    (`PG.Facility.ShadowLights`, 기본 0) → 부수는 동안 GPU 38~40ms → 30~33ms, 가장 긴 프레임 60 → 46ms. 부수지 않고 건물 앞에 서 있기만 해도 GPU 31ms 라
    나머지는 로봇 카메라로 맵 가운데를 넓게 보는 비용(시험 PC 한 대에서 화면 둘을 그린 값). 따라 하기 계산은 조각당 약 0.7ms(1초 40개일 때 프레임당 1ms 남짓).
- 드래곤 구덩이(9/28): 원 → 칸 모양. 원 안에 한가운데가 든 칸만 무너뜨리고 벽은 칸 경계를 따라 곧게. 서버 `-ServerArgs "-PGFinaleAutoLaunch" -ServerCmd "PG.Delay 45 PG.Finale.Start 0"`
  → 서버·클라 같은 값 `13788 instances ... in 75 cells`, `pit - 75 cell floors ... 38 straight wall planes`.
  ✔ 그리지 않는 컴퓨터(전용 서버)에서는 타일 묶음 범위 상자가 0 이라 전부 건너뛰어 서버 땅이 안 무너지던 것(원래 있던 문제)도 고침.
- 숨긴 탱크 물가(9/28): 가운데·네 귀퉁이 땅 높이가 시작 자리와 20cm 넘게 다르면 버림, 바깥이 다 물가면 옆·안쪽까지. 서버 로그 `hidden tank at ... ground 20 vs spawn 20` 네 지역 모두.
