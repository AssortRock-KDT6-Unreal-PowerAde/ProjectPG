// APGBattleshipActor — 함교·조종 — 조종석 앉기/일어나기, 조종(이동·상승), 함교 시점 카메라, 조준선.
// (2026-09-26 PGBattleshipActor.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGBattleshipActorInternal.h"
#include "PGShipWeapons.h"
#include "PGShipHelm.h"
#include "PGShipHullBuilder.h"
#include "Common/PGKeyPolling.h"

// 함교가 설 자리를 정한다(BuildInterior 가 갑판을 깔기 전에 부른다 — 위 주석 참고).
void UPGShipHelm::ComputeBridgeLocal(float FrontX)
{
	Ship->BridgeLocal = FVector(FrontX - 1500.0f, 0.0f, Ship->DeckLocalZ);
	// 유리창을 찾으면 그 안쪽에 선다. 못 찾으면(팩이 바뀌면) 갑판 앞쪽 그대로.
	FBox Glass(ForceInit);
	if (Ship->HullBuilder->FindHullPartLocal(TEXT("BodyFrontGlass"), Glass))
	{
		// 유리 바로 안쪽(뒤로 6m), 유리 아래끝보다 조금 위 — 창밖이 눈높이에 오게.
		Ship->BridgeLocal = FVector(Glass.Max.X - 600.0f, Glass.GetCenter().Y, FMath::Max(Ship->DeckLocalZ, Glass.Min.Z + 200.0f));
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: bridge behind the nose glass at %s (glass z %.0f..%.0f m)"),
			*Ship->BridgeLocal.ToCompactString(), Glass.Min.Z * 0.01f, Glass.Max.Z * 0.01f);
	}
}

