> **격자 크기 정정(2026-09-21):** 이 문서는 논리 격자가 45×45 이던 시절에 쓰였다.
> 프레임이 안 나와서 **30×30(900셀, 약 600×600m)** 으로 줄였고(`MapGeneratorComponent.h` `_mapSize = 30`),
> 현재 규격 문장은 그에 맞게 고쳤다. 다만 **아래에 남은 측정 기록(2,025셀 등)은 45×45 시절의 실측값**이라
> 일부러 그대로 두었다 — 지난 측정을 고쳐 쓰면 기록이 아니라 거짓이 된다.

# ProjectPG 절차적 맵 디자인 완료 보고서

## 완료 범위

`D:\ProjectPG`의 맵 재료는 싱글에서 실제 캐릭터가 지면에 스폰되고, NavMesh를 따라 이동하며, 전투·루팅·탈출 시스템이 연결될 후보 위치를 제공하는 단계까지 완성했다.

완료된 것은 **맵 디자인과 생성 데이터**다. 총기, 피해, 인벤토리, 실제 Loot Actor, AI 행동트리, 탈출 판정/UI, 세션 이동은 별도 게임플레이·서버 시스템이다. 이 시스템들은 맵을 다시 만드는 대신 아래 Point/Manifest 데이터를 소비하면 된다.

## 실행·검수 위치

- 프로젝트: `D:\ProjectPG\ProjectPG.uproject`
- 전체 생성 테스트 맵: `/Game/PG/LevelDesign/Tests/LD_MetaballGenerationTest`
- 타일 Blueprint: `/Game/PG/LevelDesign/TacticalTiles`
- 개별 타일 검수 레벨: `/Game/PG/LevelDesign/Tiles`
- 다중 셀 시설: `/Game/PG/LevelDesign/Facilities`
- 서버 인계 명세: `D:\ProjectPG\Docs\ProceduralLevelServerHandoff.md`

고정 Seed는 에디터/Standalone 실행 인수 `-PGMapSeed=1337`처럼 지정한다. 인수가 없으면 기존 형님 흐름처럼 시간 Seed를 사용한다.

## 맵 재료

- 실제 크기: 20m/셀, 30×30셀, 약 600×600m
- 일반 타일: Meadow, ForestSparse, ForestDense, Rocky, Scrub, Ambush, ServiceCamp, Ditch, Ruins, Road Straight/Corner/T/Cross/DeadEnd, Spawn, Exit, Obstacle
- 각 일반 타일: 결정적 전술 배치 4종
- 시설: RuralCompound 3×3 1개, RuralCamp 2×2 5개, Checkpoint 1×2 1개
- 바닥·도로: HISM 기반 공통 지면/충돌
- 시각 타일: Blueprint 로컬 생성
- RuralCompound/RuralCamp: Runtime Blueprint, Checkpoint: Soft World Reference + `ULevelStreamingDynamic`
- Nav: Invoker 주변 동적 생성 + 타일 벽/엄폐/부착 Prop 로컬 장애물

## 게임플레이 소켓 계약

각 `FLevelDesignPoint`는 다음을 가진다.

- 고유 `PointId`
- Spawn/Loot/AISpawn/Exit 타입
- 셀 좌표와 실제 월드 좌표
- `ArchetypeId`, Tier, Radius, Capacity
- 결정적 `PointSeed`

Seed 1337 기준 후보는 Spawn 4, Loot 78, AI 84, Exit 2다. AI 84개는 동시 AI 수가 아니라 서버/싱글 규칙이 거리·난이도·성능 예산에 따라 선택할 후보 풀이다.

시설 로드 후 실제 캡슐 겹침을 다시 검사한다. 막힌 후보는 동일 셀 내부의 안전 포켓으로 결정적으로 이동하며, 최종 좌표를 `GameplayPointHash`에 포함한다. Spawn 후보는 서로 최소 2.5m를 유지한다.

## 최종 Standalone 회귀

| Seed | LayoutHash | PointHash | Point 수 (S/L/AI/E) | 엄폐 완전노출 / 최장 | 전체 목적지 | AI 이동 | FPS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1337 | `46692AA5` | `87EE23E0` | 4 / 78 / 84 / 2 | 6.3% / 60m | 9/9 | 33.6m | 125.4 |
| 424242 | `0D20C3C6` | `DCC0B19E` | 4 / 73 / 88 / 3 | 6.7% / 40m | 10/10 | 33.5m | 149.4 |
| 8675309 | `42237197` | `3852F858` | 4 / 61 / 102 / 4 | 9.1% / 60m | 11/11 | 33.6m | 123.9 |

