# 와이즈(Wwise)로 소리 넣기 — 만드는 법과 코드 붙이는 자리 (2026-09-23)

> **지금 상태**: 소리 코드는 **하나도 없다**(일부러 비워 둠, 9/18 결정). 와이즈 플러그인도 아직 안 깔려 있다.
> 이 문서는 "나중에 소리를 넣을 때 무엇을, 어디에, 어떤 순서로" 만 적는다. 이 문서대로 하기 전까지는 코드를 바꾸지 않는다.

## ★ 9/23 밤 상태 — 코드 쪽은 끝났다. 남은 건 에디터 작업뿐 (C++ 안 고쳐도 됨)

코드가 이미 "소리 신호"를 보낸다. 신호 이름(Cue)과 붙은 자리:

| Cue 이름 | 언제 | 어디서(코드) | 누구에게 |
|---|---|---|---|
| `Weapon_Fire` | 총·무기 발사 | `PGWeaponComponent::PerformAttack` | 모두 (서버 → Multicast) |
| `WigBeam_Fire` / `WigBeam_Impact` | 가발 광선 발사 / 멈춘 자리 | `PGWigBeamComponent::MulticastBeam` | 모두 (이미 Multicast) |
| `Monster_Attack` | 몬스터 공격 시작 | `MonsterAIController::TryAttackOrSmash` | 모두 |
| `Monster_Death` | 몬스터 죽음 | `PGMonsterCharacter::Die` | 모두 |
| `Door_Open` / `Door_Close` | 문 | `PGDoorActor::SetOpen` | 모두 |
| `Container_Open` | 상자 뚜껑 | `ItemContainerActor::OnRep_IsOpen` | 각자 |
| `Explosion_Barrel` | 연료통 폭발 | `PGFloorItemActor::Explode` | 모두 |
| `Ground_Collapse` | 땅 무너짐(먼지와 같은 간격 제한) | `WarZoneFootprintPreview::SpawnCollapseDust` | 각자 |
| `Finale_<단계>` | 결말 단계 바뀜 (`Finale_Warning`, `Finale_Arrival`, `Finale_Hover`, `Finale_Launch`, `Finale_Dragon`, `Finale_Victory` …) | `PGFinaleDirector::EnterState` | 모두 |
| `UI_Announce` | 화면 안내 문구 | `PGAnnounceSubsystem::Announce` | 나만 |

신호는 `APGSoundRouter`(Source/…/Common/PGSoundRouter.h)가 받는다. 지금은 C++ 기본 담당이라 **아무 소리도 안 난다**(신호만 지나감).
신호가 지나가는지 보기: 콘솔 `Log LogPGObjects Verbose` → 로그에 `PGSound: Weapon_Fire at …`.

**혼자 하는 순서 (에디터 작업만)**
1. 와이즈 설치·프로젝트 연결 (아래 1장). `Build.cs` 에 `"AkAudio"` 를 넣을 필요 **없다** — 코드가 와이즈를 직접 부르지 않는다.
2. 와이즈에서 이벤트 만들기: 이름을 위 표의 Cue 와 같게(`Play_Weapon_Fire` 등, 2장).
3. 언리얼에서 데이터 테이블 만들기: 우클릭 > Miscellaneous > Data Table > 행 구조 **`PGSoundCueRow`** → 이름 `DT_PGSounds`. 행 이름 = Cue 이름(`Weapon_Fire`), `Sound` 칸에 와이즈 이벤트 에셋, 따라 움직이는 소리면 `bAttachToSource` 체크.
4. 블루프린트 만들기: 부모 클래스 **`PGSoundRouter`** → `BP_PGSoundRouter`. 클래스 기본값 `Cue Table` 에 `DT_PGSounds`.
5. `BP_PGSoundRouter` 이벤트 그래프에서 **Event Play Cue** 구현:
   `Get Data Table Row (DT_PGSounds, Row Name = Cue)` → 찾으면 `Sound` 를 **Ak Audio Event** 로 Cast →
   `bAttachToSource` 가 참이고 `Source` 가 있으면 **Post Event (Actor = Source)**, 아니면 **Post Event At Location (Location)**.
   (노드 이름은 와이즈 버전마다 조금 다르다 — Post Event 계열을 찾으면 된다.)
