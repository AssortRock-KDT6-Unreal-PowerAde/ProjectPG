# 형님께 전달 — 플레이어 캐릭터 멀티 수정 (2026-09-27)

> 누가 무엇을 할지 요약은 `TeamTodo_2026-09-28.md`. 이 문서는 그 자세한 내용입니다.

> 이 문서만 보시면 됩니다. 코드 안에서는 **`[멀티 임시수정`** 으로 검색하면 고친 곳이 전부 나옵니다(시작 줄 ~ `[멀티 임시수정] 끝` 줄). 9/27 은 플레이어, 9/28 은 몬스터입니다.
> 제 개인 저장소에만 임시로 넣었습니다. 팀 저장소는 건드리지 않았습니다. 형님 쪽에 반영해 주시면 제 쪽 임시 수정은 형님 코드로 바꾸겠습니다.

## 증상

전용 서버로 2명 이상 접속하면(에디터 Play As Client 포함)

1. **클라이언트 캐릭터가 서 있는데도 걷는 동작을 합니다.** (발은 제자리)
2. 걸어 다니면 서버가 캐릭터 위치를 계속 뒤로 되돌립니다(뚝뚝 끊김). 1번을 고치면서 같이 찾았습니다.

혼자 할 때(서버 = 내 컴퓨터)는 안 보입니다.

## 원인

| | 어디 | 무엇이 문제였나 |
|---|---|---|
| 1 | `UCharacterAttributeSet` | 속성(체력·스태미나·걷기 속도 등)이 **복제 설정이 없어** 서버에만 값이 있고 클라이언트는 전부 0 |
| 2 | `UCustomAnimInstance::NativeUpdateAnimation` | `Speed = 이동 속도 ÷ GetWalkSpeed()` — 클라에서 걷기 속도가 0 이라 **0 ÷ 0 = 숫자 깨짐(NaN)** → 걷는 동작 |
| 3 | `ACustomPlayerCharacter::BeginPlay` | `MaxWalkSpeed = 300` 이 `if (HasAuthority())` 안에만 있어 **클라는 기본값 600** 으로 걸음 → 서버(300)와 달라 위치 되돌림 |

## 고친 곳 (3파일 + 헤더 1)

### 1) `Public/GameplayAbilities/CharacterAttributeSet.h` / `Private/GameplayAbilities/CharacterAttributeSet.cpp`

- 속성 6개 모두 `UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_XXX)` 로 바꿈.
- `GetLifetimeReplicatedProps` 추가: `DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, XXX, COND_None, REPNOTIFY_Always);`
- `OnRep_XXX(const FGameplayAttributeData& OldValue)` 6개 추가(`UFUNCTION()`), 안에서 `GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, XXX, OldValue);`
- cpp 에 `#include "Net/UnrealNetwork.h"`.

GAS 공식 방식 그대로입니다. 속성 세트는 캐릭터의 `AbilitySystemComp`(이미 `SetIsReplicated(true)`, `Mixed`) 가 같이 복제합니다.

### 2) `Private/Animations/CustomAnimInstance.cpp`

```cpp
// 원래
Speed = velocity.Size() / characterAttributeSet->GetWalkSpeed();
// 바꿈
const float walkSpeed = characterAttributeSet->GetWalkSpeed();
Speed = walkSpeed > KINDA_SMALL_NUMBER ? velocity.Size() / walkSpeed : 0.f;
```

값이 클라에 도착하기 전 첫 몇 프레임에도 0 으로 나누지 않게 막는 줄입니다.

### 3) `Private/Characters/CustomPlayerCharacter.cpp` (`BeginPlay`)

`InitAbilityActorInfo` 바로 아래, `if (HasAuthority())` **앞**에 추가:

```cpp
if (UCharacterMovementComponent* sharedMovementComp = GetCharacterMovement())
    sharedMovementComp->MaxWalkSpeed = 300.f;
```

`HasAuthority()` 안의 원래 줄은 그대로 뒀습니다(지워도 됩니다). 걷기 속도를 표로 옮기실 때는 이 줄도 같은 값을 쓰게 해 주세요.

## 참고 — 달리기(GA_Sprint)

`GA_Sprint` 는 속성 값(`GetSprintSpeed/GetWalkSpeed`)으로 `MaxWalkSpeed` 를 바꿉니다. 1)로 속성이 클라에도 오므로 클라에서 달리기를 켜도 같은 값이 됩니다. 능력의 실행 방식(NetExecutionPolicy)이 서버 전용이면 클라는 달리기 속도를 모르므로 다시 끊길 수 있습니다 — 그때는 `LocalPredicted` 로 두시면 됩니다(제가 확인한 것은 아닙니다).