공통 결과:

- 셀 충돌 2,025/2,025, 누락 0
- 타일 격자 오류·경계 초과·잘못된 연결/회전 0
- 시설 7개, 겹침 0
- 시설 로드 후 안전하지 않은 Point 0
- 전체 논리 셀 연결 2,025/2,025
- 중앙 Nav 후보 투영 실패 0
- Fatal, Ensure, 프로젝트 에셋 로드 오류 0
- 오프스크린 Standalone 물리 메모리 약 3.6GB

FPS는 개발 PC 단일 실행의 비교값이며 Shipping 빌드나 목표 최소 사양 성능 보증은 아니다.

## 2026-08-14 MCP PIE 통합 회귀

형님의 `BP_GameMode`가 생성한 논리 `BP_MapTile` 900개 위에 시각 조립 계층을 자동 연결한 최신 회귀 결과다. 테스트용 OpenWorld Landscape는 절차 타일 지형과 무관하므로 충돌 판정에서 제외했고, 30×30 범위를 덮는 NavMesh Bounds로 검사했다.

- 단일 셀 시각 Blueprint 요청 1,994, 시설 Blueprint 6, 생성 실패 0
- 시설 총 7개: Runtime Blueprint 6 + 스트리밍 Checkpoint 1
- 시설 겹침 0, 최소 가시 경계 간격 1,305.5cm
- 셀 지면 충돌 2,025/2,025, 누락 0
- 안전하지 않은 Spawn/Loot/AI/Exit Point 0
- 타일 중복·경계 초과·잘못된 명세 0
- 엄폐 완전노출 6.3%, 최장 완전노출 구간 40m
- Nav 투영 21/21, 논리 목적지 11/11 도달
- 검증 AI 33.6m 이동, 목표 오차 1.38m, 경로 성공
- 60FPS 제한에서 평균 16.67ms / 60FPS, Actor 4,498, 물리 메모리 약 3.23GB
- `LayoutHash=4B7887AC`, `GameplayPointHash=FF060B9D`

FPS 수치는 에디터 비활성 창 제한을 해제한 PIE 목표 프레임 검증이다. 최소 사양과 동시 멀티 클라이언트 성능은 패키지 빌드에서 별도로 측정해야 한다.

실제 호스트 레벨에는 900×900m 생성 범위를 덮는 `NavMeshBoundsVolume`을 영구 배치하거나, 서버/공통 생성 계층이 같은 범위의 Nav Bounds를 준비하는 정책이 필요하다. 이번 임시 테스트 월드는 저장하지 않았다.

## 다른 프로젝트로 옮길 때

Content 폴더만 복사하면 Blueprint의 C++ 부모가 없어 깨진다. 최소 이관 단위는 다음과 같다.

1. `/Game/PG/LevelDesign` 전체
2. 해당 레벨이 참조하는 `/Game/Fab`, `/Game/TSL_CQBModularCore` 에셋
3. `ATacticalTileActor`, `ATacticalTileRoadStraight`, `ATacticalPropActor`, `AWarZoneFootprintPreview`, 내비 보조 컴포넌트
4. 형님 쪽 `AMapTile`, `UMapGeneratorComponent`, `AGameModePG`와의 타입/Seed 접점
5. Build.cs의 AI/Navigation/PCG 모듈과 프로젝트 Nav 설정

이관 후에는 서버가 정한 Manifest를 입력하고 같은 `LayoutHash`와 `GameplayPointHash`가 나오는지 먼저 확인한다. 그 다음 실제 Pawn/AI/Loot/Exit Actor를 서버 권한으로 소켓에 주입한다.

## 멀티 전환 경계

- 서버: Seed, LayoutRevision, 셀/시설/Point 목록, 실제 AI/Loot/Exit 상태 권한
- 클라이언트: 동일 목록으로 지면·정적 Blueprint 타일·시설 시각 로드
- 공통/서버 충돌: 지면, 벽, 엄폐, 시설의 권한 판정 형상
- 별도 복제: 문, 획득 가능한 Loot, AI, 파괴/이동 물체, Exit 진행 상태

현재 타일 디자인은 이 경계를 유지하므로 멀티 전환 때 맵을 다시 디자인할 필요는 없다. 다만 데디서버 Ready/Hash 대조와 실제 동적 Actor 생성 규칙은 서버 담당자와 연결해야 한다.

## 보호 범위

이번 작업은 `D:\ProjectPG`만 수정했다. `D:\ProjectPG`는 수정하지 않았다. 파일이나 에셋 삭제도 수행하지 않았다.