6. 프로젝트 설정 > ProjectPG Visuals > Actors > **Sound Router Class** 에 `BP_PGSoundRouter`.
7. PIE 로 총 쏴 보기. 안 들리면 로그의 `PGSound:` 줄이 있는지(신호 문제) / 없는지(와이즈 쪽 문제)로 가른다.

**멀티 메모**: 서버가 판정하고 모두에게 신호를 보낸다(`PGSound::PlayAll`) — 소리 파일은 각자 컴퓨터에 있다.
쏜 본인은 서버를 한 번 갔다 오는 만큼 늦게 들릴 수 있다 → 멀티를 붙일 때 "쏜 사람은 자기 화면에서 바로(`PlayLocal`), 서버 신호는 쏜 사람 빼고" 로 바꾼다.
새 소리 자리를 늘릴 때: 서버에서만 도는 함수면 `PGSound::PlayAll`, 이미 모두에게서 도는 곳(Multicast·OnRep)·내 화면 UI 면 `PGSound::PlayLocal` 한 줄.

아래 1~7장은 처음 설계 때 쓴 설명이다(설치·와이즈 사용법·가벼움 규칙은 그대로 유효). 4장의 "C++ 에서 와이즈 직접 부르기" 대신 위 방식(신호 + BP 담당)을 쓴다.

## 한 줄 요약

**와이즈에서 소리를 "이벤트(Play_…)" 로 만든다 → 언리얼로 가져오면 이벤트 에셋이 생긴다 → 그 에셋을 BP 나 데이터 에셋 칸에 끼운다 → C++ 는 "지금 그 칸을 울려라" 한 줄만 부른다.**
그림을 BP/DA 로 뺀 것과 똑같은 방식이다. C++ 에 소리 파일 경로를 적지 않는다.

---

## 1. 설치 (한 번만)

1. **Audiokinetic Launcher** 를 설치한다(와이즈 공식 설치 프로그램, 무료 계정 필요).
2. Launcher 에서 와이즈 본체를 받는다. 버전은 Launcher 의 Unreal 탭에서 **UE 5.6 을 지원한다고 나오는 것**을 고른다(버전마다 지원하는 언리얼이 다르다 — 설치 전에 꼭 확인).
3. Launcher > Unreal Engine 탭 > 이 프로젝트(`E:\ProjectTest2\ProjectPG.uproject`) 를 골라 **Integrate Wwise into Project**.
   - 그러면 `Plugins/Wwise/` 폴더와 와이즈 프로젝트 폴더(`ProjectPG_WwiseProject/` 같은 이름)가 생긴다.
   - `.uproject` 에 Wwise 플러그인이 켜진다.
4. **팀원도 같은 버전**을 깔아야 한다. 버전이 다르면 사운드뱅크가 안 맞는다. 쓴 버전을 이 문서 맨 아래 "기록" 칸에 적어 둘 것.
5. `.gitignore` 는 맨 위가 "허용 목록" 방식이다. 와이즈 프로젝트 폴더와 `Plugins/Wwise` 를 올릴지 팀과 정한다(플러그인은 크다 — 각자 Launcher 로 까는 쪽을 권장, 와이즈 프로젝트 폴더와 이벤트 에셋만 올린다).

> 에디터를 끈 상태에서 한다. 설치 후 한 번 빌드(`Build.bat`) 하고 에디터를 다시 연다.

## 2. 와이즈에서 소리 만들기

와이즈 프로그램(Wwise Authoring) 에서:

