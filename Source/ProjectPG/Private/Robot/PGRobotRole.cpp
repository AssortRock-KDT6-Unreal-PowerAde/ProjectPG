// 로봇 역할(PGRobotRole.h 머리말). 본문은 원래 APGRobotCharacter 의 `if (bRideable)` 갈래에 있던 것을 역할별로 옮겼다(내용 그대로).
#include "Robot/PGRobotRole.h"
#include "Robot/PGRobotCharacter.h"
#include "Vehicle/PGTankPawn.h"
#include "Finale/PGFinaleDirector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"

// ---- 공통 기본값 ----

float UPGRobotRole::GetKnockRatio(const APGRobotCharacter& Robot) const
{
	return 1.0f;
}

float UPGRobotRole::AttackDamageFor(const APGRobotCharacter& Robot, const APGMonsterCharacter* Target) const
{
	// 보스 로봇과 탈 수 없는 로봇이 치는 경우: 예전 값 그대로(보스는 따로 맞춘 전투 균형이 있다).
	return Robot.AttackDamage;
}

// ---- 보스 ----

void UPGRobotBossRole::OnBeginPlay(APGRobotCharacter& Robot) const
{
	Robot.SetActorScale3D(FVector(Robot.BossScale));
	// 커진 만큼 사거리·속도도 키운다. 안 그러면 큰 몸으로 종종걸음을 친다.
	Robot.AttackRange *= Robot.BossScale;
	// 깨어나는 거리: 사거리의 2.5배(8배 보스 = 약 52m). 4배(83m)였을 때는 멀리 지나가기만 해도 깨어나 워존을 휘젓고 다녔고,
	// 1.5배(31m)는 거의 붙어야 일어나서 "멍하니 서 있는 보스"로 보였다. 지금은 플레이어만 깨우므로(IsValidTarget) 넓혀도 된다.
	Robot.WakeRange = FMath::Max(Robot.WakeRange, Robot.AttackRange * 2.5f);
	Robot.GetCharacterMovement()->MaxWalkSpeed *= FMath::Sqrt(Robot.BossScale);
	// 발걸음 높이는 스케일을 따라 안 커진다. 몸집 비율보다 낮게(×0.6): 높으면 건물 위로 올라가 낀다. 걸리는 건 밀쳐낸다.
	Robot.GetCharacterMovement()->MaxStepHeight = 45.0f * Robot.BossScale * 0.6f;
	Robot.GetCharacterMovement()->SetWalkableFloorAngle(55.0f);
	// 한 번 본 플레이어는 시야에서 사라져도 이 거리 안이면 계속 쫓는다(보스는 포기하지 않는다).
	Robot.AggroKeepRadius = 8000.0f;
	if (Robot.HasAuthority())
		Robot.SpawnDefaultController();
}

float UPGRobotBossRole::GetKnockRatio(const APGRobotCharacter& Robot) const
{
	// 소품 밀쳐내기(스윕 자체는 APGMonsterCharacter::KnockAhead). 세기 기준: 2.5 배 크기 대비 비율.
	return Robot.BossScale / 2.5f;
}

float UPGRobotBossRole::ModifyIncomingDamage(const APGRobotCharacter& Robot, float Damage, const AActor* Causer) const
{
	// 보스가 사람이 탄 로봇이나 탱크에게 맞으면 피해를 키운다. 맨몸 총격·몬스터끼리의 피해는 그대로.
	const APGRobotCharacter* RobotCauser = Cast<APGRobotCharacter>(Causer);
	const bool bHeavyCauser = IsValid(Cast<APGTankPawn>(Causer)) || (IsValid(RobotCauser) && RobotCauser->IsRideable());
	return bHeavyCauser ? Damage * Robot.BossHeavyDamageMultiplier : Damage;
}

void UPGRobotBossRole::OnDied(APGRobotCharacter& Robot) const
{
	// 보스가 쓰러지면 피날레가 시작된다. 기존 코드에서 피날레로 들어가는 유일한 호출(기획서 §5).
	if (Robot.HasAuthority())
		APGFinaleDirector::NotifyBossDefeated(&Robot);
}

// ---- 탑승용 ----

