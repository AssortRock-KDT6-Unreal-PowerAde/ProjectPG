#include "Dev/ItemIconTools.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "PreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureCube.h"
#include "ImageUtils.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "ContentStreaming.h"
#endif

#if WITH_EDITOR
namespace ItemIcon
{
	// 그리기 전에 준비: 셰이더·에셋 컴파일을 끝내고, 메시가 쓰는 텍스처를 가장 선명한 단계까지 불러 둔다.
	// (10/4 첫 시도: 머티리얼이 아직 컴파일 중이라 전부 검게 찍혔다.)
	void PrepareMesh(UStaticMesh* Mesh)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
			GShaderCompilingManager->FinishAllCompilation();
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			if (!Slot.MaterialInterface)
				continue;
			TArray<UTexture*> Textures;
			Slot.MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, GMaxRHIFeatureLevel, true);
			for (UTexture* Texture : Textures)
			{
				if (!Texture)
					continue;
				Texture->SetForceMipLevelsToBeResident(30.0f);
				Texture->WaitForStreaming();
			}
		}
		FlushRenderingCommands();
	}

	FVector Axis(int32 Index)
	{
		return Index == 0 ? FVector::XAxisVector : Index == 1 ? FVector::YAxisVector : FVector::ZAxisVector;
	}

	// 카메라 방향 정하기. 돌려주는 값: 앞(보는 방향), 위(그림의 위쪽).
	// 보는 축 = 가장 얇은 축(또는 지정). 남은 두 축 중 긴 쪽을 칸의 긴 쪽에 맞춘다(가로로 긴 칸이면 가로로).
	// 정사각형 칸: 위에서 보면 그림 위쪽 = +Y(예전 썸네일과 같은 방향 — 바지·옷이 바로 섰다), 옆에서 보면 그림 위쪽 = +Z.
	void ChooseView(const FVector& Extent, FIntPoint Grid, int32 ViewAxisOverride, FVector& OutForward, FVector& OutUp)
	{
		int32 Order[3] = { 0, 1, 2 };
		Algo::Sort(Order, [&Extent](int32 A, int32 B) { return Extent[A] < Extent[B]; });
		const int32 OverrideAxis = FMath::Abs(ViewAxisOverride);
		const int32 View = (OverrideAxis >= 1 && OverrideAxis <= 3) ? OverrideAxis - 1 : Order[0];
		int32 Long = -1, Short = -1;
		for (int32 Index : { Order[2], Order[1], Order[0] })
		{
			if (Index == View) continue;
			if (Long < 0) Long = Index; else Short = Index;
		}

		// 위에서(Z 축) 볼 때는 위에서 내려다본다. 옆에서 볼 때는 + 쪽에서 본다. 지정 값이 음수면 - 쪽에서 본다.
		OutForward = ViewAxisOverride < 0 ? Axis(View) : -Axis(View);
		int32 Up;
		if (Grid.X == Grid.Y)
			Up = (View == 2) ? 1 : 2;
		else
			Up = (Grid.Y > Grid.X) ? Long : Short;
		OutUp = Axis(Up);
	}
}
#endif

