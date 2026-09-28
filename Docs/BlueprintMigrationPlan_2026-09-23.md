# 블루프린트 분리 (2026-09-23 끝남)

## 결정
눈에 보이는 것(화면 배치, 모델·재질·이펙트, 조절 숫자)은 블루프린트·데이터 에셋·데이터 테이블로 뺀다.
동작(버튼이 하는 일, 맵 생성, 탈출·루팅 규칙, 판 흐름, 저장, 멀티 동기화)은 C++ 에 둔다.
소리는 넣지 않았다(나중에 Wwise 로 따로 붙인다).

방식:
- **액터·화면** → C++ 부모 + 블루프린트 자식. C++ 는 "칸"(UPROPERTY, 기본값 = 원래 쓰던 에셋)과 "이름 약속"(BindWidgetOptional)만 연다.
- **액터가 아닌 것**(서브시스템, 코드가 놓는 무대, 인트로, 맵 짓기) → 데이터 에셋(DA_PG...).
- **표로 된 것**(무기, 착장) → 데이터 테이블(DT_PG...). 처음 표는 코드 표를 한 줄씩 그대로 옮겨 만들었다.
- 게임이 무엇을 쓸지는 프로젝트 설정 두 곳에 적힌다(`Config/DefaultGame.ini`):
  - **ProjectPG Flow > Screens**: 화면 WBP
  - **ProjectPG Visuals**: 액터 블루프린트, 데이터 에셋, 데이터 테이블
- 칸이 비었거나 에셋을 못 읽으면 C++ 기본값(원래 모습)을 쓴다. 그래서 무엇을 잘못 넣어도 게임이 멈추지 않는다.

모든 에셋은 `/Game/PG/UI/{Screens,HUD}` 와 `/Game/PG/Blueprint/Visual` 에 있다.

## 어디서 바꾸나 (전체 지도)

### 화면 (WBP)
| 화면 | WBP | C++ 부모 |
|---|---|---|
| 타이틀 | UI/Screens/WBP_PGTitle | UPGTitleScreenWidget |
| 로비 | UI/Screens/WBP_PGLobby | UPGLobbyScreenWidget |
| 결과(스코어보드) | UI/Screens/WBP_PGScoreboard | UPGScoreboardScreenWidget |
| ESC 메뉴 | UI/Screens/WBP_PGPauseMenu | UPGPauseMenuWidget |
| 환경설정 | UI/Screens/WBP_PGSettings | UPGSettingsWidget |
| 상호작용 안내 [F] | UI/HUD/WBP_PGInteractionPrompt | UPGInteractionPromptWidget |
| 로딩·시작 로고 | UI/HUD/WBP_PGLoadingScreen | UPGLoadingScreenWidget |
| 안내 문구·카운트다운 | UI/HUD/WBP_PGAnnounceLine, WBP_PGCountdownLine | UPGAnnounceLineWidget, UPGCountdownLineWidget |

목록 줄(지난 레이드 한 줄, 창고 아이템 한 줄, 기록 카드)은 C++ 가 WBP 안의 빈 칸(목록 상자)에 채운다.

### 액터 블루프린트 (Blueprint/Visual)
| 무엇 | 블루프린트 | 여는 칸 |
|---|---|---|
| 외진 보상 거점 | BP_PGRemoteOutpost | 헬기·텐트·상자 등 25칸 |
| 출구 검문소 꾸밈 | BP_PGExitDressing | 펜스·초소 등 21칸 + 표지 글자·색 |
| 변신 여고생 | BP_PGTransformNPC | 임시 몸·방패·깡통·먼지 |
| 피날레 전함 | BP_PGBattleship | 11칸 |
| 드래곤 | BP_PGDragonBoss | 메시·동작 폴더·불·충돌 폭발 |
| 코드로 짓는 시설 | BP_PGProceduralFacility | 벽·지붕·상자 등 32칸 |
| 탱크 / 차 / 로봇 | BP_PGTank / BP_PGVehicle / BP_PGRobot | 몸·포탑 / 차종별 모델 / 탑승 로봇 모습 |
| 바닥 아이템 | BP_PGFloorItem | 연료통 폭발 먼지·불덩이·재질 |
| 상점 부스 | BP_PGBooth | 부스 부품 폴더 |
| 날으는 차 비행 키트(부품) | BP_PGFlightKit | 부스터·불꽃·빔 |

캐릭터 블루프린트 안의 부품에서 바꾸는 것: 가발 빔 모양(WigBeam), 몸 머리·팔(Wearable), 검증 캐릭터 이동 동작 4개.

### 데이터 에셋 (Blueprint/Visual)
| 에셋 | 담은 것 |
|---|---|
| DA_PGMonsterLooks | 몬스터 종류별 메시·동작·크기 |
| DA_PGEffects | 잔해 먼지, 미사일 폴더·재질 |
| DA_PGFlowStage | 타이틀·로비·결과 뒤 3D 무대: 캐릭터·동작·바닥·풀·나무·바위·차·폐차·불·연기 |
| DA_PGTitleIntro | 인트로 3종: 동작·총·총구 불꽃·차·배·드래곤·빔·먼지 (주인공·바닥은 DA_PGFlowStage 를 같이 씀) |
| DA_PGMapVisuals | 맵 짓기가 게임 중에 읽던 것: 무너짐 먼지·바위 재질·풀밭 장식·호숫가 배·비탈·타일 풀/바위 등 |

