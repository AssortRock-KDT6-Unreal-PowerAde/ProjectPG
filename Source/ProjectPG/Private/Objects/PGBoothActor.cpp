#include "Objects/PGBoothActor.h"

#include "Common/PGVisualSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Objects/PGServiceInteractionActor.h"
#include "TimerManager.h"
#include "Common/PGPhysicsUtil.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Robot/PGRobotCharacter.h"
#include "Components/BoxComponent.h"
#include "Finale/PGAnnounceSubsystem.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* const KitFolder = TEXT("/Game/PG/Objects/Shop/BoothKit/");

	// 부품 이름(키트 v2 README 순서, Base 먼저). 파일 이름 = SM_Booth{종류}_{부품}.
	const TArray<const TCHAR*>& PartNames(EPGBoothKind Kind)
	{
		static const TArray<const TCHAR*> Shop = { TEXT("Base"), TEXT("Frame"), TEXT("WallFront"), TEXT("WallLeft"), TEXT("WallRight"), TEXT("WallBack"), TEXT("Door"), TEXT("Roof"), TEXT("Sign"), TEXT("Counter"), TEXT("Lamp"), TEXT("Props"), TEXT("Glass") };
		static const TArray<const TCHAR*> Parcel = { TEXT("Base"), TEXT("Frame"), TEXT("WallFront"), TEXT("WallLeft"), TEXT("WallRight"), TEXT("WallBack"), TEXT("Roof"), TEXT("Sign"), TEXT("Counter"), TEXT("Lamp"), TEXT("Props"), TEXT("Glass") };
		static const TArray<const TCHAR*> Craft = { TEXT("Base"), TEXT("Frame"), TEXT("WallFront"), TEXT("WallLeft"), TEXT("WallRight"), TEXT("WallBack"), TEXT("Roof"), TEXT("Sign"), TEXT("Counter"), TEXT("Lamp"), TEXT("Props"), TEXT("Workbench"), TEXT("Glass") };
		static const TArray<const TCHAR*> Exchange = { TEXT("Base"), TEXT("WallFront"), TEXT("WallLeft"), TEXT("WallRight"), TEXT("WallBack"), TEXT("Door"), TEXT("Roof"), TEXT("Sign"), TEXT("Counter"), TEXT("Lamp"), TEXT("Props") };
		switch (Kind)
		{
		case EPGBoothKind::Parcel:   return Parcel;
		case EPGBoothKind::Craft:    return Craft;
		case EPGBoothKind::Exchange: return Exchange;
		default:                     return Shop;
		}
	}

	const TCHAR* KindToken(EPGBoothKind Kind)
	{
		switch (Kind)
		{
		case EPGBoothKind::Parcel:   return TEXT("Parcel");
		case EPGBoothKind::Craft:    return TEXT("Craft");
		case EPGBoothKind::Exchange: return TEXT("Exchange");
		default:                     return TEXT("Shop");
		}
	}

	// Folder: 부스 칸 BoothKitFolder("/Game/.../"). 비었으면 원래 폴더.
	UStaticMesh* LoadPart(const FString& Folder, const FString& AssetName)
	{
		FString Dir = Folder.IsEmpty() ? FString(KitFolder) : Folder;
		if (!Dir.EndsWith(TEXT("/")))
			Dir += TEXT("/");
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s%s.%s"), *Dir, *AssetName, *AssetName));
	}
}

APGBoothActor::APGBoothActor()
{
	// 킷 폴더 칸 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 폴더).
	BoothKitFolder.Path = KitFolder;
	// 보호막 기본 모양·재질(9/23). 재질은 Tools/make_booth_barrier.py 가 만든다.
	BarrierMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	BarrierMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Objects/Shop/Materials/M_PGBoothBarrier.M_PGBoothBarrier")));
	// 들어오면 띄우는 안내 기본값(9/23). 상점·제조·택배는 거래 화면이 아직 없어 "준비 중" 을 솔직히 적는다.
	BoothNames.Add(EPGBoothKind::Exchange, NSLOCTEXT("PGBooth", "NameExchange", "교환소"));
	BoothNames.Add(EPGBoothKind::Shop, NSLOCTEXT("PGBooth", "NameShop", "상점"));
	BoothNames.Add(EPGBoothKind::Craft, NSLOCTEXT("PGBooth", "NameCraft", "제조소"));
	BoothNames.Add(EPGBoothKind::Parcel, NSLOCTEXT("PGBooth", "NameParcel", "택배"));
	BoothDescriptions.Add(EPGBoothKind::Shop, NSLOCTEXT("PGBooth", "DescShop", "아이템을 사고파는 곳 (거래 화면 준비 중)"));
	BoothDescriptions.Add(EPGBoothKind::Craft, NSLOCTEXT("PGBooth", "DescCraft", "재료로 장비를 만드는 곳 (제작 화면 준비 중)"));
	BoothDescriptions.Add(EPGBoothKind::Parcel, NSLOCTEXT("PGBooth", "DescParcel", "아이템을 창고로 보내는 곳 (배송 화면 준비 중)"));
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // 멀티(9/27): 큰 건물이라 150m(기본 복제 거리) 밖에서 사라졌다 나타났다. 맵이 600m 라 늘 보낸다
	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);
}

