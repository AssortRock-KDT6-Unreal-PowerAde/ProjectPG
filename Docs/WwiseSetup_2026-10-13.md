# Wwise 도입 기록 (2026-10-13)

작업 브랜치: `wwise-소리-붙이기` (`기획서-화면-붙이기`에서 갈라짐)

## 1. 현재 상태

- Wwise 버전: `2025.1.11.9262` (통합 빌드 `.4509`), 언리얼 5.6에서 빌드·재생 확인
- 버튼을 누르면 클릭 소리가 남 (타이틀·로비·인벤토리에서 확인)
  - 빈 배경 클릭, 에디터 쪽 버튼, 아이템 집기에서는 소리가 안 남 (의도대로)
- 아직 확인 못 한 것: 서버 타깃 빌드, 패키징된 게임에서의 재생

## 2. 다른 PC에서 이어서 하려면

git에는 코드·설정·뱅크만 있다. 아래는 따로 가져와야 한다.

| 필요한 것 | 어디서 | 어디에 |
| --- | --- | --- |
| Wwise 플러그인 폴더 4개 (`Wwise`, `WwiseSoundEngine`, `WwiseNiagara`, `WwiseMotionOpenXRHelpers`) | 구글 드라이브 (또는 Audiokinetic Launcher에서 2025.1.11.9262로 Integrate) | `ProjectPG/Plugins/` |
| Wwise 오서링 툴 2025.1.11.9262 | Audiokinetic Launcher | 소리를 만질 PC에만 |
| 효과음 원본 | 구글 드라이브 | `ProjectPG_WwiseProject/Originals/` |
| 효과음 후보 (`PG_Candidates`) | 구글 드라이브 | 아무 곳 |

- 플러그인이 없으면 빌드가 깨진다 (`AkAudio` 모듈을 못 찾음)
- 게임만 돌리는 팀원은 플러그인만 있으면 된다. 원본·오서링 툴은 필요 없다

## 3. 팀 공유 방식 (결정)

| 대상 | git | 구글 드라이브 |
| --- | --- | --- |
| Wwise 플러그인 (`Plugins/Wwise*` 폴더 4개) | **gitignore** (6GB, 100MB 넘는 파일이 있어 GitHub에 못 올림) | 공유 → 각자 `Plugins/`에 풀기 |
| Wwise 프로젝트 파일 (`.wproj`, Events 등) | 커밋 | - |
| 원본 효과음 (`ProjectPG_WwiseProject/Originals`) | **gitignore** (저장소가 공개라 Sonniss 원본을 올리면 안 됨) | 공유 |
| 생성된 SoundBank (`ProjectPG_WwiseProject/GeneratedSoundBanks`) | **커밋** | - |
| Wwise 이벤트 에셋 (`Content/WwiseAudio`) | 커밋 (`.gitignore`에 예외 추가함) | - |

- 이 브랜치를 main에 합치기 전에 팀원 전원이 플러그인을 받아 둬야 한다
- 라이선스: `K_` / Kenney = CC0, 제약 없음. `S_` = Sonniss, 게임에 쓰는 건 되지만 원본 재배포 금지

## 4. 코드 구조

- `Source/ProjectPG/Public/Core/SoundSubSystem.h` — 소리 창구 (GameInstanceSubsystem)
  - `PlaySound2D(이름)` / `PlaySoundAtLocation(이름, 위치)` / `PlaySoundOnActor(이름, 액터)`
  - 블루프린트에서도 `Get SoundSubSystem` 노드로 부를 수 있다
  - 전용 서버에서는 소리를 내지 않는다
  - 부른 컴퓨터에서만 소리가 난다. 남도 들어야 하는 소리는 모든 클라이언트에서 불리는 자리에 넣을 것
- `Source/ProjectPG/Public/Common/SoundData.h` — 소리 표의 한 줄 (`FSoundTableRow`, 칸은 `Event` 하나)
- `Content/PG/Table/SoundTable` — 소리 이름 → Wwise 이벤트. `TableLoader`에 등록돼 있다
- 버튼 클릭음은 `SoundSubSystem.cpp` 안의 `FUIClickSoundProcessor`가 자동으로 낸다
  - 마우스 왼쪽 버튼이 눌릴 때, 마우스 밑에 게임 화면의 버튼(`SButton`)이 있으면 `UI_Click` 재생
  - 버튼마다 코드를 넣지 않는다

## 5. 소리 하나 추가하는 순서

1. Wwise에서 원본을 임포트하고 Event를 만든다 (예: `Play_UI_Hover`)
2. 그 Event를 SoundBank(`UI`)에 넣고 뱅크를 생성한다
3. 언리얼 Wwise Browser에서 이벤트 에셋을 `Content/WwiseAudio`로 가져온다
4. `SoundTable`에 줄 추가: 줄 이름 `UI_Hover`, `Event` 칸에 `Play_UI_Hover`
5. 코드에서 `USoundSubSystem::Get(this)->PlaySound2D(TEXT("UI_Hover"));`

## 6. TODO

- [x] 플러그인 빌드 확인, 첫 이벤트(`Play_UI_Click`)와 뱅크 생성
- [x] `.gitignore` 확정, 소리 창구·소리 표·버튼 클릭음 커밋
- [ ] 드라이브에 플러그인·원본·후보 올리고 팀원에게 공유 (Wwise 버전, 넣을 경로)
- [ ] UI 소리 나머지: 마우스 올리기, 창 열기/닫기, 뒤로가기, 아이템 집기/놓기, 못 놓는 자리
  - 마우스 올리기는 `FUIClickSoundProcessor`에 추가, 나머지는 해당 코드 자리에서 `PlaySound2D` 호출
  - 팀원 파일(인벤토리 = wlghks0723)을 고쳐야 하면 먼저 말하기
- [ ] 서버 타깃 빌드 확인 (서버 빌드하는 팀원에게 부탁)
- [ ] 발소리·재장전은 애니메이션 노티파이로, 배경음은 레벨 배치로
- [ ] 아직 없는 소리: 재장전, 총 꺼내기, 빈 탄창, 권총 발사, 소모품, 숲·호수 배경음, 타이틀 음악
