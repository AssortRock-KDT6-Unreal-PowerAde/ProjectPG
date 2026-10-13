# Wwise 도입 기록 (2026-10-13)

## 1. 현재 상태

- Wwise 버전: `2025.1.11.9262` (통합 빌드 `.4509`), 언리얼 5.6
- Audiokinetic Launcher에서 Integrate 완료
  - `Plugins/Wwise` 등 Wwise 플러그인 폴더 생성됨 (Plugins/Wwise, WwiseNiagara, WwiseSoundEngine, WwiseMotionOpenXRHelpers)
  - Wwise 프로젝트: `ProjectPG_WwiseProject/ProjectPG_WwiseProject.wproj`
  - `Config/DefaultGame.ini`가 Wwise 때문에 수정됨 → 확인 후 커밋
- 런처에서 다시 열면 "UPGRADE WWISE IN PROJECTPG"로 뜸. 이미 설치된 상태라 같은 버전이면 아무것도 안 눌러도 됨 (Cancel).
  - `Modify Wwise` = Wwise에 플랫폼 추가 설치 (윈도우만 쓰므로 불필요)
  - `Add Plug-ins to Wwise` = 효과 플러그인 추가 설치 (지금은 불필요)
- 팀원 모두 같은 Wwise 버전(2025.1.11.9262) 설치 필요

## 2. 효과음 원본

- 위치: `D:\PG_Candidates` (346파일, 242MB, 26개 폴더, README.md에 출처 목록)
- 접두사 `S_` = Sonniss GDC 번들, `K_` = Kenney (CC0)
- 소리마다 후보가 3~14개 → **다 넣지 말고 1~3개만 골라서** Wwise로 임포트
  - 발소리·총소리처럼 여러 개를 섞을 소리는 Random Container 사용
- 라이선스
  - Kenney: CC0, 제약 없음
  - Sonniss: 게임에 사용 OK, **원본 파일 재배포 금지** → 공개 repo에 올리면 안 됨
  - 이 프로젝트는 포트폴리오 용도, 팀원끼리 구글 드라이브로 공유

## 3. 팀 공유 방식 (결정)

| 대상 | git | 구글 드라이브 |
| --- | --- | --- |
| Wwise 플러그인 (`Plugins/Wwise*` 폴더 4개) | **gitignore** (6GB, 100MB 넘는 파일이 있어 GitHub에 못 올림) | 공유 → 각자 `Plugins/`에 풀기 |
| Wwise 프로젝트 파일 (`.wproj`, Events 등) | 커밋 | - |
| 원본 효과음 (`ProjectPG_WwiseProject/Originals`) | **gitignore** | 공유 |
| 생성된 SoundBank | **커밋** | - |

- 소리를 만지는 사람(Wwise 오서링 툴 사용): 드라이브에서 원본을 받아 `ProjectPG_WwiseProject/Originals`에 넣음
- 게임만 돌리는 팀원: 원본 불필요. SoundBank만 있으면 소리가 남
- 원본을 ignore하면 팀원 쪽에서 뱅크를 새로 못 만드니, 뱅크는 반드시 커밋해야 함

## 4. 주의: 기존 .gitignore

- 현재 `.gitignore`에 `Content/*` + `!Content/PG`가 있음
- Wwise 통합 설정(`Config/DefaultGame.ini`의 `AkSettings`)상 두 군데가 중요함
  - `DefaultAssetCreationPath=/Game/WwiseAudio` → Wwise Event 등의 언리얼 에셋이 `Content/WwiseAudio`에 생김. 지금 규칙으로는 **커밋되지 않으므로** `!Content/WwiseAudio` 예외 필요
  - 생성된 SoundBank는 Wwise 프로젝트 폴더 안의 `GeneratedSoundbanks`(기본값)에 생길 가능성이 큼. 뱅크를 처음 생성한 뒤 실제 경로 확인 필요 (`RootOutputPath`는 현재 비어 있음)
- 처음 생성 전에는 Wwise Browser에 "Root Output Path is empty" 안내가 뜸 → 정상

## 5. TODO

- [ ] 언리얼 에디터 열어서 Wwise 플러그인 빌드 / 메뉴 확인
- [ ] 후보에서 효과음 골라 Wwise로 임포트 (`D:\PG_Candidates`)
- [ ] 효과음별 Event 만들고 SoundBank 한 번 생성
- [ ] 생성된 폴더 구조 확인 후 `.gitignore` 확정 (Originals ignore, Content/WwiseAudio 예외)
- [ ] `ProjectPG_WwiseProject/`, `Config/DefaultGame.ini` 커밋 (`Plugins/Wwise*`는 드라이브로 공유)
- [ ] 팀원에게 공유: Wwise 버전, 드라이브 링크, 원본 넣을 경로