## 추가로 알려 드릴 것 — 가방(인벤토리)이 클라이언트로 안 옵니다 (고치지 않음, 형님 판단 필요)

- `UInventoryComponent` 의 `ItemsMap`(TMap)·`PocketInventoryID` 는 복제되지 않습니다. 전용 서버에서 아이템을 주우면 **서버 쪽 가방에만** 들어가고 클라이언트 가방은 비어 있습니다.
- 그래서 클라이언트 가방 화면에는 주운 아이템이 안 보일 것이고, 클라이언트에서 "이 아이템 있나?" 를 물으면 항상 "없다" 가 나옵니다.
- 이 때문에 두 번째 사람이 여고생에게 연료통을 못 건넸습니다. 이건 제 쪽(상호작용 컴포넌트, 우리 코드)에서 "클라이언트 판단을 믿지 않고 서버에 물어보게" 고쳤습니다 — 형님 코드는 안 건드렸습니다.
- 확인 방법(9/28): 서버 `PG.Delay 35 PG.SpawnGirl`, 접속자 `PG.GirlSpamTest` → 접속자 로그 `press 1 on BP_PGFloorItem… (bag Fuel=0)` 이 주운 뒤에도 0 이면 아직 안 오는 것.
- 가방 화면까지 멀티에서 맞추려면 서버 → 주인 클라이언트로 가방 내용을 보내는 길(복제 가능한 배열로 바꾸거나 Client RPC)이 필요합니다. 웹 서버에서 가방을 받는 구조(`OnInventoryReceived`)와 어떻게 맞출지는 형님이 정하셔야 해서 손대지 않았습니다.

## 몬스터 공격·맞는 동작 (2026-09-28 임시수정)

- **증상:** 전용 서버에서 몬스터가 서 있기·달리기·죽음은 보이는데 **공격·맞는 동작은 클라이언트 화면에 안 나왔습니다.** 제자리에서 휘두르지 않고 피해만 줬습니다.
- **원인:** `APGMonsterCharacter::PlayOnce`(`GetMesh()->PlayAnimation`)가 서버에서만 불립니다(공격 결정·맞음·벽 부수기 휘두르기). 한 번짜리 동작은 복제되지 않습니다.
- **고친 곳(`[멀티 임시수정 2026-09-28` 로 검색):**
  - `Public/Monster/PGMonsterCharacter.h` — `FPGMonsterOnceCue{Anim, Seq}` 구조체, `OnceCue`(ReplicatedUsing=`OnRep_OnceCue`), `PlayOnceForAll`.
  - `Private/Monster/PGMonsterCharacter.cpp` — `GetLifetimeReplicatedProps` 에 `OnceCue` 한 줄, `PlayOnceForAll`/`OnRep_OnceCue` 함수, 호출 세 곳(`PlayOnce(Swing)`, `PlayOnce(Visuals.Hit)`, `PlayOnce(Chosen)`)을 `PlayOnceForAll` 로(원래 줄은 주석에 적어 둠).
  - 깨어남(`OnRep_Dormant`)·죽음(`OnRep_Dead`)은 원래 클라에서도 트므로 그대로 뒀습니다.
- **요점:** 클라이언트는 `OnRep_OnceCue` 에서 같은 동작을 틀고 `CurrentLoop = nullptr`, `AnimLockUntil = 지금 + 동작 길이` 를 잡습니다. 안 잡으면 클라 `Tick` 이 바로 서 있기/달리기 루프로 덮습니다(클라에는 `bAttacking` 이 없어서).
- **확인:** 전용 서버 + 클라이언트, 슬라임 셋이 클라 캐릭터를 공격 — 서버 "attacks … Slime_Attack01" 와 같은 동작이 클라 로그 "once anim on this screen — Slime_Attack01_ANIM" 으로 나옴.
- 탑승 로봇(`APGRobotCharacter`, 제 코드)도 이 `PlayOnceForAll` 을 씁니다. 형님 쪽에서 이름을 바꾸시면 로봇 두 줄(`PGRobotCharacter.cpp` 공격)도 같이 바꿔 주세요.

## 참고 — 로비 "출격" 버튼과 매칭 (제 코드만 고침)

