# ProjectPG 문서 안내 (2026-09-28 정리)

작업을 이어갈 때는 아래 순서로 읽는다. 9/28 에 끝난 계획·옛 인계 문서는 지웠다(내용은 git 기록에 남아 있다).

## 지금 상태 — 여기부터

1. [`MultiplayerAudit_2026-09-27.md`](MultiplayerAudit_2026-09-27.md) — 멀티(전용 서버 + 최대 4명) 점검표. 무엇을 고쳤고 **어떻게 확인했는지(시험 명령)**, 남은 것.
2. [`TeamTodo_2026-09-28.md`](TeamTodo_2026-09-28.md) — 형님 두 분(캐릭터·몬스터 / 서버·UI) 각각 할 일과 에셋 zip 받는 법. 자세한 내용은 아래 문서.
   [`TeamHandoff_2026-09-27_PlayerMultiplayer.md`](TeamHandoff_2026-09-27_PlayerMultiplayer.md) — 팀원(형님)에게 넘길 것: 플레이어 피해·사망, PvP 규칙, 매칭, 팀원 코드 안 임시 수정 표시.
3. [`BlueprintMigrationPlan_2026-09-23.md`](BlueprintMigrationPlan_2026-09-23.md) — 보이는 것(화면·모델·효과·표)을 어디서 고치나. 9/28: 아이템·물건 목록·상자 표도 데이터 표 에셋.
4. [`ProcessLog_2026-09-23.md`](ProcessLog_2026-09-23.md) — 8월~9/28 전체 과정: 시간 순서, 분야별 문제→원인→해결→결과.
5. [`CodeReview_OOP_SOLID_2026-09-21.md`](CodeReview_OOP_SOLID_2026-09-21.md) — 객체지향·SOLID 자가 점검(9/28 추가: 긴 함수 나누기와 아직 약한 곳).
6. [`Portfolio/deck/`](Portfolio/deck/) — 발표형 포트폴리오 PDF 와 만드는 스크립트(`build_deck.py`).

## 분야별 참고

- [`CodeFirstLevelDesignStatus_2026-08-18.md`](CodeFirstLevelDesignStatus_2026-08-18.md) — 맵 시각화 층의 코드 구조·시설 규격. 맵 규격이 문서끼리 다르면 이것이 기준.
- [`MapDesignCompletionReport.md`](MapDesignCompletionReport.md) — 맵 디자인 검증 결과 보고(45×45 시절 측정값 포함).
- [`ObjectArchetypes_2026-09-13.md`](ObjectArchetypes_2026-09-13.md) — 오브젝트 원형 9개 + 카탈로그. **스모크 테스트 헤드리스 명령**(`passed=64 failed=0` 유지).
- [`PGObjectHandoff_2026-09-17.md`](PGObjectHandoff_2026-09-17.md) — 팀 캐릭터가 오브젝트·탈것과 만나는 인터페이스 4함수.
- [`ItemGrades_2026-09-22.md`](ItemGrades_2026-09-22.md) — 아이템 등급·값 표의 근거. 데이터: `DT_PGItemValue.csv`, `DT_PGObjectCatalog.csv`, `DT_PGLootTables.csv`(에셋의 원본, `Tools/wbp/make_object_tables.py` 로 가져온다).
- [`DebrisOptimization_2026-09-20.md`](DebrisOptimization_2026-09-20.md) — 잔해·부수기 최적화 면접 설명(9/28 멀티·조명 추가).
- [`MonsterAIBenchmark_2026-09-23.md`](MonsterAIBenchmark_2026-09-23.md) — 몬스터 AI 코드 방식 vs StateTree 측정.
- [`FinalePlan_2026-09-18.md`](FinalePlan_2026-09-18.md) — 결말 연출(배·드래곤) 기획.
- [`WwiseSoundGuide_2026-09-23.md`](WwiseSoundGuide_2026-09-23.md), [`TankFxCandidates.md`](TankFxCandidates.md) — 소리·이펙트 붙이는 법과 자리.
- [`Credits.md`](Credits.md) — 외부 에셋 출처.
- [`Guide/`](Guide/) — 코드 공부용(읽는 순서·용어·구조 지도·함수 설명·협업). [`UE5_자주쓰는API.md`](UE5_자주쓰는API.md).
- [`Diagrams/`](Diagrams/) — 클래스 지도, 맵 생성 코드 사전(HTML).
- `프로젝트 기획 v0.4.pdf` — 팀 기획서.

## 절대 작업 규칙

- 수정 대상은 집 PC `E:\ProjectTest2`(학원 PC `D:\ProjectTest2`)뿐이다. 브랜치 `team-merge`.
- 팀 저장소 `E:\TestProject2`와 팀원 코드는 수정하지 않는다(사용자가 허락한 `[멀티 임시수정 …]` 표시 구간만 예외).
- 작업 규칙 전체는 루트의 `CLAUDE.md` / `AGENTS.md`(같은 내용)에 있다.
- 파일이나 폴더를 삭제하기 전에는 대상과 이유를 사용자에게 설명하고 승인을 받는다.
- 무엇이든 "됐다" 는 전용 서버 + 접속자 시험(`Tools/wbp/headless_multi.ps1 -Dedicated -Clients 2`)을 통과한 뒤에 말한다.

- 성능: [`Perf4060_2026-09-28.md`](Perf4060_2026-09-28.md) — RTX 4060 8GB 기준 장면별 측정(fps·GPU·VRAM), 고친 것, 다시 재는 법.