void UPGShipHelm::BuildBridge(float FrontX, float HalfY)
{
	// 코드 갑판이 꺼져 있으면(바닥은 배 블루프린트의 보이지 않는 상자) 함교 바닥·경사로는 사람만 밟고 차는 지나가게 한다.
	// 왜(9/23): 이 두 상자는 예전 코드 갑판 높이(deck_z 1981)에 맞춰져 있어, 블루프린트 바닥과 높이가 어긋난 채 통로 한가운데에
	//   남는다. 로그에서 날던 차가 여기에 걸렸다("touching ship boxes: BridgeRamp"). 사람은 여전히 이걸 밟고 조종석에 오른다.
	auto LetCarsThrough = [this](UBoxComponent* Box)
	{
		if (IsValid(Box) && !Ship->bBuildCodeDeck)
			Box->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	};
	// 바닥·난간(발 닿는 상자). 유리 쪽으로 걸어 나가 떨어지지 않게 앞을 막는다.
	LetCarsThrough(Ship->HullBuilder->AddWalkBox(TEXT("BridgeFloor"), Ship->BridgeLocal + FVector(0.0f, 0.0f, -60.0f), FVector(900.0f, 1200.0f, 60.0f)));
	Ship->HullBuilder->AddSolidBox(TEXT("BridgeFloorPlate"), Ship->InteriorRoot, Ship->BridgeLocal + FVector(0.0f, 0.0f, -60.0f), FVector(900.0f, 1200.0f, 60.0f),
		FRotator::ZeroRotator, *Ship->BridgePanelMaterial.ToSoftObjectPath().ToString());
	// 난간은 두지 않는다.
	//
	// 전에는 "나머지 세 면이 32m 절벽" 이라고 보고 네 면을 다 둘렀는데, 그 32m 는 낡은 주석에서 나온 숫자였다.
	// 실제 낙차는 PIE 로그가 말해 준다: "bridge ramp 20m long, 1m up" — 함교 바닥은 갑판보다 78cm 위일 뿐이다.
	// 78cm 를 막자고 두른 높이 180cm 짜리 보이지 않는 울타리가, 차로 갑판을 날아 함교까지 가려 할 때 앞을 막고 있었다
	// (9/20 사용자: "전함 안에서 왜 날다가 앞으로 가면 막혀?"). 떨어져 봐야 갑판이고, 한 걸음이면 도로 올라온다.

	// 앉았을 때 쓸 카메라. 조종석 뒤 6m, 위 3.5m 에서 뱃머리(+X)를 본다 — 전함 3인칭.
	// 왜 카메라 컴포넌트인가: SetViewTarget 은 액터를 보는데, 액터에 카메라가 있으면 그 자리를 쓴다.
	//   없으면 배 원점(선체 한가운데)이 잡혀 배 속을 들여다보게 된다.
	BridgeViewCamera = NewObject<UCameraComponent>(Ship.Get(), TEXT("BridgeViewCamera"));
	BridgeViewCamera->SetupAttachment(Ship->InteriorRoot);
	ApplyBridgeViewCamera();
	BridgeViewCamera->RegisterComponent();

	// 갑판에서 함교까지 오르는 경사로.
	//
	// 왜 필요한가: 함교 바닥은 뱃머리 유리창 아래끝에 맞춰 놓여서 갑판보다 32.7m 위다. 이 프로젝트의
	//   오를 수 있는 턱(MaxStepHeight)은 45cm 고 점프해도 1m 남짓이라, 계단이 없으면 함교는
	//   걸어서 영영 못 간다(주포도 못 쓴다). 콘솔 명령 PG.Finale.Bridge 로 순간이동해야만 갔었다.
	// 왜 계단이 아니라 경사로인가: 40cm 단으로 쌓으면 상자가 82개 필요하다. 기울인 상자 한 장이면 끝이고,
	//   차로도 올라갈 수 있다. UE 의 기본 걸을 수 있는 경사는 44도라 22도면 넉넉히 걷는다.
	// 문턱은 오를 수 있는 턱(MaxStepHeight 45cm)이다. 200cm 로 잡아 놨더니 실제 단차 78cm 에서는
	// 경사로가 아예 안 만들어졌고, 그 78cm 턱은 걸어서 못 오른다(9/20 조사: "bridge ramp" 로그 없음).
	const float Rise = Ship->BridgeLocal.Z - Ship->DeckLocalZ;
	if (Rise > 45.0f)
	{
		const float Run = FMath::Max(Rise * 2.45f, 2000.0f);       // 기울기 ≈ 22도
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));
		const float Length = FMath::Sqrt(Rise * Rise + Run * Run);
		const FVector Foot(Ship->BridgeLocal.X - 900.0f - Run, Ship->BridgeLocal.Y, Ship->DeckLocalZ);
		const FVector Centre = (Foot + FVector(Ship->BridgeLocal.X - 900.0f, Ship->BridgeLocal.Y, Ship->BridgeLocal.Z)) * 0.5f;
		// 피치를 주면 상자의 로컬 +X 가 위를 향한다 = 뱃머리 쪽으로 올라가는 비탈.
		// 두께만큼 내리는 오프셋도 같이 기울여야 한다. 배 축으로 그냥 내리면 경사로 윗면이 양 끝에서
		// 몇 cm 씩 어긋난다(9/20 조사).
		const FRotator RampRotation(Angle, 0.0f, 0.0f);
		const FVector RampCentre = Centre + RampRotation.RotateVector(FVector(0.0f, 0.0f, -60.0f));
		LetCarsThrough(Ship->HullBuilder->AddWalkBox(TEXT("BridgeRamp"), RampCentre, FVector(Length * 0.5f, 900.0f, 60.0f), RampRotation));
		// 경사로에도 보이는 몸을 준다. 없으면 허공을 걸어 올라가는 것으로 보인다(9/20 PIE).
		Ship->HullBuilder->AddSolidBox(TEXT("BridgeRampPlate"), Ship->InteriorRoot, RampCentre, FVector(Length * 0.5f, 900.0f, 60.0f),
			RampRotation, *Ship->BridgePanelMaterial.ToSoftObjectPath().ToString());
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: bridge ramp %.0fm long, %.0fm up, %.0f deg"),
			Length * 0.01f, Rise * 0.01f, Angle);
	}

	// 조종 콘솔·캐노피·의자: 모델링 세션이 만든 함교 키트(Tools/import_ship_bridge.py).
	// 규칙은 부스 키트와 같다 — 피벗 = 바닥 한가운데, +X = 유리창 쪽. 이 배의 앞도 +X 라 회전 없이 그대로 놓는다.
	// 조작자는 콘솔 뒤(-X)에 서서 유리창을 본다.
	UStaticMeshComponent* Console = Ship->HullBuilder->AddPartByPath(*Ship->BridgeConsoleMesh.ToSoftObjectPath().ToString(), Ship->BridgeLocal + FVector(120.0f, 0.0f, 0.0f));
	Ship->HullBuilder->AddPartByPath(*Ship->BridgeCanopyMesh.ToSoftObjectPath().ToString(), Ship->BridgeLocal + FVector(320.0f, 0.0f, 0.0f));
	Ship->HullBuilder->AddPartByPath(*Ship->BridgeSeatMesh.ToSoftObjectPath().ToString(), Ship->BridgeLocal + FVector(-200.0f, 0.0f, 0.0f));
	if (!Console) // 키트를 아직 안 가져왔으면 팩 부품으로 대신한다(경로가 없어도 함교는 선다)
		Ship->HullBuilder->AddPart(TEXT("SM_KB3D_MTM_VehicleCargoShip_A_InteriorControls"), Ship->BridgeLocal + FVector(500.0f, 0.0f, 0.0f), FRotator(0.0f, 90.0f, 0.0f));

	// 계기 글자 4줄. 눈높이 앞에 떠 있어 창밖 풍경 위에 겹쳐 보인다(전함 UI).
	const TCHAR* const Labels[] = { TEXT("ALT"), TEXT("SPD"), TEXT("STATE"), TEXT("TGT") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Labels); ++Index)
	{
		UTextRenderComponent* Text = NewObject<UTextRenderComponent>(Ship.Get());
		Text->SetupAttachment(Ship->InteriorRoot);
		Text->SetRelativeLocation(Ship->BridgeLocal + FVector(260.0f, -240.0f + Index * 160.0f, 190.0f));
		// 글자는 기본으로 +X 쪽에서 읽도록 서 있다. 조종석은 뒤(-X)에서 보므로 180도 돌린다.
		Text->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetWorldSize(22.0f);
		Text->SetTextRenderColor(FColor(90, 220, 255));
		Text->SetText(FText::FromString(Labels[Index]));
		Text->SetCanEverAffectNavigation(false);
		Text->RegisterComponent();
		BridgeReadouts.Add(Text);
	}

	// 콘솔 화면(후방 카메라): 콘솔 메시의 화면 슬롯 3개(ScreenA/B/C = 인덱스 3·4·5)에 같은 렌더 타깃을 물린다.
	// 화면 면은 사각형 한 장에 UV 0~1, 가로세로비 1.95:1 이라 렌더 타깃도 그 비율로 잡아야 안 눌린다(모델링 README).
	UMaterialInterface* ScreenMaterial = (Ship->ShipScreenMaterial.IsNull() ? nullptr : Ship->ShipScreenMaterial.LoadSynchronous());
	if (Console && ScreenMaterial)
	{
		BridgeScreenTarget = NewObject<UTextureRenderTarget2D>(Ship.Get());
		BridgeScreenTarget->RenderTargetFormat = RTF_RGBA8;
		BridgeScreenTarget->InitAutoFormat(512, 264); // 작게. 화면을 한 번 더 그리는 일이라 해상도가 곧 비용이다
		BridgeScreenTarget->UpdateResourceImmediate(true);
		BridgeScreenMaterial = UMaterialInstanceDynamic::Create(ScreenMaterial, Ship.Get());
		BridgeScreenMaterial->SetTextureParameterValue(TEXT("Screen"), BridgeScreenTarget);
		BridgeScreenMaterial->SetScalarParameterValue(TEXT("Brightness"), 1.0f);
		for (int32 Slot = 3; Slot <= 5; ++Slot)
			Console->SetMaterial(Slot, BridgeScreenMaterial);

		BridgeCamera = NewObject<USceneCaptureComponent2D>(Ship.Get());
		BridgeCamera->SetupAttachment(Ship->InteriorRoot);
		// 배 뒤쪽(격납고 입구)을 본다 — 들어오는 차가 화면에 잡힌다.
		BridgeCamera->SetRelativeLocation(FVector(Ship->HullLocalBounds.Min.X + 500.0f, 0.0f, Ship->DeckLocalZ + 1500.0f));
		BridgeCamera->SetRelativeRotation(FRotator(-5.0f, 180.0f, 0.0f));
		BridgeCamera->TextureTarget = BridgeScreenTarget;
		BridgeCamera->CaptureSource = SCS_FinalColorLDR;
		BridgeCamera->bCaptureEveryFrame = false; // 우리가 필요할 때만 찍는다
		BridgeCamera->bCaptureOnMovement = false;
		BridgeCamera->FOVAngle = 80.0f;
		BridgeCamera->RegisterComponent();
	}
	// 437m 짜리 배 안에서 함교를 걸어 찾기가 어렵다(9/20 PIE: "콘솔 어디갔데?").
	// 함교 자리에 얇은 빛기둥을 세우고, 격납고에서 함교까지 갑판에 노란 안내선을 깐다.
	if (UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		UMaterialInterface* Glow = (Ship->BridgeGlowMaterial.IsNull() ? nullptr : Ship->BridgeGlowMaterial.LoadSynchronous());
		// 빛기둥: 함교 위로 40m. 갑판 어디서 봐도 "저기가 앞쪽"이라고 읽힌다.
		// 코드 갑판을 안 깔면 빛기둥도 안 세운다 — 조종석 바로 앞을 가로지르는 노란 봉으로 보였다(9/21 사용자: "노란색 봉 치워 줘").
		if (UStaticMeshComponent* Beacon = !Ship->bBuildCodeDeck ? nullptr : Ship->HullBuilder->AddPartByPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), Ship->BridgeLocal + FVector(0.0f, 0.0f, 2000.0f)))
		{
			Beacon->SetRelativeScale3D(FVector(0.4f, 0.4f, 40.0f));
			if (Glow)
				Beacon->SetMaterial(0, Glow);
		}
		// 안내선: 격납고 입구에서 함교까지 갑판에 길게 한 줄.
		// 코드 갑판을 안 깔면 안 그린다 — 그 높이(DeckLocalZ)에 바닥이 없어 허공에 뜬 노란 줄로 보였다(9/21 사용자: "노란 선도 없애도 될 듯").
		const float LineLength = FMath::Max(Ship->BridgeLocal.X - Ship->HangarEntranceLocalX, 1000.0f);
		if (UStaticMeshComponent* Line = !Ship->bBuildCodeDeck ? nullptr : Ship->HullBuilder->AddPartByPath(TEXT("/Engine/BasicShapes/Cube.Cube"),
			FVector((Ship->HangarEntranceLocalX + Ship->BridgeLocal.X) * 0.5f, 0.0f, Ship->DeckLocalZ + 6.0f)))
		{
			Line->SetRelativeScale3D(FVector(LineLength / 100.0f, 0.6f, 0.06f));
			if (Glow)
				Line->SetMaterial(0, Glow);
		}
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: bridge built at %s (readouts=%d screen=%s)"),
		*Ship->GetActorTransform().TransformPosition(Ship->BridgeLocal).ToCompactString(),
		BridgeReadouts.Num(), BridgeScreenMaterial ? TEXT("on") : TEXT("off"));
}