UTexture2D* UItemIconTools::RenderItemIcon(UStaticMesh* Mesh, const FString& PackagePath, FIntPoint GridSize,
	int32 PixelsPerCell, float Padding, bool bFlipX, bool bFlipY, int32 ViewAxis)
{
#if WITH_EDITOR
	if (!Mesh || PackagePath.IsEmpty())
		return nullptr;
	GridSize.X = FMath::Clamp(GridSize.X, 1, 8);
	GridSize.Y = FMath::Clamp(GridSize.Y, 1, 8);
	PixelsPerCell = FMath::Clamp(PixelsPerCell, 16, 256);
	const int32 OutW = GridSize.X * PixelsPerCell, OutH = GridSize.Y * PixelsPerCell;
	constexpr int32 Super = 4; // 4배로 크게 찍고 줄여서 테두리를 부드럽게
	const int32 CapW = OutW * Super, CapH = OutH * Super;

	ItemIcon::PrepareMesh(Mesh);

	// ① 빈 미리보기 장면(바닥·하늘 없음)에 물건만 놓는다.
	FPreviewScene::ConstructionValues Values;
	Values.SetCreatePhysicsScene(false).SetTransactional(false).SetForceMipsResident(true)
		.SetLightBrightness(4.0f).SetSkyBrightness(1.2f);
	FPreviewScene Scene(Values);
	if (UTextureCube* Sky = LoadObject<UTextureCube>(nullptr, TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")))
		Scene.SetSkyCubemap(Sky); // 쇠붙이(총)가 비칠 하늘. 없으면 총이 새까맣게 찍혔다(10/4 산탄총).

	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(GetTransientPackage());
	MeshComp->SetStaticMesh(Mesh);
	MeshComp->SetForcedLodModel(1);
	MeshComp->bCastDynamicShadow = false;
	Scene.AddComponent(MeshComp, FTransform::Identity);

	const FBox Box = Mesh->GetBoundingBox();
	const FVector Center = Box.GetCenter(), Extent = Box.GetExtent();
	FVector Forward, Up;
	ItemIcon::ChooseView(Extent, GridSize, ViewAxis, Forward, Up);
	const FVector Right = FVector::CrossProduct(Up, Forward);
	const FRotator CameraRot = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();

	// 빛: 카메라 왼쪽 위 앞에서 비추는 주광 + 반대편 보조광. 그림자는 끔(물건 스스로 드리운 그림자가 검은 얼룩으로 찍힘).
	Scene.SetLightDirection(FRotationMatrix::MakeFromX(Forward * 0.6f - Up * 0.6f + Right * 0.5f).Rotator());
	if (Scene.DirectionalLight)
		Scene.DirectionalLight->SetCastShadows(false);
	UDirectionalLightComponent* Fill = NewObject<UDirectionalLightComponent>(GetTransientPackage());
	Fill->SetIntensity(1.5f);
	Fill->SetCastShadows(false);
	Scene.AddComponent(Fill, FTransform(FRotationMatrix::MakeFromX(Forward * 0.5f + Up * 0.3f - Right * 0.7f).Rotator()));

	// ② 직교 카메라로 찍는다(멀리 있는 쪽이 작아지지 않게). 물건이 그림 안에 넉넉히 들어가는 폭으로.
	const float SpanRight = 2.0f * FMath::Abs(FVector::DotProduct(Extent, Right.GetAbs()));
	const float SpanUp = 2.0f * FMath::Abs(FVector::DotProduct(Extent, Up.GetAbs()));
	const float Radius = Extent.Size();

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	Target->RenderTargetFormat = RTF_RGBA16f;
	Target->ClearColor = FLinearColor::Transparent;
	Target->InitAutoFormat(CapW, CapH);
	Target->UpdateResourceImmediate(true);

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
	Capture->TextureTarget = Target;
	Capture->CaptureSource = SCS_SceneColorHDR; // RGB = 색, A = 1 - 덮인 정도(물건 없는 곳 1)
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = FMath::Max(SpanRight, SpanUp * CapW / CapH) * 1.2f + 1.0f;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ShowOnlyComponents.Add(MeshComp);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetAntiAliasing(false);
	Capture->ShowFlags.SetTemporalAA(false);
	Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	Capture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	Capture->PostProcessSettings.bOverride_ReflectionMethod = true;
	Capture->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	Capture->PostProcessBlendWeight = 1.0f;
	Scene.AddComponent(Capture, FTransform(CameraRot, Center - Forward * (Radius * 3.0f + 50.0f)));

	// 앞의 몇 장은 버린다: 장면에 처음 올라간 메시는 그때서야 텍스처를 불러오기(컴파일) 시작해서,
	// 첫 장은 텍스처 자리에 엔진 임시 체크무늬가 찍혔다(10/7). 찍기 → 컴파일·불러오기 끝내기를 몇 번 되풀이한다.
	for (int32 Warmup = 0; Warmup < 3; ++Warmup)
	{
		Capture->CaptureScene();
		FlushRenderingCommands();
		FAssetCompilingManager::Get().FinishAllCompilation();
		IStreamingManager::Get().StreamAllResources(5.0f);
		FlushRenderingCommands();
	}
	Capture->CaptureScene();
	FlushRenderingCommands();

	TArray<FLinearColor> Raw;
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->ReadLinearColorPixels(Raw, FReadSurfaceDataFlags(RCM_MinMax)) || Raw.Num() != CapW * CapH)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemIcon] capture failed for %s"), *Mesh->GetName());
		return nullptr;
	}

	// ③ 덮인 정도(0~1). 보통 A = 1 - 덮임. 귀퉁이(빈 곳) A 가 0 이면 반대로 읽는다.
	const bool bInverted = Raw[0].A > 0.5f;
	TArray<float> Cover;
	Cover.SetNumUninitialized(Raw.Num());
	for (int32 Index = 0; Index < Raw.Num(); ++Index)
		Cover[Index] = FMath::Clamp(bInverted ? 1.0f - Raw[Index].A : Raw[Index].A, 0.0f, 1.0f);

	// 밝기 맞추기: 물건 픽셀의 가장 밝은 색 채널 90% 지점을 0.85 로. 단 배율은 3~10 으로 묶는다.
	// (10/7 첫 시도는 밝기(휘도)로 맞추고 묶지 않아서, 빨간 기름통이 분홍으로·검은 가방이 은색으로 바랬다.)
	TArray<float> Lum;
	int32 MinX = CapW, MinY = CapH, MaxX = -1, MaxY = -1;
	for (int32 Y = 0; Y < CapH; ++Y)
		for (int32 X = 0; X < CapW; ++X)
		{
			const int32 Index = Y * CapW + X;
			if (Cover[Index] < 0.02f)
				continue;
			MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X);
			MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
			if (Cover[Index] > 0.9f)
				Lum.Add(Raw[Index].GetMax());
		}
	if (MaxX < MinX || Lum.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemIcon] %s: nothing visible"), *Mesh->GetName());
		return nullptr;
	}
	Lum.Sort();
	const float Gain = FMath::Clamp(0.85f / FMath::Max(Lum[Lum.Num() * 9 / 10], 1e-6f), 3.0f, 10.0f);

	// ④ 물건 둘레만 잘라 칸 비율 그림 가운데에 맞춰 넣는다(칸 하나당 4×4 점을 모아 평균).
	const float BoxW = MaxX - MinX + 1.0f, BoxH = MaxY - MinY + 1.0f;
	const float Margin = Padding * FMath::Min(OutW, OutH);
	const float Scale = FMath::Min((OutW - 2.0f * Margin) / BoxW, (OutH - 2.0f * Margin) / BoxH); // 결과 픽셀 / 찍은 픽셀
	const float OffX = (OutW - BoxW * Scale) * 0.5f, OffY = (OutH - BoxH * Scale) * 0.5f;
	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(OutW * OutH);
	for (int32 Y = 0; Y < OutH; ++Y)
		for (int32 X = 0; X < OutW; ++X)
		{
			FLinearColor Sum(0, 0, 0, 0);
			for (int32 SY = 0; SY < Super; ++SY)
				for (int32 SX = 0; SX < Super; ++SX)
				{
					const float SrcX = MinX + (X + (SX + 0.5f) / Super - OffX) / Scale;
					const float SrcY = MinY + (Y + (SY + 0.5f) / Super - OffY) / Scale;
					const int32 IX = FMath::FloorToInt(SrcX), IY = FMath::FloorToInt(SrcY);
					if (IX < 0 || IY < 0 || IX >= CapW || IY >= CapH)
						continue;
					const int32 Index = IY * CapW + IX;
					// 색은 이미 덮인 만큼 곱해진 값(빈 곳 = 검정)이라 그대로 더한다.
					Sum.R += Raw[Index].R; Sum.G += Raw[Index].G; Sum.B += Raw[Index].B;
					Sum.A += Cover[Index];
				}
			const float Samples = Super * Super;
			const float Alpha = Sum.A / Samples;
			if (Alpha <= 0.0f)
				continue;
			const FLinearColor Color(
				FMath::Min(1.0f, Sum.R / Sum.A * Gain), FMath::Min(1.0f, Sum.G / Sum.A * Gain), FMath::Min(1.0f, Sum.B / Sum.A * Gain), Alpha);
			const int32 OutX = bFlipX ? OutW - 1 - X : X, OutY = bFlipY ? OutH - 1 - Y : Y;
			Pixels[OutY * OutW + OutX] = Color.ToFColor(true);
		}
	UE_LOG(LogTemp, Display, TEXT("[ItemIcon] %s %dx%d view=(%s) up=(%s) gain=%.2f inverted=%d"), *Mesh->GetName(), OutW, OutH,
		*Forward.ToCompactString(), *Up.ToCompactString(), Gain, bInverted ? 1 : 0);

	// ⑤ 텍스처 에셋으로 저장(UI 용: 압축 EditorIcon, 밉맵 없음).
	UPackage* Package = CreatePackage(*PackagePath);
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
	FCreateTexture2DParameters Params;
	Params.bUseAlpha = true;
	Params.CompressionSettings = TC_EditorIcon;
	Params.bDeferCompression = false;
	Params.bSRGB = true;
	UTexture2D* Texture = FImageUtils::CreateTexture2D(OutW, OutH, Pixels, Package, AssetName, RF_Public | RF_Standalone, Params);
	if (!Texture)
		return nullptr;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->PostEditChange();
	FAssetRegistryModule::AssetCreated(Texture);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString FileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Texture, *FileName, SaveArgs);
	return Texture;
#else
	return nullptr;
#endif
}