UClass* APGBoothActor::GetSpawnClass()
{
	return UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().BoothClass, APGBoothActor::StaticClass());
}

float APGBoothActor::GetHalfWidth(EPGBoothKind InKind)
{
	if (const UStaticMesh* Base = LoadPart(GetSpawnClass()->GetDefaultObject<APGBoothActor>()->BoothKitFolder.Path, FString::Printf(TEXT("SM_Booth%s_Base"), KindToken(InKind))))
		return FMath::Max(Base->GetBounds().BoxExtent.Y, 100.0f);
	return 180.0f;
}

EPGServiceKind APGBoothActor::ToServiceKind(EPGBoothKind InKind)
{
	switch (InKind)
	{
	case EPGBoothKind::Parcel:   return EPGServiceKind::Courier;
	case EPGBoothKind::Craft:    return EPGServiceKind::Craft;
	case EPGBoothKind::Exchange: return EPGServiceKind::Exchange;
	default:                     return EPGServiceKind::Shop;
	}
}

void APGBoothActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGBoothActor, Kind);
}

void APGBoothActor::BeginPlay()
{
	Super::BeginPlay();
	// 보호 표: 차·로봇·폭발·잔해에 부품이 안 떨어진다(PGPhysicsUtil::TryKnockProp). "PGBooth" 표는 드래곤 붕괴에서만 예외로 지우라는 뜻.
	Tags.AddUnique(PGPhysicsUtil::ProtectedTag);
	Tags.AddUnique(TEXT("PGBooth"));
	BuildParts();
	BuildBarrier();
	{
		FBox Local(ForceInit);
		const FTransform ToLocal = GetActorTransform().Inverse();
		for (const UStaticMeshComponent* Part : Parts)
			if (IsValid(Part))
				Local += Part->Bounds.GetBox().TransformBy(ToLocal);
		BuildEnterZone(Local);
	}
	if (HasAuthority())
	{
		SpawnService();
		// 거래 창구가 부서졌는지 1초마다 본다. 부술 때 알려 주는 신호가 없어서(잔해 시스템은 부품만 숨긴다) 가볍게 확인한다.
		GetWorldTimerManager().SetTimer(StandingTimer, this, &APGBoothActor::CheckStillStanding, 1.0f, true);
	}
}

void APGBoothActor::BuildBarrier()
{
	if (!bBarrier || Parts.IsEmpty())
		return;
	UStaticMesh* Mesh = BarrierMesh.IsNull() ? nullptr : BarrierMesh.LoadSynchronous();
	if (!Mesh)
		return;
	// 부품 전체를 부스 기준으로 잰 상자 + 여유 = 막의 타원.
	FBox Local(ForceInit);
	const FTransform ToLocal = GetActorTransform().Inverse();
	for (const UStaticMeshComponent* Part : Parts)
		if (IsValid(Part))
			Local += Part->Bounds.GetBox().TransformBy(ToLocal);
	if (!Local.IsValid)
		return;
	const FBox MeshBox = Mesh->GetBoundingBox();
	const FVector MeshSize = MeshBox.GetSize();
	const FVector Want(Local.GetSize().X + BarrierMarginCm * 2.0f, Local.GetSize().Y + BarrierMarginCm * 2.0f, BarrierHeightCm);
	const FVector Scale(Want.X / FMath::Max(MeshSize.X, 1.0), Want.Y / FMath::Max(MeshSize.Y, 1.0), Want.Z / FMath::Max(MeshSize.Z, 1.0));
	BarrierPart = NewObject<UStaticMeshComponent>(this, TEXT("Barrier"));
	BarrierPart->SetStaticMesh(Mesh);
	BarrierPart->SetupAttachment(RootScene);
	// 바닥이 부스 바닥에 닿게: 메시 아래끝 × 배율만큼 올린다. 가로는 부품 상자 가운데.
	BarrierPart->SetRelativeLocation(FVector(Local.GetCenter().X, Local.GetCenter().Y, -MeshBox.Min.Z * Scale.Z));
	BarrierPart->SetRelativeScale3D(Scale);
	if (UMaterialInterface* Material = BarrierMaterial.IsNull() ? nullptr : BarrierMaterial.LoadSynchronous())
		BarrierPart->SetMaterial(0, Material);
	else
		BarrierPart->SetVisibility(false); // 재질이 없으면 회색 원통이 보이느니 안 보이는 벽으로
	// 몸(폰)·탈것·물리 물체(잔해·굴러다니는 상자)만 막는다. 총알·시선(Visibility)·카메라는 통과 — 막 너머로 쏘고 창구를 본다.
	BarrierPart->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BarrierPart->SetCollisionObjectType(ECC_WorldDynamic);
	BarrierPart->SetCollisionResponseToAllChannels(ECR_Ignore);
	BarrierPart->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	BarrierPart->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
	BarrierPart->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	BarrierPart->SetCanEverAffectNavigation(true); // 몬스터 길찾기가 막을 돌아가게
	BarrierPart->SetCastShadow(false);
	BarrierPart->ComponentTags.Add(PGPhysicsUtil::ProtectedTag);
	BarrierPart->RegisterComponent();
	LetPlayersThroughBarrier();
	GetWorldTimerManager().SetTimer(BarrierTimer, this, &APGBoothActor::LetPlayersThroughBarrier, 1.0f, true);
	UE_LOG(LogPGObjects, Display, TEXT("PGBooth: %s barrier %.0f x %.0f x %.0f m"), *GetName(), Want.X * 0.01f, Want.Y * 0.01f, Want.Z * 0.01f);
}