| 순서 | 할 일 | 비유 |
|---|---|---|
| 1 | 녹음 파일(.wav) 을 **Actor-Mixer Hierarchy** 로 끌어다 놓는다 → "Sound SFX" 가 생긴다 | 재료 |
| 2 | 소리 여러 개를 번갈아 내고 싶으면 **Random Container** 로 묶는다(총소리·발소리는 꼭 — 똑같은 소리 반복은 티가 난다) | 재료 섞기 |
| 3 | 거리에 따라 작아지게 **Attenuation(감쇠)** 을 건다. 3D 소리는 전부 필요하다 | 멀면 작게 |
| 4 | **Event** 를 만든다: 이름은 `Play_무엇` (예: `Play_Weapon_Fire`), 안에 "Play → 1번 소리". 끄는 소리가 필요하면 `Stop_무엇` | 버튼 |
| 5 | **SoundBank** 를 만들어 이벤트를 넣고 **Generate** (요즘 버전은 자동 뱅크가 있어 이 단계가 거의 없다) | 포장 |

**이름 규칙** (코드·에셋·와이즈가 같은 이름을 쓴다):
`Play_분류_무엇` — 분류는 `Weapon`, `WigBeam`, `Explosion`, `Debris`, `Vehicle`, `Tank`, `FlightKit`, `Robot`, `Ship`, `Dragon`, `Monster`, `Door`, `Loot`, `Booth`, `Extract`, `UI`, `Amb`(주변 소리), `Music`.

## 3. 언리얼로 가져오기

1. 언리얼 에디터 > 창 > **Wwise Browser**(버전에 따라 "WAAPI Picker") 를 연다.
2. 와이즈에서 만든 이벤트를 **`/Game/PG/Audio/Events/`** 폴더로 끌어다 놓는다 → `Play_Weapon_Fire` 같은 **이벤트 에셋**이 생긴다.
3. 소리를 바꾸고 싶으면 와이즈에서 고치고 다시 Generate 만 하면 된다. 언리얼 쪽 에셋·코드는 그대로.

## 4. 코드·BP 에 붙이기 — 원칙 3개

### 원칙 1: 소리 에셋은 C++ 에 적지 않는다 (그림과 같은 규칙)

그림을 뺄 때와 똑같이 "칸" 만 만들고 에셋은 에디터에서 끼운다.

| 소리 나는 쪽 | 칸을 어디에 | 이유 |
|---|---|---|
| BP 자식이 있는 액터(전함 BP, 문, 상자, 부스 …) | **그 BP 에서 직접** `Post Event` 노드 | C++ 를 안 건드려도 된다 |
| 서브시스템(미사일 `UPGMissileSubsystem`, 잔해 `UPGDebrisSubsystem`) | 새 데이터 에셋 **`DA_PGSounds`** (아래) | BP 자식을 만들 수 없는 곳 — `UPGEffectSet`(DA_PGEffects) 를 만든 이유와 같다 |
| 무기 | 무기 표 **`DT_PGWeapons`** 행(`FPGWeaponDef`)에 `FireSound`, `ImpactSound` 칸 추가 | 무기마다 소리가 다르다 |
| 애니메이션에 맞춰(발소리, 용 울음, 날갯짓) | 애니메이션 에셋의 **노티파이**(애니메이션 시간줄에 찍는 표시) 에서 `Post Event` | 코드 없이 정확한 박자 |

`DA_PGSounds` 모양(나중에 만들 것 — 지금 `PGEffectSet.h` 를 그대로 본뜬다):

```cpp
// 여러 곳이 같이 쓰는 소리(데이터 에셋 DA_PGSounds). 서브시스템은 BP 자식이 없어서 여기에 모은다.
UCLASS(BlueprintType)
class PROJECTPG_API UPGSoundSet : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    static const UPGSoundSet* GetActive();   // 설정(ProjectPG Visuals > Sound Set) 에 끼운 에셋

    UPROPERTY(EditAnywhere, Category = "PG|Debris")    TSoftObjectPtr<UAkAudioEvent> DebrisImpact;
    UPROPERTY(EditAnywhere, Category = "PG|Missile")   TSoftObjectPtr<UAkAudioEvent> MissileLaunch;
    UPROPERTY(EditAnywhere, Category = "PG|Missile")   TSoftObjectPtr<UAkAudioEvent> MissileExplode;
    UPROPERTY(EditAnywhere, Category = "PG|Collapse")  TSoftObjectPtr<UAkAudioEvent> GroundCollapse;
    UPROPERTY(EditAnywhere, Category = "PG|UI")        TSoftObjectPtr<UAkAudioEvent> Announce;
    // …
};
```

