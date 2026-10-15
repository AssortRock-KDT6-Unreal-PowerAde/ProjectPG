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

- 10-15부터 원본이 `Originals/SFX/SFX/` 안에도 들어감 (WAAPI 임포트가 한 칸 더 만듦). 원본 폴더는 통째로 옮기면 된다
- 집 PC에서 이어 하기: 학원 PC에서 `git push` → 집에서 `git pull` 후 위 표대로 플러그인·원본을 넣는다. 게임만 돌려 볼 거면 플러그인만 있으면 된다 (뱅크는 git에 있음)
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

## 6. 진행 상황 (2026-10-15)

- UI 소리 7개 완료: `UI_Click`, `UI_Hover`(버튼 감시자), `UI_Open`/`UI_Close`(UIManagerSubSystem), `UI_ItemPick`/`UI_ItemDrop`/`UI_ItemFail`(ItemWidget, EquipSlot, InventoryGridWidget)
  - 팀원 파일에는 `USoundSubSystem::PlayUISound(this, TEXT("이름"))` 한 줄씩만 들어감
- 총소리 3개 Wwise 완료(`Weapon` 뱅크): `Play_Weapon_Fire_Pistol` / `_Rifle` / `_Shotgun`
  - 샷건은 Blend Container 4겹, 재생 제한은 상자(부모)에 건다
- **남은 일: 총소리를 게임에 연결**
  - 사격 코드(`ACustomCharacter::Fire`)는 비어 있고, 사격은 몽타주만 재생한다. 몽타주는 `OnRep_PlayMontage`(NetMulticast)로 모든 클라이언트에서 재생됨
  - 그래서 사격 몽타주 10개(`Content/PG/Animations/Montages`, `.../Meta/Montages`의 `AM_Fire_*`)에 Wwise 플러그인의 `AnimNotify_AkEvent` 노티파이를 꽂는다
  - 노티파이의 **Follow는 끈다**. 켜 두면 캐릭터에 AkComponent가 없어서 화면에 노란 경고 글자가 뜬다
  - **10-15 완료**: 10개 모두 `Sound` 트랙 0초에 꽂음 (권총→`_Pistol`, 라이플 Hip/Ironsights→`_Rifle`, 샷건→`_Shotgun`, Follow 끔). 아직 PIE에서 귀로 확인 안 함
  - 메뉴 (공식 문서 "Triggering Wwise Events in Animation Sequences"): Notifies 트랙 오른쪽 클릭 → Add notify → AkEvent
  - 에디터가 켜진 채로 바꾸려면: 헤드리스 에디터(`UnrealEditor-Cmd.exe ... -run=pythonscript -script=...`)로 `unreal.AnimationLibrary.add_animation_notify_event` 사용. 커맨드릿에선 `load_blueprint_class`가 안 되니 `load_asset("/Wwise/AnimNotify_AkEvent").generated_class()`로 클래스를 얻는다
- Wwise 오서링 툴은 WAAPI(`http://127.0.0.1:8090/waapi`)로 바깥에서 조종할 수 있다 (켜져 있을 때)

### 10-15 저녁: 남은 소리 한꺼번에 (Claude가 고른 임시 소리, 귀로는 아직 확인 안 함)

| 소리 | Wwise | 원본 (`D:\PG_Candidates`) | 게임에 붙인 곳 |
| --- | --- | --- | --- |
| 총 꺼내기 | `Weapon\Weapon_Equip` (-4dB) | `Container_Open\S_Latchlocker,cable,steel...alt2` | `AM_Equip_*` 6개, 길이의 30% 지점 |
| 탄창 빼기 | `Weapon\Weapon_Reload_MagOut` (-2dB) | `Container_Open\K_metalLatch` | `AM_Reload_*` 10개, 25% 지점 |
| 탄창 끼우기 | `Weapon\Weapon_Reload_MagIn` (-2dB) | `Container_Open\S_Latchlocker,aluminum...over` | `AM_Reload_*` 10개, 60% 지점 |
| 발소리 | `Player\Player_Footstep` 랜덤 5개, 피치 ±100 (-6dB) | `Footstep_Grass` 5개 | 걷기·조깅·달리기 시퀀스 18개 |
| 앉아 걷기 | `Player\Player_Footstep_Crouch` (-14dB) | 위와 같음 | 앉아 걷기 시퀀스 12개 |
| 착지 | `Player\Player_Land` 랜덤 5개 (-3dB) | `Land` 5개 | `*_Land` 시퀀스 5개, 0.02초 |
| 상자 열기 | `World\World_Container_Open` (-3dB) | `Container_Open\S_Latchlocker,design,FutureLoot...` | `AInteractActor::Interact` 상자 쪽 → 소리 표 `World_Container_Open` |