// 앉았을 때 쓰는 카메라를 자리에 놓는다(만들 때 한 번, 그 뒤로는 앉을 때마다).
//
// 왜 조종석 시점이 아닌가: 함교는 선체 깊숙이 있어서 그 자리에 카메라를 두면 사방이 껍데기라 창밖이 거의 안 보인다
//   (9/20 PIE: "조종석에서 F 로 해도 시야가 외부가 보이지 않아"). 사용자가 F8 로 빠져나가 본 "배 전체가 보이는 그림"이
//   원하는 것이라 배 뒤 위쪽에 둔다.
// 왜 값을 UPROPERTY 로 빼나: 얼마나 뒤로·위로가 좋은지는 눈으로만 정해진다. 처음에는 뒤로 0.55배·위로 0.30배였는데
//   화면의 절반 이상을 배 뒷면이 차지해 답답했다(9/20 사용자: "좀 더 탁 트인 시야였으면"). 0.80배·0.45배로 옮기면
//   선미가 세로 화면의 72% -> 46% 로 줄고 앞쪽 하늘이 열린다. 다음에 또 만질 값이라 빌드 없이 고칠 수 있어야 한다.
//
// 카메라는 배에 "붙어만" 있고 배의 회전은 따라가지 않는다(SetAbsolute). 마우스가 배 주위를 도는 궤도 카메라다
//   (9/21 사용자 결정). 왜 1인칭이 아닌가: 사용자가 원한 것은 계속 "배 전체가 보이는 그림"이고, 1인칭은 선체가
//   시야를 가려 드래곤이 옆이나 뒤에 있을 때 아예 안 보인다.
// 왜 롤을 늘 0 으로 두나: 배는 선회할 때 최대 8도 기운다. 카메라까지 같이 기울면 멀미가 난다.
void UPGShipHelm::GetOrbitPivotAndDistance(FVector& OutPivotWorld, float& OutDistance, float& OutStartPitchDeg) const
{
	// 도는 중심은 배 한가운데의 갑판 높이. 배 원점이 아니라 경계 한가운데라야 뱃머리와 선미가 고르게 담긴다.
	// 높이는 배 지붕보다 위다. 화면 한가운데(=조준점)가 이 점을 지나므로, 갑판 높이에 두면 조준점이 늘 배 몸통 위에 얹혀
	// 무엇을 겨누는지 안 보였다(9/21 사용자: "조준점 다 가려져서 어떻게 쓰라고"). 지붕 위 배 높이의 35% 에 두면
	// 조준점은 배 위 하늘을 가리키고 배는 화면 아래쪽에 담긴다.
	const float PivotZ = static_cast<float>(Ship->HullLocalBounds.Max.Z + Ship->HullLocalBounds.GetSize().Z * 0.35f);
	const FVector PivotLocal(Ship->HullLocalBounds.GetCenter().X, 0.0f, PivotZ);
	// 반지름과 처음 각도는 "예전의 고정 자리"를 중심 기준 극좌표로 바꾼 것이다. 그래야 기본 그림이 예전과 같은
	// 크기로 잡히고, 거기서 마우스로 각도만 바뀐다.
	// 뒤로 물러나는 거리는 꼬리(Min.X)에서 재는 값이므로, 중심에서 재려면 배 반길이를 더해야 한다.
	// 한 번 틀렸던 자리다 — ShipLength*0.80 을 그대로 빗변에 넣었더니 카메라가 218m 가까워져 배가 화면을 꽉 채웠다.
	const float ShipLength = Ship->HullLocalBounds.GetSize().X;
	const FVector DefaultLocal(Ship->HullLocalBounds.Min.X - ShipLength * Ship->ViewBackRatio, 0.0f, Ship->DeckLocalZ + ShipLength * Ship->ViewUpRatio);
	const FVector Offset = DefaultLocal - PivotLocal;
	OutDistance = FMath::Max(static_cast<float>(Offset.Size()), 1000.0f);
	OutStartPitchDeg = -FMath::RadiansToDegrees(FMath::Atan2(Offset.Z, Offset.Size2D()));
	OutPivotWorld = Ship->GetActorTransform().TransformPosition(PivotLocal);
}

void UPGShipHelm::PlaceOrbitCamera(const FRotator& Look)
{
	if (!IsValid(BridgeViewCamera) || !Ship->HullLocalBounds.IsValid)
		return;
	FVector Pivot;
	float Distance = 0.0f;
	float StartPitch = 0.0f;
	GetOrbitPivotAndDistance(Pivot, Distance, StartPitch);
	// 바라보는 방향의 반대쪽으로 반지름만큼 물러난다 — 그러면 화면 한가운데가 늘 배 한가운데다.
	BridgeViewCamera->SetWorldLocationAndRotation(Pivot - Look.Vector() * Distance, Look);
}

void UPGShipHelm::ApplyBridgeViewCamera()
{
	if (!IsValid(BridgeViewCamera) || !Ship->HullLocalBounds.IsValid)
		return;
	BridgeViewCamera->SetFieldOfView(Ship->ViewFieldOfView);
	// 부모(배)의 회전·이동을 물려받지 않게 한다. 자리는 아래에서 월드 좌표로 직접 놓는다.
	BridgeViewCamera->SetAbsolute(true, true, false);
	FVector Pivot;
	float Distance = 0.0f;
	float StartPitch = 0.0f;
	GetOrbitPivotAndDistance(Pivot, Distance, StartPitch);
	PlaceOrbitCamera(FRotator(StartPitch, Ship->GetActorRotation().Yaw, 0.0f));
}

// 마우스가 카메라를 돌린다. 앉아 있는 동안만, 그리고 배가 이번 프레임에 다 움직인 뒤에 부른다
// (Tick 맨 끝) — 먼저 놓으면 카메라가 배보다 한 프레임 뒤처져 덜덜거린다.
// 배를 모는 것은 W/S/A/D 로 따로다. 시야가 배를 따라 돌지 않으므로, 선회하면서도 드래곤을 계속 볼 수 있다.
void UPGShipHelm::TickBridgeView(float DeltaSeconds)
{
	if (!bLocalSeated || !IsValid(BridgeViewCamera) || !Ship->HullLocalBounds.IsValid)
		return;
	const APlayerController* PC = GetLocalPC(); // 앉은 사람 컴퓨터에서만(멀티 9/27 — 예전엔 서버의 첫 플레이어)
	if (!PC)
		return;
	// 마우스 움직임을 여기서 직접 받아 쌓는다.
	//
	// 왜 ControlRotation 을 안 쓰나: 도보 캐릭터의 마우스 입력은 제 스프링암의 상대 회전만 바꾸고
	//   ControlRotation 은 건드리지 않는다(Input/NA_Look_Mouse.cpp — AddControllerYawInput 은 차·탱크·로봇만 쓴다).
	//   그래서 앉은 뒤 ControlRotation 이 TickSeat 에서 넣은 값에 굳고, 궤도 카메라가 한 각도로 멈춰 있었다
	//   (9/21 진단). 자리는 맞는데 도는 것만 안 됐다.
	// 왜 남의 스프링암을 읽지 않나: 그건 ACustomPlayerCharacter 에만 있다. 마우스 움직임을 직접 받으면
	//   어떤 폰을 몰고 있든, 그쪽 입력 방식이 바뀌든 조종석 시야는 그대로 돈다.
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	HelmViewYaw = FRotator::NormalizeAxis(HelmViewYaw + MouseX * Ship->ViewMouseSensitivity);
	HelmViewPitch = FMath::Clamp(HelmViewPitch + MouseY * Ship->ViewMouseSensitivity * (Ship->bViewInvertPitch ? -1.0f : 1.0f),
		Ship->ViewPitchMinDeg, Ship->ViewPitchMaxDeg);
	PlaceOrbitCamera(FRotator(HelmViewPitch, HelmViewYaw, 0.0f));
}