### 원칙 2: 와이즈를 부르는 곳은 한 군데로 모은다

C++ 여기저기서 와이즈 함수를 직접 부르지 않고, 도우미 함수 하나(`PGSound::Play`) 만 부른다.
- 와이즈 버전이 바뀌어 함수 모양이 달라져도 **한 파일만** 고치면 된다.
- 소리 에셋이 비어 있으면 조용히 넘어가게 여기서 한 번만 검사한다(지금처럼 소리 없이도 게임이 돈다).

```cpp
// Common/PGSound.h — 소리를 내는 유일한 입구.
namespace PGSound
{
    // 액터에 붙여서 낸다(따라 움직이는 소리: 엔진, 날갯짓).
    void PlayOn(const TSoftObjectPtr<UAkAudioEvent>& Event, AActor* Actor);
    // 그 자리에서 한 번 낸다(폭발, 부딪힘).
    void PlayAt(const UObject* WorldContext, const TSoftObjectPtr<UAkAudioEvent>& Event, const FVector& Location);
}
```

안에서는 와이즈의 `UAkGameplayStatics::PostEvent`(액터에 붙여 내기 — 액터에 소리 부품이 없으면 알아서 붙인다) 와 `PostEventAtLocation`(자리에서 내기) 을 쓴다.
**정확한 함수 인자는 설치한 와이즈 버전의 문서를 보고 맞춘다**(버전마다 조금씩 다르다).

`ProjectPG.Build.cs` 의 `PublicDependencyModuleNames` 에 `"AkAudio"` 를 추가해야 이 코드가 빌드된다.
(와이즈 버전에 따라 `"WwiseSoundEngine"` 도 필요하다는 안내가 나오면 같이 넣는다.)

### 원칙 3: 멀티플레이 — 소리는 **모든 사람 화면에서** 내야 한다

이 게임은 서버가 판정하고 결과를 나눠 주는 구조다. 그래서 **서버에서만 도는 함수에 소리를 넣으면 다른 사람은 못 듣는다.**

| 소리를 넣는 곳 | 되나 |
|---|---|
| `Multicast…` 함수 (모두에게 보내는 함수) | ✅ 예: `UPGWigBeamComponent::MulticastBeam`, `APGTankPawn::MulticastShotFx` |
| `OnRep_…` 함수 (값이 바뀐 걸 받은 쪽에서 도는 함수) | ✅ 예: `APGFinaleDirector::OnRep_State`, `APGMonsterCharacter::OnRep_Dead`, `APGDoorActor::OnRep_IsOpen` |
| 서버 판정 함수 (`PerformAttack`, `TakeDamage` 안쪽) | ❌ 여기서 부르면 서버 혼자 듣는다 → Multicast 하나를 새로 만들어 거기서 낸다 |
| UI(내 화면에만 뜨는 것) | ✅ 내 화면만 들리면 되니 그 자리에서 바로 |

> **총소리가 대표적 함정**: `UPGWeaponComponent::PerformAttack` 은 서버에서만 돈다. `MulticastBeam` 을 본떠 `MulticastFireFx` 를 하나 만들고 거기서 소리·총구 불꽃을 같이 낸다.

---

## 5. 소리가 필요한 순간과 붙일 자리

**먼저 할 것 15개** (★ = 없으면 게임이 어색한 것). 줄 번호는 2026-09-23 기준이라 조금 밀릴 수 있다 — 함수 이름으로 찾을 것.