- 거리 감쇠(Attenuation) 3개 새로 만듦: `Att_Weapon` 150m, `Att_Foley` 25m, `Att_World` 20m. 총소리 3개에도 `Att_Weapon`을 걸었음 (이전엔 멀리서 쏴도 바로 옆처럼 들렸음)
- 뱅크: `Weapon`에 3개 추가, `Player`·`World` 새로 만듦
- 발소리 위치는 `foot_l`/`foot_r` 뼈 높이가 땅 근처로 내려오는 순간을 계산해서 꽂음. 어긋나면 애니메이션 열어서 노티파이를 끌어서 옮기면 됨
- 상자 소리는 여는 사람만 들린다 (상자 열기가 그 컴퓨터에서만 돌기 때문)
- 쓴 스크립트는 `Tools/Sound/`에 있음
  - `build_wwise.py`: WAAPI로 Wwise에 소리·이벤트·뱅크를 만들고 뱅크까지 생성 (Wwise를 켜 둔 채로 실행)
  - 언리얼 이벤트 에셋 만들기: `UnrealEditor-Cmd.exe ProjectPG.uproject -run=WwiseReconcileCommandlet -modes=create` (에디터를 끈 채로)
  - `add_anim_sounds.py`, `add_fire_sounds.py`, `add_sound_rows.py`: 노티파이·소리 표 (`-run=pythonscript -script=경로`)
- **게임에 기능이 없어서 못 붙인 것**: 빈 탄창(탄약 없음), 소모품 사용(사용 기능 주석 처리됨), 몬스터 공격·죽음, 문, 폭발, 땅 꺼짐, 엔딩(우주선·용·경보). 후보 소리는 `D:\PG_Candidates`에 준비돼 있음
- **소리 원본이 없어서 못 한 것**: 타이틀·로비 음악, 숲·호수 배경음 (후보 폴더에 없음)
- 팀원들이 사운드 작업은 전부 맡김 (소리용 수정은 허락 불필요)
- Wwise 메뉴 위치는 공식 문서로 확인할 것: https://www.audiokinetic.com/ko/public-library/2025.1.11_9262/?source=Help&id=welcome_to_wwise (처음 열 때 캡차가 뜨면 사용자가 직접 풀어야 함)

## 7. TODO

- [x] 플러그인 빌드 확인, 첫 이벤트(`Play_UI_Click`)와 뱅크 생성
- [x] `.gitignore` 확정, 소리 창구·소리 표·버튼 클릭음 커밋
- [ ] 드라이브에 플러그인·원본·후보 올리고 팀원에게 공유 (Wwise 버전, 넣을 경로)
- [ ] UI 소리 나머지: 마우스 올리기, 창 열기/닫기, 뒤로가기, 아이템 집기/놓기, 못 놓는 자리
  - 마우스 올리기는 `FUIClickSoundProcessor`에 추가, 나머지는 해당 코드 자리에서 `PlaySound2D` 호출
  - 팀원 파일(인벤토리 = wlghks0723)을 고쳐야 하면 먼저 말하기
- [ ] 서버 타깃 빌드 확인 (서버 빌드하는 팀원에게 부탁)
- [x] 발소리·재장전·총 꺼내기·착지·상자 열기 (10-15, 임시 소리)
- [ ] PIE에서 귀로 확인: 볼륨, 발소리 타이밍, 거리 감쇠
- [ ] 바닥별 발소리 (Switch `Surface`) — 지금은 풀밭 소리 하나
- [ ] 아직 없는 소리: 빈 탄창, 소모품, 숲·호수 배경음, 타이틀 음악 (기능 또는 원본이 생기면)