void UPGShipHelm::TickBridge(float DeltaSeconds)
{
	if (BridgeReadouts.IsEmpty())
		return;
	// 계기 글자: 초당 4번만 고친다(매 프레임 글자를 다시 만드는 것도 비용이다).
	BridgeUpdateTimer += DeltaSeconds;
	if (BridgeUpdateTimer >= 0.25f)
	{
		BridgeUpdateTimer = 0.0f;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipAltitude), false, Ship.Get());
		const FVector From = Ship->GetActorLocation();
		const float Altitude = Ship->GetWorld()->LineTraceSingleByChannel(Hit, From, From - FVector(0.0f, 0.0f, 100000.0f), ECC_Visibility, Params)
			? (From.Z - Hit.ImpactPoint.Z) * 0.01f : 0.0f;
		// 실제로 움직인 속도를 보여 준다. 조종석에서 몰면 이 숫자가 따라 움직여야 "먹히고 있다"가 읽힌다.
		const float SpeedKph = Ship->ShipVelocity.Size() * 0.036f; // 이륙은 순수 수직이라 Size2D 로는 0 으로 보인다
		const TCHAR* StateText = Ship->bWrecked ? (Ship->bWreckGrounded ? TEXT("WRECKED") : TEXT("MAYDAY"))
			: Ship->Motion == EPGShipMotion::Cruise ? TEXT("APPROACH")
			: (Ship->Motion == EPGShipMotion::Hover ? (Ship->bSeated ? TEXT("HELM CONTROL") : TEXT("STATION KEEPING")) : TEXT("STANDBY"));
		BridgeReadouts[0]->SetText(FText::FromString(FString::Printf(TEXT("ALT  %.0f m"), Altitude)));
		BridgeReadouts[1]->SetText(FText::FromString(FString::Printf(TEXT("SPD  %.0f km/h"), SpeedKph)));
		BridgeReadouts[2]->SetText(FText::FromString(StateText));
		if (Ship->GetNetMode() == NM_Client && LastLoggedReadout != StateText) // 멀티 확인용: 클라 계기판 글자가 바뀔 때 한 줄
		{
			LastLoggedReadout = StateText;
			UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: bridge readout on this screen — %s, ALT %.0f m, SPD %.0f km/h"), StateText, Altitude, SpeedKph);
		}
		BridgeReadouts[3]->SetText(FText::FromString(Ship->LastTargetDistanceM > 0.0f
			? FString::Printf(TEXT("TGT  %.0f m"), Ship->LastTargetDistanceM)
			: FString::Printf(TEXT("HULL %.0f%%"), Ship->MaxHealth > 0.0f ? Ship->Health / Ship->MaxHealth * 100.0f : 0.0f)));
	}
	// 옆 화면: 사람이 함교 가까이 있을 때만 찍는다.
	if (!IsValid(BridgeCamera) || Ship->ScreenFps <= 0.0f)
		return;
	ScreenCaptureTimer += DeltaSeconds;
	if (ScreenCaptureTimer < 1.0f / Ship->ScreenFps)
		return;
	ScreenCaptureTimer = 0.0f;
	if (Ship->GetNetMode() == NM_DedicatedServer)
		return; // 화면이 없다(멀티 9/27) — 첫 컨트롤러는 원격 사람이라 찍어 봐야 아무도 못 본다
	const APlayerController* PC = Ship->GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (IsValid(Pawn) && FVector::DistSquared(Pawn->GetActorLocation(), GetBridgeWorld()) < FMath::Square(Ship->ScreenActiveRange))
		BridgeCamera->CaptureScene();
}

FVector UPGShipHelm::GetBridgeWorld() const
{
	return Ship->GetActorTransform().TransformPosition(Ship->BridgeLocal + FVector(0.0f, 0.0f, 100.0f));
}

// 조종석 의자: F 로 앉고 일어선다.
//
// 왜 의자인가: 함교는 뱃머리 유리창 안쪽이라 걸어서 찾아가면 창 쪽으로 계속 나아가다 배 밖으로 나가 버린다
//   (9/20 PIE). 사용자 제안대로 "앉으면 조종 화면" 으로 바꾸면 그 문제가 통째로 사라진다 —
//   앉는 동안은 걷기를 막아 두기 때문이다.
// 왜 앉는 애니메이션이 없나: 시점이 곧바로 조종석 눈높이로 옮겨 가고 몸은 의자에 고정된다. 애니가 없어도
//   "앉았다"가 읽힌다. 애니는 나중에 A_SchoolGirl_Drink 처럼 따로 만들어 끼우면 된다.
// 멀티(9/27): 예전 TickSeat 는 서버가 "첫 번째 플레이어 컨트롤러" 의 F 를 직접 읽었다. 전용 서버에는 키보드가 없고 그 사람은
//   원격이라(IsLocallyControlled 가 거짓) 아무도 앉지 못했고, 디렉터는 "앉을 때까지" 기다려 피날레가 떠 있기 단계에서 멈췄다.
//   이제 F 는 그 사람 컴퓨터가 읽고(TickLocalPilot) 서버에 청한다(ServerSeatRequest). 서버가 거리·상태를 보고 앉힌다.

FVector UPGShipHelm::GetSeatWorld() const
{
	return Ship->GetActorTransform().TransformPosition(Ship->BridgeLocal + FVector(-200.0f, 0.0f, 0.0f));
}

APlayerController* UPGShipHelm::GetLocalPC() const
{
	UWorld* World = Ship->GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr; // 클라·듣기 서버·혼자 하는 판에서는 이 컴퓨터 사람
	return (IsValid(PC) && PC->IsLocalController()) ? PC : nullptr;
}

bool UPGShipHelm::IsAtHelm(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->IsA(APGVehiclePawn::StaticClass()))
		return false; // 차를 몬 채로는 주포를 못 쓴다(9/21 차량 세션 지적 — 좌클릭 한 번에 주포와 차 빔이 같이 나갔다)
	if (Ship->bSeated && Ship->SeatedPawn == Pawn)
		return true;
	if (FVector::DistSquared(Pawn->GetActorLocation(), GetBridgeWorld()) < FMath::Square(400.0f))
		return true;
	if (IsValid(Ship->HelmZone))
	{
		TArray<AActor*> Standing;
		Ship->HelmZone->GetOverlappingActors(Standing, APawn::StaticClass());
		return Standing.Contains(Pawn);
	}
	return false;
}