| ★ | 순간 | 붙일 자리 (Source/ProjectPG/Private/) | 이미 있는 연결 | 이벤트 이름 |
|---|---|---|---|---|
| ★ | 총 쏘기 | `Weapons/PGWeaponComponent.cpp` `PerformAttack` (서버 전용!) | 없음 → Multicast 새로 | `Play_Weapon_Fire` |
| ★ | 탄 없음 | 같은 파일 `PerformAttack` 앞부분 빈 탄 검사 | 없음 (쏜 사람만 들리면 됨) | `Play_Weapon_DryFire` |
| ★ | 가발 광선 발사·맞음 | `Objects/PGWigBeamComponent.cpp` `MulticastBeam_Implementation` | 이펙트 나오는 자리 그대로 | `Play_WigBeam_Fire`, `_Impact` |
| ★ | 발소리·착지 | C++ 자리 없음 → 캐릭터 걷기 애니메이션 **노티파이** | AnimBP | `Play_Footstep`, `Play_Land` |
| ★ | 문 열고 닫기 | `Objects/PGDoorActor.cpp` `OnRep_IsOpen` | `OnDoorUsed` (BP 에서 바로 가능) | `Play_Door_Open/Close` |
| ★ | 상자 열기·줍기 | `Actors/ItemContainerActor.cpp` `OnRep_IsOpen`, `Objects/PGLootableComponent.cpp` `TakeItem` | `OnLooted` (BP) | `Play_Container_Open`, `Play_Loot_Take` |
| ★ | 몬스터 공격·아픔·죽음 | `Monster/PGMonsterCharacter.cpp` `TryAttack`, `OnRep_Dead` | 공격 애니메이션(노티파이) | `Play_Monster_Attack/Hurt/Death` |
| ★ | 폭발(통·포탄·미사일) | `Objects/PGFloorItemActor.cpp` `Explode`, `Finale/PGMissileSubsystem.cpp` `StartExplosion` | 흙먼지 이펙트 자리 | `Play_Explosion_*` |
| ★ | 잔해 떨어짐 | `Common/PGDebrisSubsystem.cpp` `PlayDust` | **이미 0.12초 간격 제한이 있음** → 소리도 여기 붙이면 한꺼번에 수백 개 울리는 일이 없다 | `Play_Debris_Impact` |
| ★ | 차 엔진(속도 따라 음 높이) | `Vehicle/PGVehiclePawn.cpp` 탈 때 `Mount` 에서 켜고 `Dismount` 에서 끔, 속도는 `Tick` 에서 RTPC(와이즈 쪽 숫자 손잡이) 로 | `UPGSeatComponent::OnEntered` | `Play_Vehicle_Engine` |
| ★ | 전함 문 | 전함 BP 의 `CloseRearDoor`/`OpenRearDoor` 이벤트 | **BP 에 이미 있음** → 노드 하나만 추가 | `Play_Ship_Door_Close/Open` |
| ★ | 용 울음·날갯짓·불 뿜기 | 애니메이션 노티파이 + `Finale/PGDragonBoss.cpp` `SetBreathFx` (불 켜고 끌 때) | 애니메이션·이펙트 | `Play_Dragon_Roar/Wings/Breath` |
| ★ | 땅 무너짐 | `Actors/WarZoneFootprintPreview.cpp` `SpawnCollapseDust` | 먼지 이펙트 자리 | `Play_Ground_Collapse` |
| ★ | 결말 단계(경보·전함 도착·용 등장·승리) + 보스 음악 | `Finale/PGFinaleDirector.cpp` `OnRep_State` | 모두에게 도는 자리 | `Play_Finale_*`, `Play_Music_Boss` |
| ★ | 안내 문구·카운트다운·탈출 | `Finale/PGAnnounceSubsystem.cpp` `Announce`/`SetCountdown`, `Objects/PGExtractionZoneActor.cpp` | 탈출은 BP 알림 4개 있음 | `Play_UI_Announce`, `Play_Extract_*` |

