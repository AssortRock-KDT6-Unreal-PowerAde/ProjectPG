#include "Objects/PGCarRevertComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Objects/PGObjectTypes.h"
#include "Objects/PGTransformNPCActor.h"

UPGCarRevertComponent::UPGCarRevertComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 1초에 한 번만 본다. 매 프레임 볼 이유가 없고, 판 내내 떠 있는 컴포넌트다.
	PrimaryComponentTick.TickInterval = 1.0f;
}

void UPGCarRevertComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPGCarRevertComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	APawn* Car = Cast<APawn>(GetOwner());
	if (!IsValid(Car) || !Car->HasAuthority())
		return;
	// 타고 있거나, 아직 움직이는 중이면 기다린다. 굴러가는 차가 갑자기 사람으로 바뀌면 사고다.
	if (Car->GetController() || Car->GetVelocity().Size() > StillSpeed)
	{
		IdleTime = 0.0f;
		bWarned = false;
		bPausedLogged = false;
		return;
	}
	// 공중이거나 무언가에 실려 있으면 세지 않는다.
	//
	// 왜(9/20 PIE): 승강기로 전함에 실은 차가 배를 안 따라가고 제자리에 남았고, 아무도 안 타는 채로
	//   60초가 지나 역변신이 돌아 **여고생이 하늘에 떠 있었다**. 근본 원인은 전함 쪽이지만
	//   "차가 배에 실려 하늘을 난다"는 이 게임의 정상 동선이라, 그때마다 여고생이 튀어나오면 안 된다.
	// 리셋하지 않고 **멈추기만** 한다. 배에서 내려 땅에 서면 이어서 세는 쪽이 자연스럽다.
	if (IsInAir() || Car->GetAttachParentActor())
	{
		if (!bPausedLogged)
		{
			bPausedLogged = true;
			UE_LOG(LogPGObjects, Display, TEXT("%s: in the air or carried — holding the revert timer at %.0fs"),
				*Car->GetName(), IdleTime);
		}
		return;
	}
	bPausedLogged = false;
	// 플레이어가 가까이(80m 안) 있으면 세지 않는다. 차에서 내려 근처에서 싸우거나 서 있는 동안 타고 온 차가 없어지면
	// "내 차가 사라졌다" 로 보인다(9/22 PIE: "가만히 있으니까 타던 차가 사라졌는데?"). 멀리 두고 떠났을 때만 돌아간다.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Player = PC ? PC->GetPawn() : nullptr;
		if (IsValid(Player) && FVector::DistSquared(Player->GetActorLocation(), Car->GetActorLocation()) < FMath::Square(8000.0f))
		{
			IdleTime = 0.0f;
			bWarned = false;
			return;
		}
	}
	IdleTime += DeltaTime;
	// 사라지기 전에 한 번 알린다. 소리 없이 없어지면 버그로 보인다(9/20 PIE: "갑자기 사라지는데 머냐?").
	if (!bWarned && IdleTime >= RevertSeconds - WarnSeconds)
	{
		bWarned = true;
		UE_LOG(LogPGObjects, Display, TEXT("%s: nobody is driving — turning back into the girl in %.0fs (drive it to keep it)"),
			*Car->GetName(), WarnSeconds);
	}
	if (IdleTime >= RevertSeconds)
		Revert();
}

void UPGCarRevertComponent::Revert()
{
	AActor* Car = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(Car) || !IsValid(World))
		return;

	// 차가 선 자리 땅에 여고생을 다시 세운다. 발이 땅에 붙도록 실제 표면을 잰다.
	FVector Where = Car->GetActorLocation();
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGRevertGround), false, Car);
	// [9/22 PIE] 외진 거점 옆에서 돌아온 여고생이 3m 쯤 공중에 떠 있었다. 차 위 3m 에서 재기 시작하면
	//   머리 위 나뭇가지·지붕 같은 것을 먼저 맞고 그 위에 세운다. 차 원점은 차 가운데(바닥에서 1m 안팎)라
	//   거기서 조금 위에서 재기 시작하면 차 밑 땅을 바로 맞는다. 무엇을 맞았는지 로그에 남겨 다음에 바로 확인한다.
	const FVector From = Where + FVector(0.0f, 0.0f, 60.0f);
	if (World->LineTraceSingleByChannel(Ground, From, From - FVector(0.0f, 0.0f, 1200.0f), ECC_Visibility, Params))
		Where = Ground.ImpactPoint + FVector(0.0f, 0.0f, 5.0f);
	UE_LOG(LogPGObjects, Display, TEXT("%s: revert ground car_z=%.0f girl_z=%.0f hit=%s/%s"),
		*Car->GetName(), Car->GetActorLocation().Z, Where.Z,
		*GetNameSafe(Ground.GetActor()), *GetNameSafe(Ground.GetComponent()));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FTransform Transform(FRotator(0.0f, Car->GetActorRotation().Yaw, 0.0f), Where);
	APGTransformNPCActor* Girl = World->SpawnActorDeferred<APGTransformNPCActor>(
		APGTransformNPCActor::GetSpawnClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (IsValid(Girl))
	{
		// 연료는 한 번만 받는다. 돌아온 뒤에는 F 한 번이면 바로 변신한다(사용자 결정 9/20).
		Girl->SetAlreadyFueled(true);
		Girl->SetPlayRevertIntro(true); // 변신 애니를 거꾸로 돌려 "차가 도로 사람이 되는" 그림을 만든다
		Girl->FinishSpawning(Transform);
	}
	UE_LOG(LogPGObjects, Display, TEXT("%s: nobody drove it for %.0fs — back to the girl (%s)"),
		*Car->GetName(), RevertSeconds, *GetNameSafe(Girl));
	Car->Destroy();
}


// 아래에 밟을 것이 있나. 비행 키트의 IsAirborne 과 같은 뜻이되, 차에 그 컴포넌트가 없을 수도 있어 따로 잰다.
// 기준을 150cm 로 둔 이유: 세워 둔 차도 바닥에서 1m 쯤 떠 있는 것으로 재진다(액터 원점이 차 가운데라서).
bool UPGCarRevertComponent::IsInAir() const
{
	const AActor* Car = GetOwner();
	const UWorld* World = GetWorld();
	if (!IsValid(Car) || !IsValid(World))
		return false;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGRevertAir), false, Car);
	const FVector Start = Car->GetActorLocation();
	if (!World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0f, 0.0f, 20000.0f), ECC_Visibility, Params))
		return true; // 아래에 아무것도 없다(맵 밖·바다 위)
	return Start.Z - Hit.ImpactPoint.Z > 150.0f;
}