void UPGShipHelm::ServerSeatRequest(APlayerController* PC, bool bSit)
{
	APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	// 부서진 배에는 앉을 수 없다. 앉아 있던 사람은 BeginWreck 이 이미 일으켰다.
	if (!IsValid(Pawn) || Ship->bWrecked)
		return;
	if (!bSit)
	{
		if (Ship->bSeated && Ship->SeatedPawn == Pawn)
			StandUpFromSeat(Pawn, TEXT("F pressed")); // 일어서는 길은 추락 강제 기립과 같은 함수다
		return;
	}
	if (Ship->bSeated)
		return; // 이미 누가 앉아 있다
	// 조종석은 "걸어서" 앉는 자리다. 차를 몬 채로는 앉을 수 없다(W/S/A/D 가 배와 차에 동시에 먹는다 — 9/21 차량 세션 지적).
	// 거리는 조금 넉넉히(700) — 클라가 600 안에서 누른 사이에 서버 위치가 조금 뒤처져 있을 수 있다.
	if (Pawn->IsA(APGVehiclePawn::StaticClass()) || FVector::DistSquared(Pawn->GetActorLocation(), GetSeatWorld()) > FMath::Square(700.0f))
		return;
	Ship->bSeated = true;
	Ship->SeatedPawn = Pawn;
	// 앉은 동안은 캐릭터의 걷기 계산을 끈다.
	// 왜(9/21 로그): 켜 둔 채로 매 프레임 의자에 순간이동만 시켰더니, 걷기 계산이 "전에 밟고 있던 바닥 기준 자리"를
	//   되살려 캐릭터를 조종석에서 90m 뒤(local x 10672, 13093)로 끌어갔다가 다시 의자로 오기를 반복했다. 앉은 채
	//   떨어지기도 했다(speed 2566, z 감소). 일어설 때도 그 자리로 끌려가 갇혔다 — 사용자 "F 누르니 앞으로 못 간다".
	if (ACharacter* Character = Cast<ACharacter>(Pawn); Character && Character->GetCharacterMovement())
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
		Character->GetCharacterMovement()->DisableMovement();
		Character->SetBase(nullptr);
	}
	Ship->HelmThrottle = 0.0f;
	Ship->HelmYawRate = 0.0f;
	Input = FPGHelmInput();
	InputFrom = PC;
	if (Ship->HelmAnchor.IsZero())
		Ship->HelmAnchor = Ship->GetActorLocation(); // 디렉터를 거치지 않고 앉은 경우(콘솔 스폰 등)
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: %s took the helm seat (F to stand, W/S move, A/D turn, LMB cannon, RMB climb)"), *Pawn->GetName());
	Ship->OnRep_Seated(); // 서버 컴퓨터 사람(혼자 하는 판·듣기 서버 방장)이 앉은 것이면 화면 쪽도 여기서 켠다
}

void UPGShipHelm::ServerHelmInput(APlayerController* PC, const FPGHelmInput& InInput)
{
	APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	// 앉은 사람은 모든 입력, 함교에 선 사람은 주포만. 그 밖의 사람이 보낸 것은 버린다(클라가 보낸 값을 그대로 믿지 않는다).
	if (!IsValid(Pawn) || !IsAtHelm(Pawn))
		return;
	const bool bPilot = Ship->bSeated && Ship->SeatedPawn == Pawn;
	if (!bPilot && Ship->bSeated && InputFrom.Get() != PC && InputFrom.IsValid())
	{
		// 앉은 사람이 따로 있으면 선 사람은 주포만 보탠다.
		Input.bFire = InInput.bFire;
		Input.Aim = InInput.Aim;
		return;
	}
	Input = InInput;
	if (!bPilot)
	{
		Input.Throttle = 0.0f;
		Input.Yaw = 0.0f;
		Input.bClimb = false;
	}
	InputFrom = PC;
	InputAt = Ship->GetWorld()->GetTimeSeconds();
}

void UPGShipHelm::TickSeatServer(float DeltaSeconds)
{
	// 앉아 있는 동안은 의자에 붙여 둔다. 배가 떠서 오르내리고 이륙도 하므로 매 프레임 따라가야 한다.
	if (Ship->bSeated && IsValid(Ship->SeatedPawn))
		Ship->SeatedPawn->SetActorLocation(GetSeatWorld() + FVector(0.0f, 0.0f, 95.0f), false, nullptr, ETeleportType::TeleportPhysics);
	else if (Ship->bSeated)
	{
		// 앉은 폰이 사라졌다(나감·죽음) — 자리를 비운다.
		Ship->bSeated = false;
		Ship->SeatedPawn = nullptr;
		Input = FPGHelmInput();
		Ship->OnRep_Seated();
	}
	// 받은 입력이 오래됐으면(보낸 사람이 나감) 버린다. 입력은 바뀔 때마다 오므로, 가만히 누르고 있으면 오래될 수 있다 —
	// 그래서 보내는 쪽이 1초마다 한 번 다시 보낸다(TickLocalPilot).
	if (Ship->GetWorld()->GetTimeSeconds() - InputAt > 2.5 || !InputFrom.IsValid())
		Input = FPGHelmInput();
}

void UPGShipHelm::OnSeatChangedLocal()
{
	APlayerController* PC = GetLocalPC();
	APawn* Mine = PC ? PC->GetPawn() : nullptr;
	const bool bMineNow = Ship->bSeated && IsValid(Mine) && Ship->SeatedPawn == Mine;
	if (bMineNow && !bLocalSeated)
	{
		bLocalSeated = true;
		PC->SetIgnoreMoveInput(true); // 앉은 동안은 걷지 않는다(둘러보는 것은 그대로). 입력은 이 컴퓨터의 것이라 여기서 막는다
		if (ACharacter* Character = Cast<ACharacter>(Mine); Character && Character->GetCharacterMovement() && !Character->HasAuthority())
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
			Character->GetCharacterMovement()->DisableMovement();
		}
		// 배 뒤 위쪽에서 뱃머리를 보는 자리에서 시작한다. 그 뒤로는 마우스가 카메라를 돌린다(TickBridgeView).
		FVector Pivot;
		float Distance = 0.0f;
		float StartPitch = 0.0f;
		GetOrbitPivotAndDistance(Pivot, Distance, StartPitch);
		PC->SetControlRotation(FRotator(StartPitch, Ship->GetActorRotation().Yaw, 0.0f));
		HelmViewPitch = StartPitch;
		HelmViewYaw = Ship->GetActorRotation().Yaw;
		// 시점을 배로 넘긴다. 앉았는데 여전히 내 등만 보이면 "탔다"가 안 읽힌다(9/20 PIE).
		// 넘기기 전에 카메라를 다시 놓는다 — PIE 에서 View* 값을 고친 뒤 일어섰다 앉으면 바로 반영된다.
		ApplyBridgeViewCamera();
		PC->SetViewTargetWithBlend(Ship.Get(), 0.5f, EViewTargetBlendFunction::VTBlend_Cubic);
	}
	else if (!bMineNow && bLocalSeated)
	{
		bLocalSeated = false;
		bAimHasTarget = false; // 일어서면 조준점도 사라진다
		if (PC)
		{
			PC->SetIgnoreMoveInput(false); // 앉을 때 true 로 한 번 올린 것을 한 번 내린다(횟수를 세는 값이라 짝이 맞아야 한다)
			if (IsValid(Mine))
				PC->SetViewTargetWithBlend(Mine, 0.4f, EViewTargetBlendFunction::VTBlend_Cubic);
		}
		if (ACharacter* Character = Cast<ACharacter>(Mine); Character && Character->GetCharacterMovement() && !Character->HasAuthority())
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling); // 바로 밑 함교 바닥을 찾아 내려선다
	}
}