- 로비 출격(`UPGRunSubsystem::StartGame`)이 이제: ① 실행 인자 `-PGServer=주소:포트` 가 있으면 그 서버로 바로 접속, ② 로그인돼 있으면(`UWebSocketSubSystem::GetCurrentUserID()` 가 비어 있지 않으면) 형님 `UMatchmakingSubSystem::RequestGameStart()` 를 부르고 `OnMatchStatusChanged` 를 받아 로딩 화면에 매칭 상태를 띄웁니다. 접속은 형님 코드(`JOIN_SERVER` → `ClientTravel`) 그대로입니다. ③ 둘 다 아니면 예전처럼 혼자 하는 판.
- 형님 코드는 안 건드렸습니다. 확인한 것은 ①(로비 → 출격 → 전용 서버 접속 → 시작 지역에 섬)뿐이고, ②는 웹 서버가 있어야 해서 형님 쪽에서 한 번 봐 주세요.
- 매칭 취소 알림의 상태 글자가 `Match_CANCELLED`(대소문자 섞임)와 `CancelMatch` 두 가지라 둘 다 받게 했습니다.

## 부탁 — 플레이어가 맞으면 체력이 줄고 죽게 (2026-09-28, 고치지 않음)

**지금:** 캐릭터에 체력(`UCharacterAttributeSet::Health` 100)은 있는데 **피해를 받아 깎는 코드가 없습니다.** 총·몬스터·폭발에 맞아도 안 죽습니다.
확인: 서버 콘솔 `PG.DamagePlayer 0 30` → `Health 100 -> 100 (NOT reduced …)`.

**저희 쪽은 준비돼 있습니다**
- 공격은 전부 언리얼 기본 피해로 보냅니다: 총(`ApplyPointDamage`), 가발 빔, 탱크 포·미사일·전함 포(직격 + `ApplyRadialDamage`), 드래곤 불·충돌, 연료통 폭발, 형님 몬스터 공격(`ApplyDamage`).
  → 캐릭터의 `TakeDamage` 까지 옵니다. 차·탱크·로봇 들이받기는 몬스터에게만 갑니다.
- 체력이 0 이 되면: 서버가 사람마다 체력 변화를 듣고 있다가 그 사람 판을 "사망" 으로 끝내고 결과 화면을 띄웁니다(`UPGRunSubsystem::WatchPlayerPawn` → `HandleNetHealthChanged`). 캐릭터가 Destroy 돼도 사망으로 칩니다.

**넣으실 코드(예시 — 형님 방식대로 바꾸셔도 됩니다)**

방법 A: `TakeDamage` 에서 체력 속성 직접 깎기 (가장 짧음)
```cpp
// CustomPlayerCharacter.h
virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser) override;

// CustomPlayerCharacter.cpp
float ACustomPlayerCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || Applied <= 0.f)   // 체력은 서버만 바꾼다(복제로 클라에 간다)
		return Applied;
	// (PvP 규칙 — 아래) 게임 모드가 "이 사람이 저 사람에게 피해를 줄 수 있나" 를 판단
	if (UAbilitySystemComponent* Asc = GetAbilitySystemComponent())
	{
		const FGameplayAttribute HealthAttr = UCharacterAttributeSet::GetHealthAttribute();
		const float NewHealth = FMath::Max(0.f, Asc->GetNumericAttribute(HealthAttr) - Applied);
		Asc->SetNumericAttributeBase(HealthAttr, NewHealth);
	}
	return Applied;
}
```
방법 B(GAS 정석): 피해용 GameplayEffect(`GE_Damage`, SetByCaller 크기)를 만들어 `TakeDamage` 에서 적용하고, `UCharacterAttributeSet::PostGameplayEffectExecute` 에서 Health 를 0~MaxHealth 로 자릅니다. 방어구·버프가 붙을 거면 이쪽이 낫습니다.

**PvP(플레이어끼리 피해) 규칙 — 기획 결정 후 한 곳에만**
- 게임 모드에 예: `bool AGameModePG::CanDamage(AController* Attacker, AController* Victim) const` — PvP 를 끄면 둘 다 플레이어일 때 false. 위 `TakeDamage` 에서 이것만 부르면 됩니다.
- 저희 무기 쪽에는 "플레이어 빼고 쏘기" 를 넣지 않았습니다(규칙이 두 곳으로 갈라지지 않게).

**확인:** 서버 `PG.DamagePlayer 0 30` → `Health 100 -> 70`, `PG.DamagePlayer 0 100` → 그 사람 결과 화면이 "사망". 쏜 사람을 주면 PvP 확인: `PG.DamagePlayer 0 30 1`(1번 사람이 0번을 쏜 것처럼).

## 확인 방법

1. 에디터 → 플레이 설정 **인원 2명, Play As Client** → `LD_MetaballGenerationTest` 실행.
2. 두 창 모두 가만히 두면 캐릭터가 **서 있는 동작**이어야 합니다.
3. 걸어 다닐 때 끊기지 않아야 합니다. 콘솔에 `p.NetShowCorrections 1` 을 치면 서버가 위치를 되돌릴 때마다 화면에 캡슐이 그려집니다(안 그려지면 정상).