void APGBoothActor::BuildEnterZone(const FBox& LocalParts)
{
	if (!LocalParts.IsValid)
		return;
	// 막보다 조금 안쪽 상자. 몸(폰)과 겹칠 때만 알린다 — 막고 부딪히는 일은 하지 않는다.
	const float Inset = FMath::Max(BarrierMarginCm - 50.0f, 100.0f);
	EnterZone = NewObject<UBoxComponent>(this, TEXT("EnterZone"));
	EnterZone->SetupAttachment(RootScene);
	EnterZone->SetBoxExtent(FVector(LocalParts.GetExtent().X + Inset, LocalParts.GetExtent().Y + Inset, 200.0f));
	EnterZone->SetRelativeLocation(FVector(LocalParts.GetCenter().X, LocalParts.GetCenter().Y, 200.0f));
	EnterZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EnterZone->SetCollisionObjectType(ECC_WorldDynamic);
	EnterZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	EnterZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	EnterZone->SetGenerateOverlapEvents(true);
	EnterZone->SetCanEverAffectNavigation(false);
	EnterZone->OnComponentBeginOverlap.AddDynamic(this, &APGBoothActor::OnEnterZoneBeginOverlap);
	EnterZone->RegisterComponent();
}

void APGBoothActor::OnEnterZoneBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 들어온 사람 "자기 화면" 에만 띄운다. 안내 줄은 화면마다 따로라 서버·다른 사람 화면에는 안 뜬다.
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled() || !Pawn->IsLocallyControlled())
		return;
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->TimeSince(LastEnterMessageTime) < EnterMessageCooldownSeconds)
		return;
	LastEnterMessageTime = World->GetTimeSeconds();
	const FText* Name = BoothNames.Find(Kind);
	FText Description = BoothDescriptions.Contains(Kind) ? BoothDescriptions[Kind] : FText::GetEmpty();
	// 교환소: 지금 창구가 받는 것 → 주는 것을 붙인다(조건을 바꿔도 안내가 저절로 맞는다).
	if (Kind == EPGBoothKind::Exchange && IsValid(Service))
		Description = FText::Format(NSLOCTEXT("PGBooth", "ExchangeLine", "{0} (창구 앞에서 F)"), Service->GetExchangeSummary());
	TArray<FText> Lines;
	Lines.Add(Name ? *Name : StaticEnum<EPGBoothKind>()->GetDisplayNameTextByValue(static_cast<int64>(Kind)));
	if (!Description.IsEmpty())
		Lines.Add(Description);
	if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(this))
		Announcer->Announce(Lines);
	UE_LOG(LogPGObjects, Display, TEXT("PGBooth: %s entered %s — \"%s\""), *GetNameSafe(Pawn), *GetName(), *Lines[0].ToString());
}

void APGBoothActor::LetPlayersThroughBarrier()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(BarrierPart))
		return;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		ACharacter* Walker = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		// 로봇은 캐릭터지만 부수는 몸이라 막는다. 차·탱크는 캐릭터가 아니라 원래 막힌다.
		if (!IsValid(Walker) || Walker->IsA<APGRobotCharacter>())
			continue;
		if (UCapsuleComponent* Capsule = Walker->GetCapsuleComponent())
			Capsule->IgnoreComponentWhenMoving(BarrierPart, true);
	}
}

void APGBoothActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Service))
		Service->Destroy();
	Super::EndPlay(EndPlayReason);
}