// 이 컴퓨터 사람의 조종석: F 로 앉기/일어서기를 청하고, 앉았거나 함교에 서 있으면 키·조준을 읽어 서버로 보낸다.
void UPGShipHelm::TickLocalPilot(float DeltaSeconds)
{
	APlayerController* PC = GetLocalPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UWorld* World = Ship->GetWorld();
	if (!IsValid(Pawn) || !World)
		return;
	// 앉아 있다가 폰이 바뀌었으면(죽음 등) 화면 쪽을 되돌린다.
	if (bLocalSeated && Ship->SeatedPawn != Pawn)
		OnSeatChangedLocal();
	if (Ship->bWrecked)
	{
		bAimHasTarget = false;
		return;
	}
	const bool bKey = PGKeyPolling::IsDown(PC, EKeys::F);
	const bool bNear = bLocalSeated || FVector::DistSquared(Pawn->GetActorLocation(), GetSeatWorld()) < FMath::Square(600.0f);
	// 눌린 "순간" 만 본다. 계속 누르고 있으면 앉았다 일어섰다를 반복한다.
	if (bKey && !bSeatKeyDown && bNear && World->GetTimeSeconds() - LastSeatToggle > 0.35)
	{
		LastSeatToggle = World->GetTimeSeconds();
		UPGHelmControlComponent::RequestSeat(PC, Ship.Get(), !bLocalSeated);
	}
	bSeatKeyDown = bKey;
	// 원격 클라이언트: 서버가 의자에 붙여 둔 위치는 조종하는 본인에게 복제되지 않는다 — 이 화면에서도 의자에 둔다.
	if (bLocalSeated && !Pawn->HasAuthority())
		Pawn->SetActorLocation(GetSeatWorld() + FVector(0.0f, 0.0f, 95.0f), false, nullptr, ETeleportType::TeleportPhysics);

	const bool bAtHelm = IsAtHelm(Pawn);
	FPGHelmInput Now;
	if (bAtHelm)
	{
		if (bLocalSeated)
		{
			Now.Throttle = (PGKeyPolling::IsDown(PC, EKeys::W) ? 1.0f : 0.0f) - (PGKeyPolling::IsDown(PC, EKeys::S) ? 1.0f : 0.0f);
			Now.Yaw = (PGKeyPolling::IsDown(PC, EKeys::D) ? 1.0f : 0.0f) - (PGKeyPolling::IsDown(PC, EKeys::A) ? 1.0f : 0.0f);
			Now.bClimb = PGKeyPolling::IsDown(PC, EKeys::RightMouseButton);
		}
		Now.bFire = PGKeyPolling::IsDown(PC, EKeys::LeftMouseButton);
		// 조준선에 뭐가 걸렸는지는 쏘지 않을 때도 본다 — 십자가 "지금 쏘면 쫓아간다"를 보여 주려면. 초당 10번이면 깜빡이지 않는다.
		AimScanTime += DeltaSeconds;
		if (AimScanTime >= 0.1f || Now.bFire)
		{
			AimScanTime = 0.0f;
			FVector ViewLocation;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			const FVector ViewEnd = ViewLocation + ViewRotation.Vector() * Ship->CannonRange;
			FHitResult ViewHit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipAim), false, Ship.Get());
			Params.AddIgnoredActor(Pawn);
			const bool bAimHit = World->LineTraceSingleByChannel(ViewHit, ViewLocation, ViewEnd, ECC_Visibility, Params);
			LocalAim = bAimHit ? ViewHit.ImpactPoint : ViewEnd;
			// 미사일이 쫓아갈 만한 표적인가. 폰으로 좁히면 안 된다 — 드래곤(APGDragonBoss)은 AActor 다.
			// 기준은 "움직이는 것": 지면 타일·건물은 Static 이라 걸러지고, 드래곤·차·몬스터는 Movable 이라 걸린다.
			// 배 자신과 배에 붙은 것(갑판 위의 차)도 뺀다 — 내 배에 쏘면 안 된다.
			const AActor* AimActor = bAimHit ? ViewHit.GetActor() : nullptr;
			bAimHasTarget = IsValid(AimActor) && AimActor != Ship.Get() && AimActor != Pawn && !AimActor->IsAttachedTo(Ship.Get())
				&& AimActor->GetRootComponent() && AimActor->GetRootComponent()->Mobility == EComponentMobility::Movable;
			// 조준 보정: 선이 드래곤 몸에 안 걸려도, 드래곤이 조준점 가까이(화면 가운데에서 AimAssistDegrees 안)에 있으면 그걸 표적으로 삼는다.
			// 왜: 드래곤 충돌 캡슐은 몸통만 감싸고 날개는 비어 있다. 날개를 겨누면 선이 허공을 지나 "표적 없음" 이 되고,
			//   주포가 옆으로 빗나갔다(9/21 사용자: "조준점 맞춰도 제대로 드래곤 맞추지도 못하네").
			if (!bAimHasTarget)
			{
				const FVector ViewDir = ViewRotation.Vector();
				for (TActorIterator<APGDragonBoss> It(World); It; ++It)
				{
					APGDragonBoss* Dragon = *It;
					if (!IsValid(Dragon) || Dragon->IsDead())
						continue;
					const FVector To = Dragon->GetActorLocation() - ViewLocation;
					const float Distance = To.Size();
					if (Distance > Ship->CannonRange || Distance < 1.0f)
						continue;
					if (FVector::DotProduct(To / Distance, ViewDir) >= FMath::Cos(FMath::DegreesToRadians(Ship->AimAssistDegrees)))
					{
						bAimHasTarget = true;
						LocalAim = Dragon->GetActorLocation(); // 주포도 드래곤 몸통으로
						break;
					}
				}
			}
		}
		Now.Aim = LocalAim;
	}
	else
	{
		bAimHasTarget = false;
	}
	// 바뀌었을 때, 그리고 누르고 있는 동안은 1초마다 한 번 다시(서버는 2.5초 넘게 소식이 없으면 입력을 버린다).
	const bool bActive = Now.Throttle != 0.0f || Now.Yaw != 0.0f || Now.bClimb || Now.bFire;
	if (!(Now == LastSent) || (bActive && World->GetTimeSeconds() - LastSentAt > 1.0))
	{
		// 서버가 이 컴퓨터가 아니고 함교와 상관없는 곳이면 보낼 것이 없다(빈 입력은 한 번만 보낸다 — 위 비교가 막는다).
		UPGHelmControlComponent::SendInput(PC, Ship.Get(), Now);
		LastSent = Now;
		LastSentAt = World->GetTimeSeconds();
	}
}