void UPGRobotRideRole::OnBeginPlay(APGRobotCharacter& Robot) const
{
	// 보스만 AI 를 붙인다. 탑승용은 플레이어가 올 때까지 빈 몸으로 서 있는다.
	// 크기 배율만큼 몸·발걸음·카메라 거리를 키운다. 17m 로봇을 사람 카메라로 보면 몸통만 보인다.
	if (Robot.RideScale <= 1.0f)
		return;
	Robot.SetActorScale3D(FVector(Robot.RideScale));
	Robot.AttackRange *= Robot.RideScale;
	Robot.AttackSweepRadius *= Robot.RideScale;
	// 발 높이 = 사람 비율(45cm) × 몸집 × 0.6. 1.6 이었을 때는 5.7m 짜리도 계단처럼 밟아서 선반·건물 지붕에 올라가 끼었다(stuck 131회).
	// 이제 소품·벽 판자는 밟지 않고 밀쳐내므로(KnockAhead) 낮게 둔다. 못 넘는 건 부수고 지나간다.
	Robot.GetCharacterMovement()->MaxStepHeight = 45.0f * Robot.RideScale * 0.6f;
	Robot.GetCharacterMovement()->SetWalkableFloorAngle(55.0f);
	Robot.RideWalkSpeed *= FMath::Sqrt(Robot.RideScale);
	// 크기에 비례해서만 멀어지면 17m 로봇이 화면 안에서 사람 크기로 보인다. 배율을 곱해 당겨서 덩치를 느끼게 한다.
	Robot.CameraArm->TargetArmLength = 520.0f * Robot.RideScale * Robot.RideCameraDistanceFactor;
	Robot.CameraArm->SocketOffset = FVector(0.0f, 0.0f, 160.0f * Robot.RideScale * Robot.RideCameraHeightFactor);
}

float UPGRobotRideRole::GetKnockRatio(const APGRobotCharacter& Robot) const
{
	return Robot.RideScale / 2.5f;
}

bool UPGRobotRideRole::CanKnockProps(const APGRobotCharacter& Robot) const
{
	// 빈 탑승 로봇은 서 있기만 하므로 밀쳐내지 않는다.
	return Robot.IsRidden();
}

// 탑승 로봇 한 방의 피해. 9/22 사용자: "탑승 로봇이 왜 이렇게 약해? 작은 몹은 한 방, 크리처도 세 대면 쓰러지게".
// 예전에는 누구에게나 40 이라 크리처(체력 400)를 10대 때려야 했다.
//  - 작은 몹(반지름 1.2m 이하): 최대 체력 전부 → 한 방.
//  - 크리처(그보다 큰 몹): 최대 체력의 1/3 → 세 대. 0.1% 더 줘서 소수점 오차로 네 대가 되지 않게.
//  - 로봇(보스 포함)을 칠 때는 예전 값 그대로.
float UPGRobotRideRole::AttackDamageFor(const APGRobotCharacter& Robot, const APGMonsterCharacter* Target) const
{
	if (!IsValid(Target) || Target->IsA<APGRobotCharacter>())
		return Robot.AttackDamage;
	const float TargetMax = FMath::Max(Target->GetMaxHealth(), 1.0f);
	return Target->GetSimpleCollisionRadius() <= 120.0f ? TargetMax : TargetMax / 3.0f * 1.001f;
}

void UPGRobotRideRole::TickRidden(APGRobotCharacter& Robot) const
{
	if (Robot.HasAuthority())
		Robot.TrampleSmallMonsters();
}

void UPGRobotRideRole::TickUnridden(APGRobotCharacter& Robot) const
{
	// 탑승용은 AI 가 없어서 잠복 해제를 여기서 직접 본다. 가장 가까운 플레이어 기준. 빈 로봇은 0.2초에 한 번만.
	if (!Robot.HasAuthority())
		return;
	Robot.SetActorTickInterval(0.2f);
	float Nearest = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = Robot.GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Other = It->IsValid() ? It->Get()->GetPawn() : nullptr;
		if (IsValid(Other) && Other != &Robot)
			Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist(Other->GetActorLocation(), Robot.GetActorLocation())));
	}
	if (Robot.IsDormant() && Nearest <= Robot.WakeRange)
		Robot.Wake();
	else if (!Robot.IsDormant() && !Robot.IsBusy() && Nearest > Robot.WakeRange * 1.5f)
		Robot.Sleep();
}