**그다음** (있으면 좋은 것): 로봇 타기·공격(`Robot/PGRobotCharacter.cpp`), 비행 장치 펼치기·분사(`Vehicle/PGFlightKitComponent.cpp` `SetDeployed`, 분사 중), 탱크 포·엔진(`Vehicle/PGTankPawn.cpp` `MulticastShotFx`), 전함 엔진·포(`Finale/PGBattleshipActor.cpp`), 부스 들어감(`Objects/PGBoothActor.cpp` 안내 문구 뜨는 자리), 교환(`Objects/PGServiceInteractionActor.cpp` — `OnServiceRequested` BP 알림), 가발 쓰기(`Objects/PGWearableComponent.cpp` `OnWearableChanged`), 로딩·일시정지·버튼 소리(`Flow/`, `UI/`), 타이틀 연출(`Flow/PGTitleIntro.cpp`).

**주변 소리(호수·시내·공장·전쟁 구역)**: 지금은 지역을 알려 주는 장치가 없다.
- 시설(공장·시내·전쟁 구역 핵심·어촌)은 각각 **따로 불러오는 레벨**이다 → 그 레벨 안에 와이즈의 **AkAmbientSound** 액터를 놓으면 레벨이 올 때 같이 온다. 코드가 필요 없다(가장 쉬움).
- 호수는 `WarZoneFootprintPreview` 가 호수 자리를 알고 있다 → 호수 가운데에 소리 액터 하나를 놓는 한 줄이면 된다.

---

## 6. 가벼움 (RTX 4060·팀원 PC 기준)

| 할 것 | 왜 |
|---|---|
| 모든 3D 소리에 **감쇠 거리**(Attenuation) 를 건다 | 600m 맵이라 멀리 있는 소리까지 계산하면 낭비 |
| 와이즈 **최대 동시 소리 수**(Voice Limit) 를 분류별로 정한다 (예: 잔해 8개, 총 16개) | 건물이 무너지면 잔해가 수백 개 — 전부 울리면 느려지고 시끄럽다 |
| 한꺼번에 많이 나는 소리(잔해·탄피)는 **Virtual Voice**(안 들리면 계산을 멈춤) 로 | 위와 같은 이유 |
| 긴 소리(음악·주변 소리)는 **Streaming**(조금씩 읽기) | 메모리 절약 |
| 소리 에셋은 `TSoftObjectPtr`(필요할 때 읽기) 로 칸을 만든다 | 게임 시작 때 소리 전부를 올리지 않는다 — 그림 칸과 같은 방식 |
| 매 프레임 `PostEvent` 를 부르지 않는다 — 계속 나는 소리는 **켤 때 한 번, 끌 때 한 번** | 엔진·날갯짓·분사 같은 반복 소리 |

---

## 7. 작업 순서 (추천)

1. 설치 → 빈 이벤트 하나(`Play_Test`) 를 만들어 BP 에서 `Post Event` 로 들리는지 확인. **여기까지 C++ 변경 없음.**
2. BP 로 되는 것부터: 전함 문, 문, 상자, 부스, 탈출 (각 BP 에 노드 추가).
3. 애니메이션 노티파이: 발소리, 몬스터 공격, 용 울음·날갯짓.
4. 그다음 C++: `Build.cs` 에 `AkAudio` → `PGSound` 도우미 → `DA_PGSounds` → 총 `MulticastFireFx` → 잔해·미사일·무너짐·결말.
5. 매 단계마다: 빌드 → 스모크(`PGObjectSmokeTest passed=64 failed=0`) → 헤드리스 한 판(크래시 없는지).

## 기록

| 날짜 | 와이즈 버전 | 누가 | 메모 |
|---|---|---|---|
| | | | |

참고: [Audiokinetic — Coding Wwise in UE for Beginners](https://www.audiokinetic.com/en/blog/coding-wwise-in-ue-for-beginners/), [Above Noise Studios — UE5 and Wwise Integration](https://abovenoisestudios.com/blogeng/ue5andwwiseintegrationeng)