// 앉은 사람을 일으킨다. F 로 일어서기(TickSeat)와 추락 때의 강제 기립(BeginWreck)이 같은 길을 쓴다 —
// 걷기를 다시 켜는 순서(순간이동 먼저, 그다음 MOVE_Falling)가 한 번 틀렸던 자리라 두 벌로 두면 한쪽만 고쳐진다.
// Pawn 은 일으킬 폰(TickSeat 은 지금 조종 중인 폰, BeginWreck 은 SeatedPawn). 없으면 표시만 지운다.
void UPGShipHelm::StandUpFromSeat(APawn* Pawn, const TCHAR* Why)
{
	Ship->bSeated = false;
	Ship->SeatedPawn = nullptr;
	Input = FPGHelmInput();
	Ship->HelmThrottle = 0.0f;   // 키를 쥔 채 일어나도 배가 계속 밀리지 않게
	Ship->HelmYawRate = 0.0f;
	// 걷기 입력·시점 되돌리기는 그 사람 컴퓨터에서(OnSeatChangedLocal — 원격이면 복제된 bSeated 로 불린다).
	Ship->OnRep_Seated();
	if (!IsValid(Pawn))
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: left the helm seat (%s) — pilot pawn already gone"), Why);
		return;
	}
	// 의자 뒤로 한 발 물러나며 일어선다(의자 속에 끼지 않게).
	const FVector StandAt = GetSeatWorld() - Ship->GetActorForwardVector() * 250.0f + FVector(0.0f, 0.0f, 95.0f);
	Pawn->SetActorLocation(StandAt, false, nullptr, ETeleportType::TeleportPhysics);
	// 원격이면 선 자리를 그 사람 컴퓨터에도(서버가 옮긴 위치는 조종하는 본인에게 복제되지 않는다).
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()); PC && !PC->IsLocalController())
		PC->ClientSetLocation(StandAt, Pawn->GetActorRotation());
	// 순간이동을 먼저 하고 걷기를 다시 켠다 — 반대로 하면 켜지는 순간 옛 바닥 기준 자리로 또 끌려간다.
	if (ACharacter* Character = Cast<ACharacter>(Pawn); Character && Character->GetCharacterMovement())
	{
		Character->SetBase(nullptr);
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling); // 바로 밑 함교 바닥을 찾아 내려선다
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: left the helm seat (%s) — standing at local %s"),
		Why, *Ship->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation()).ToCompactString());
}

// ---- 조종석에서 배 몰기 ----
//
// 왜 필요한가: 조종석에 앉으면 당연히 몰 수 있다고 기대하는데, 지금까지 배를 움직이는 것은 디렉터의 CruiseTo 뿐이었다
//   (9/20 사용자: "컨트롤도 안 된다 전함. 안 움직이네"). 몰 수 없으면 공중전이 그냥 지켜보는 장면이 된다.
// 왜 느린가: 437m 짜리가 키를 누르는 즉시 움직이면 장난감처럼 보인다. 입력을 그대로 쓰지 않고 목표값으로 천천히
//   보간한다(변신차 TickArcade 의 ArcadeSmooth 와 같은 요령). 눌러도 몇 초 뒤에 붙고, 떼도 몇 초 미끄러진다.
// 고도는 안 건드린다: 공중전 높이가 드래곤 등장·간격 계산과 묶여 있다(9/20 조율). 위아래는 Hover 흔들림만 맡는다.
// Cruise(디렉터가 옮기는 중)에는 아예 안 불린다 — Tick 의 Hover 분기에서만 부른다. 이륙·정박 연출과 조종이 싸우면 둘 다 이상해진다.
void UPGShipHelm::TickHelmDrive(float DeltaSeconds)
{
	// 앉아 있을 때만: 앉은 사람 컴퓨터가 보낸 W/S/A/D(ServerHelmInput). 앉으면 그 컴퓨터에서 걷기가 막히므로 겹치지 않는다.
	const bool bPilot = Ship->bSeated && IsValid(Ship->SeatedPawn);
	const float WantThrottle = bPilot ? FMath::Clamp(Input.Throttle, -1.0f, 1.0f) : 0.0f;
	const float WantYaw = bPilot ? FMath::Clamp(Input.Yaw, -1.0f, 1.0f) : 0.0f;
	Ship->HelmThrottle = FMath::FInterpTo(Ship->HelmThrottle, WantThrottle, DeltaSeconds, Ship->HelmSmooth);
	Ship->HelmYawRate = FMath::FInterpTo(Ship->HelmYawRate, WantYaw, DeltaSeconds, Ship->HelmSmooth);

	const FRotator Rotation = Ship->GetActorRotation();
	const bool bStill = FMath::IsNearlyZero(Ship->HelmThrottle, 0.002f) && FMath::IsNearlyZero(Ship->HelmYawRate, 0.002f);
	if (bStill)
	{
		// 멈췄으면 기울기만 천천히 되돌린다.
		if (!FMath::IsNearlyZero(Ship->Bank, 0.02f))
		{
			Ship->Bank = FMath::FInterpTo(Ship->Bank, 0.0f, DeltaSeconds, 1.5f);
			Ship->SetActorRotation(FRotator(0.0f, Rotation.Yaw, Ship->Bank));
		}
		return;
	}
	// 도는 만큼 기운다(TickCruise 와 같은 규칙: 오른쪽으로 돌면 오른쪽으로 기운다).
	const float NewYaw = FRotator::NormalizeAxis(Rotation.Yaw + Ship->HelmYawRate * Ship->HelmTurnRateDeg * DeltaSeconds);
	Ship->Bank = FMath::FInterpTo(Ship->Bank, Ship->HelmYawRate * Ship->MaxBankDeg, DeltaSeconds, 1.5f);

	FVector Location = Ship->GetActorLocation() + FRotator(0.0f, NewYaw, 0.0f).Vector() * (Ship->HelmThrottle * Ship->HelmSpeed * DeltaSeconds);
	// 맵 밖으로 못 나가게. 맵 좌표를 알 필요 없이 "정박한 자리에서 몇 m" 로 묶는다 — 연출이 끝난 자리 근처를 벗어나지 않는다.
	const FVector FromAnchor(Location.X - Ship->HelmAnchor.X, Location.Y - Ship->HelmAnchor.Y, 0.0f);
	if (FromAnchor.SizeSquared() > FMath::Square(Ship->HelmRangeCm))
	{
		const FVector Edge = Ship->HelmAnchor + FromAnchor.GetSafeNormal() * Ship->HelmRangeCm;
		Location.X = Edge.X;
		Location.Y = Edge.Y;
		Ship->HelmThrottle = 0.0f; // 벽에 닿았으니 더 밀지 않는다
	}
	// Z 는 그대로 둔다 — 바로 뒤에서 Hover 흔들림이 다시 잡는다(오르기는 아래 TickHelmClimb 가 HoverBaseZ 를 올린다).
	Ship->SetActorLocationAndRotation(Location, FRotator(0.0f, NewYaw, Ship->Bank));
}