### 데이터 테이블 (Blueprint/Visual)
| 표 | 행 | 담은 것 |
|---|---|---|
| DT_PGWeapons | FPGWeaponDef | 총 5종: 모양·손에 드는 자리·성능 |
| DT_PGWearables | FPGWearableColor | 옷 색 24줄: 메시·재질·바닥 모양·붙는 뼈 |

| DT_PGItemValue | FPGItemValueRow | 아이템 50줄: 이름·등급·값·바닥 메시·같이 떨어질 탄 (9/28 추가) |
| DT_PGObjectCatalog | FPGObjectCatalogRow | 물건 목록 104줄: 상자·장치·바닥 아이템의 모양·크기 (9/28 추가) |
| DT_PGLootTables | FPGLootTableRow | 상자 속 아이템 표 11개 (9/28 추가) |

설정 칸 하나: 바닥 연료통 세 색(ProjectPG Visuals > Fuel Can Meshes).
9/28 추가: 위 세 표는 전에는 코드 표 + Docs CSV 로만 읽어서(에디터에서 못 고치고, 패키징하면 Docs 가 안 따라가 코드 표로 돌아감)
데이터 표 에셋으로 옮겼다(`Tools/wbp/make_object_tables.py`). 설정 ProjectPG Objects / ProjectPG Item Grades 의 기본값이 이 에셋이다.
코드 표(PGObjectSmokeTest::RegisterDefaultCatalog, PGItemValue 예비 표)는 에셋이 없을 때만 쓴다.
바닥 총 모양: 총마다 눕힌 바닥 메시(SM_PGWFloor_*)를 표의 FloorMesh·물건 목록 메시가 가리킨다. AR70 은 `Tools/make_ar70_floor.py` 로 만들었다(9/28).
DA_PGMapVisuals > Lighting > FacilityShadowLights: 시설 레벨마다 그림자 남길 전등 수(기본 0 — 중앙 건물 조명 52개가 부서질 때 프레임을 떨어뜨렸다).

## 일부러 코드에 둔 것
- **맵 생성 규칙이 고르는 타일·시설**: 모양은 이미 타일 블루프린트(BP_Tile_*)·묶은 타일(BPP_Tile_*)·시설 레벨(LD_Facility_*)에 있다. 코드는 "어느 것을 쓸지" 만 정한다.
- **맵 짓기 액터 부품 기본값**(바닥·도로·호수 재질, 배경 산, 지형 장식): 레벨에 놓인 맵 짓기 액터의 부품 칸에서 바꿀 수 있다.
- **지형 굴곡(SM_Terrain_*)·호숫가(SM_Shore_*) 메시**: 코드의 높이 공식·생성기(PG.BuildShoreMeshes)와 숫자가 묶여 있어 바꾸면 소품이 공중에 뜬다.
- **엔진 기본 도형**(큐브·원통·평면): 코드가 크기를 정해 쓰는 재료.
- **인트로 검은 띠, 십자 조준선**: 화면 비율·조준 계산과 한 몸이라 코드에 둔다.
- **스모크 테스트·디버그 명령(PG.SpawnBattleship 등)의 경로**: 시험 도구.
- **팀원 코드**(맵 생성기, 게임모드, MapTile, 인벤토리·로그인 UI).

## 검증 방법 (다시 옮기거나 고칠 때)
- 에셋 칸 확인: `Tools/wbp/check_slots.py` (에디터 끈 상태, 데이터 에셋·블루프린트 칸이 읽히는지)
- 같은 시드 로그 비교: `Tools/wbp/headless_map.ps1` + `Tools/wbp/logdiff.py` (시드 12345, 1790045299)
- 화면 그림: `-PGFlowShot`(타이틀·로비·결과), `-PGIntroShots -PGIntro=<Rescue|Ambush|ShipDragon>`, `-PGOutpostShot`(게임 맵) + `Tools/wbp/imgdiff.ps1`
- 게임 중에만 나오는 것: 콘솔 `PG.VisualProbe` (미사일 한 발·연료통 폭발·비행 키트·무기 표·착장 표를 로그로)
- 스모크: `PGObjectSmokeTest passed=64 failed=0` 유지
- 매번 조금씩 달라서 비교하면 안 되는 줄: AI 걷기 시험 시간, `PCG dressing generated ... managed_resources`(3~5), 불·연기 움직임

## 옮기다 고친 버그
- 결과 화면 "생존율" 줄 이름·값 겹침 (WBP 로 옮기며 폭 수정)
- 옷 색 뽑기(PickColorVariant)가 실행마다 달라질 수 있던 문제: 이름 번호(FName 순서) 대신 글자로 해시. 같은 시드면 서버·클라이언트 어디서든 같은 색. 대신 시드별 색은 9/23 전과 다르다.

## 커밋
UI 1단계 ~ 4f92e08, 2단계 8번 e0d26a2 ~ 14번 8a212d1 (`team-merge`).