void APGBoothActor::BuildParts()
{
	UStaticMeshComponent* BasePart = nullptr;
	for (const TCHAR* Part : PartNames(Kind))
	{
		UStaticMesh* Mesh = LoadPart(BoothKitFolder.Path, FString::Printf(TEXT("SM_Booth%s_%s"), KindToken(Kind), Part));
		if (!Mesh)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("%s: booth part SM_Booth%s_%s missing (run Tools/import_shop_booth.py)"), *GetName(), KindToken(Kind), Part);
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, FName(Part));
		Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(RootScene);
		// 부품마다 UCX 충돌 상자가 있다(키트 v2). 걸어서 못 뚫고, 차·로봇이 치면 그 부품만 잔해가 된다.
		Component->SetCollisionProfileName(TEXT("BlockAll"));
		Component->RegisterComponent();
		Parts.Add(Component);
		if (FCString::Strcmp(Part, TEXT("Base")) == 0)
			BasePart = Component;
		else if (FCString::Strcmp(Part, TEXT("Counter")) == 0)
			CounterPart = Component;
	}

	// 창구 모듈(창틀·유리·서랍)은 바닥 부품의 Window 소켓에. 가져오기 때 SOCKET_ 접두어가 빠져 이름은 "Window".
	if (BasePart && BasePart->DoesSocketExist(TEXT("Window")))
	{
		for (const TCHAR* Part : { TEXT("Frame"), TEXT("Glass"), TEXT("Drawer") })
		{
			const FString Name = FString::Printf(TEXT("SM_BoothWindow_%s"), Part);
			UStaticMesh* Mesh = LoadPart(BoothKitFolder.Path, Name);
			if (!Mesh)
				continue;
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, FName(*FString::Printf(TEXT("Window%s"), Part)));
			Component->SetStaticMesh(Mesh);
			Component->SetupAttachment(BasePart, TEXT("Window"));
			// 소켓 배율은 따르지 않는다. 키트 소켓에 100배 배율이 들어 있어서 창틀·유리만 50m 짜리 흰 액자가 됐다(9/20 PIE).
			// 위치·방향은 소켓을 따르고 크기는 부스와 같게.
			Component->SetUsingAbsoluteScale(true);
			Component->SetCollisionProfileName(TEXT("BlockAll"));
			Component->RegisterComponent();
			Component->SetWorldScale3D(GetActorScale3D());
			Parts.Add(Component);
		}
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGBooth: %s kind=%s parts=%d"), *GetName(), KindToken(Kind), Parts.Num());
}

void APGBoothActor::SpawnService()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || Parts.IsEmpty())
		return;
	// 부품 소켓(바닥의 Interact = 손님이 서는 자리, Window = 창구)을 월드 좌표로.
	const UStaticMeshComponent* BasePart = Parts[0];
	const FVector UseWorld = BasePart->DoesSocketExist(TEXT("Interact")) ? BasePart->GetSocketLocation(TEXT("Interact")) : GetActorLocation() + GetActorForwardVector() * 160.0f;
	// 창구 구멍 가운데(바닥에서 50cm 위), 유리보다 20cm 바깥: 시선이 유리에 막혀도 둘레 후보로 잡힌다.
	const FVector WindowWorld = BasePart->DoesSocketExist(TEXT("Window")) ? BasePart->GetSocketLocation(TEXT("Window")) + FVector(0.0f, 0.0f, 50.0f) + GetActorForwardVector() * 20.0f : GetActorLocation() + FVector(0.0f, 0.0f, 150.0f);

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Service = World->SpawnActor<APGServiceInteractionActor>(APGServiceInteractionActor::StaticClass(), FTransform(GetActorRotation(), WindowWorld), Params);
	if (!IsValid(Service))
		return;
	Service->SetServiceKind(ToServiceKind(Kind));
	Service->ConfigureUseArea(UseWorld, WindowWorld);
	Service->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
}

void APGBoothActor::CheckStillStanding()
{
	// 창구(카운터)가 부서져 숨겨졌거나 부스가 거의 다 날아갔으면 거래를 닫는다.
	int32 Visible = 0;
	for (const UStaticMeshComponent* Part : Parts)
		if (IsValid(Part) && Part->IsVisible())
			++Visible;
	const bool bCounterGone = IsValid(CounterPart) && !CounterPart->IsVisible();
	if (bCounterGone || Visible < Parts.Num() / 3)
	{
		if (IsValid(Service))
		{
			UE_LOG(LogPGObjects, Display, TEXT("%s: booth destroyed, closing %s"), *GetName(), *Service->GetName());
			Service->Destroy();
			Service = nullptr;
		}
		GetWorldTimerManager().ClearTimer(StandingTimer);
	}
}