// 조종석에서 오른쪽 마우스를 누르고 있으면 배가 위로 오른다(9/21 사용자: "오른쪽 마우스 누르면 위쪽으로 고도 높일 수 있게").
// 전에는 오른쪽 마우스가 미사일이었는데 "티도 안 나고 공격도 잘 모르겠다" 해서 미사일을 빼고 이것으로 바꿨다.
// Hover 는 매 프레임 HoverBaseZ 둘레로 흔들어 Z 를 잡으므로, 위치가 아니라 그 기준 높이를 올린다.
// 정박 높이에서 HelmMaxClimbCm 까지만 — "너무 높이 오르지 않게"(9/21 사용자).
void UPGShipHelm::TickHelmClimb(float DeltaSeconds)
{
	UWorld* World = Ship->GetWorld();
	if (!Ship->bSeated || !IsValid(Ship->SeatedPawn) || !World || Ship->Motion != EPGShipMotion::Hover)
		return;
	if (HelmClimbFloorZ <= 0.0f)
		HelmClimbFloorZ = Ship->HoverBaseZ; // 처음 앉았을 때의 높이 — 오르기 한도의 기준
	if (!Input.bClimb) // 앉은 사람의 오른쪽 버튼(서버로 받은 입력)
	{
		// 안 누르면 천천히 내려간다(9/21 사용자: "자동차처럼 점점 내려가게. 유저가 내려서 맵 Exit 쪽으로 나가야 한다").
		// 바닥은 배 밑 땅 + HelmLandClearanceCm. 땅은 배 바로 아래를 선으로 재서 찾는다(배·껍데기 자신은 뺀다).
		// 배의 밑면은 액터 원점보다 HullLocalBounds.Min.Z 만큼 아래라 그만큼 보정한다.
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipLand), false, Ship.Get());
		if (IsValid(Ship->Hull))
			Params.AddIgnoredActor(Ship->Hull);
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (APawn* Rider = It->Get() ? It->Get()->GetPawn() : nullptr)
				Params.AddIgnoredActor(Rider);
		const FVector From = Ship->GetActorLocation();
		const float GroundZ = World->LineTraceSingleByChannel(Ground, From, From - FVector(0.0f, 0.0f, 200000.0f), ECC_Visibility, Params)
			? static_cast<float>(Ground.ImpactPoint.Z) : 20.0f;
		const float Floor = GroundZ + Ship->HelmLandClearanceCm - static_cast<float>(Ship->HullLocalBounds.Min.Z);
		if (Ship->HoverBaseZ > Floor)
		{
			Ship->HoverBaseZ = FMath::Max(Ship->HoverBaseZ - Ship->HelmSinkSpeed * DeltaSeconds, Floor);
			if (Ship->HoverBaseZ <= Floor)
				UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: helm settled %.0f m over the ground (ground z %.0f) — you can walk off now"),
					Ship->HelmLandClearanceCm * 0.01f, GroundZ);
		}
		return;
	}
	const float Before = Ship->HoverBaseZ;
	Ship->HoverBaseZ = FMath::Min(Ship->HoverBaseZ + Ship->HelmClimbSpeed * DeltaSeconds, HelmClimbFloorZ + Ship->HelmMaxClimbCm);
	if (Ship->HoverBaseZ >= HelmClimbFloorZ + Ship->HelmMaxClimbCm && Before < HelmClimbFloorZ + Ship->HelmMaxClimbCm)
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: helm climb reached the ceiling (+%.0f m over the hover height)"), Ship->HelmMaxClimbCm * 0.01f);
}

void UPGShipHelm::TickBeamFade(float DeltaSeconds)
{
	// 빔: 쏜 순간 굵게 번쩍였다가 0.4초 동안 가늘어지며 사라진다(9/21 사용자: "왼쪽 마우스 공격 너무 힘없다").
	//   전에는 0.15초 동안 같은 굵기로 떴다 툭 꺼져서, 쏜 건지 모를 만큼 짧았다.
	if (Ship->LastBeamTime <= 0.0)
		return;
	constexpr double BeamSeconds = 0.4;
	const double Age = Ship->GetWorld()->GetTimeSeconds() - Ship->LastBeamTime;
	if (Age > BeamSeconds)
	{
		Ship->LastBeamTime = -100.0;
		for (UStaticMeshComponent* Component : Ship->CannonBeams)
			if (IsValid(Component))
				Component->SetVisibility(false);
		return;
	}
	const float Thick = FMath::Lerp(14.0f, 1.0f, static_cast<float>(Age / BeamSeconds));
	for (UStaticMeshComponent* Component : Ship->CannonBeams)
		if (IsValid(Component) && Component->IsVisible())
		{
			const FVector Scale = Component->GetRelativeScale3D();
			Component->SetRelativeScale3D(FVector(Scale.X, Thick, Thick));
		}
}

// 서버: 함교에 있는 사람이 보낸 "쏜다" 로 주포를 쏜다(주포 쪽이 재장전 간격을 지킨다).
void UPGShipHelm::TickHelm(float DeltaSeconds)
{
	if (!IsValid(Ship->HelmZone) || Ship->bWrecked || !Input.bFire)
		return; // 부서진 배의 주포는 죽었다(9/21 사용자 요청)
	APlayerController* PC = InputFrom.Get();
	if (!IsValid(PC) || !IsAtHelm(PC->GetPawn()))
		return;
	Ship->Weapons->FireCannon(Input.Aim, PC);
}

// ---- 임시 조준점 ----
//
// 왜 필요한가: 주포와 미사일은 화면 가운데(GetPlayerViewPoint)를 겨누는데, 화면에 그 점을 알려 주는 것이 없어서
//   어디로 나가는지 알 수가 없었다(9/20 사용자: "전함도 공격에 대해서 드래곤을 맞추려면 조준점 있어야겠네").
// 임시 표식이다. 진짜 HUD 는 UI 담당 팀원 영역이라, 그게 붙으면 이 함수와 BeginPlay/EndPlay 의 등록 두 곳만 지우면 된다.
// 통로와 치수는 변신차(UPGFlightKitComponent::DrawCrosshair, 커밋 da9e077)에 맞췄다 — 팔 길이 화면 높이의 1.2%,
//   가운데 빈 곳 0.35배, 두께 2, 하늘색 (0.35, 0.85, 1.0, 0.9). 게임 안에서 조준점이 두 가지로 보이면 안 된다.
//   UDebugDrawService 를 쓰는 이유도 같다: 게임 모드의 HUD 클래스를 건드리면 팀원 UI 와 부딪히고,
//   나중에 진짜 HUD 로 합칠 때 두 구현이 같은 자리에 있어야 한 번에 걷어낼 수 있다.
// 다른 점은 하나: 미사일이 쫓아갈 표적이 걸리면 주황색으로 바뀌고 바깥에 네 귀퉁이 꺾쇠가 붙는다 —
//   "지금 쏘면 쫓아간다"가 읽혀야 한다. 차에는 유도 무기가 없어서 그쪽에는 없는 표시다.
void UPGShipHelm::DrawCrosshair(UCanvas* Canvas, APlayerController* PC)
{
	if (!bLocalSeated || !IsValid(Canvas)) // 앉은 사람 화면에만(멀티 9/27)
		return;
	const float CentreX = Canvas->SizeX * 0.5f;
	const float CentreY = Canvas->SizeY * 0.5f;
	const float Arm = FMath::Max(6.0f, Canvas->SizeY * Ship->CrosshairScreenRatio);
	const float Gap = Arm * 0.35f; // 가운데를 비운다 — 겨누는 점이 선에 가려지면 조준이 어렵다
	const float Thickness = 2.0f;
	const FLinearColor Colour = bAimHasTarget ? FLinearColor(1.0f, 0.55f, 0.1f, 0.9f) : FLinearColor(0.35f, 0.85f, 1.0f, 0.9f);
	Canvas->K2_DrawLine(FVector2D(CentreX - Arm, CentreY), FVector2D(CentreX - Gap, CentreY), Thickness, Colour);
	Canvas->K2_DrawLine(FVector2D(CentreX + Gap, CentreY), FVector2D(CentreX + Arm, CentreY), Thickness, Colour);
	Canvas->K2_DrawLine(FVector2D(CentreX, CentreY - Arm), FVector2D(CentreX, CentreY - Gap), Thickness, Colour);
	Canvas->K2_DrawLine(FVector2D(CentreX, CentreY + Gap), FVector2D(CentreX, CentreY + Arm), Thickness, Colour);
	if (!bAimHasTarget)
		return;
	const float Box = Arm * 1.8f;
	const float Notch = Arm * 0.6f;
	for (int32 Sx = -1; Sx <= 1; Sx += 2)
		for (int32 Sy = -1; Sy <= 1; Sy += 2)
		{
			const FVector2D Corner(CentreX + Sx * Box, CentreY + Sy * Box);
			Canvas->K2_DrawLine(Corner, Corner - FVector2D(Sx * Notch, 0.0f), Thickness, Colour);
			Canvas->K2_DrawLine(Corner, Corner - FVector2D(0.0f, Sy * Notch), Thickness, Colour);
		}
}
