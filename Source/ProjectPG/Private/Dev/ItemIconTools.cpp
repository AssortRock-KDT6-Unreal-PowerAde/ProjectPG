#include "Dev/ItemIconTools.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "ObjectTools.h"
#include "ImageUtils.h"
#include "Misc/ObjectThumbnail.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "RenderingThread.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#endif

UTexture2D* UItemIconTools::RenderMeshIcon(UStaticMesh* Mesh, const FString& PackagePath, int32 Size, float OrbitPitch, float OrbitYaw)
{
#if WITH_EDITOR
	if (!Mesh || PackagePath.IsEmpty())
		return nullptr;
	Size = FMath::Clamp(Size, 32, 512);

	// ⓪ 그리기 전에 준비: 셰이더·에셋 컴파일을 끝내고, 메시가 쓰는 텍스처를 가장 선명한 단계까지 불러 둔다.
	//    (10/4 첫 시도: 에디터 명령줄에서는 머티리얼이 아직 컴파일 중이라 전부 검게 찍혔다.)
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

	// 찍는 각도: 메시의 썸네일 각도를 잠깐 바꿔 찍고 돌려놓는다(메시 에셋은 저장하지 않음).
	UThumbnailInfo* OriginalView = Mesh->ThumbnailInfo;
	USceneThumbnailInfo* IconView = NewObject<USceneThumbnailInfo>(GetTransientPackage());
	IconView->OrbitPitch = OrbitPitch;
	IconView->OrbitYaw = OrbitYaw;
	IconView->OrbitZoom = 0.0f;
	Mesh->ThumbnailInfo = IconView;
	ON_SCOPE_EXIT { Mesh->ThumbnailInfo = OriginalView; };

	// ① 에디터 썸네일 그리기로 찍는다(콘텐츠 브라우저 썸네일과 같은 그림). 첫 장은 버리고 두 번 찍는다(빛·그림자 준비).
	{
		FObjectThumbnail Warmup;
		ThumbnailTools::RenderThumbnail(Mesh, Size, Size, ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush, nullptr, &Warmup);
	}
	FObjectThumbnail Thumbnail;
	ThumbnailTools::RenderThumbnail(Mesh, Size, Size, ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush, nullptr, &Thumbnail);
	const TArray<uint8>& Bytes = Thumbnail.GetUncompressedImageData();
	if (Bytes.Num() < Size * Size * 4)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemIcon] thumbnail empty for %s (rendering off?)"), *Mesh->GetName());
		return nullptr;
	}

	// ② 바탕을 투명으로: 네 귀퉁이에서 시작해 "바탕색과 비슷하고 이어진" 점만 지운다(채우기).
	//    색만 보고 지우면(10/4 첫 시도) 검은 총처럼 바탕과 비슷한 물건 안쪽까지 지워지거나, 바탕 얼룩이 점점이 남았다.
	//    이어진 것만 지우면 물건 안쪽은 바탕과 색이 비슷해도 남는다.
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Size * Size);
	FMemory::Memcpy(Pixels.GetData(), Bytes.GetData(), Size * Size * 4);
	const FColor Corners[] = { Pixels[0], Pixels[Size - 1], Pixels[(Size - 1) * Size], Pixels[Size * Size - 1] };
	auto IsBackground = [&Corners](const FColor& C)
	{
		for (const FColor& B : Corners)
			if (FMath::Abs(C.R - B.R) + FMath::Abs(C.G - B.G) + FMath::Abs(C.B - B.B) < 30) // 30: 60 이면 검은 총까지 바탕으로 이어져 지워졌다(10/4)
				return true;
		return false;
	};
	for (FColor& Pixel : Pixels)
		Pixel.A = 255;
	TArray<int32> Stack;
	for (int32 Start : { 0, Size - 1, (Size - 1) * Size, Size * Size - 1 })
		Stack.Add(Start);
	while (Stack.Num() > 0)
	{
		const int32 Index = Stack.Pop(EAllowShrinking::No);
		FColor& Pixel = Pixels[Index];
		if (Pixel.A == 0 || !IsBackground(Pixel))
			continue;
		Pixel.A = 0;
		const int32 X = Index % Size, Y = Index / Size;
		if (X > 0) Stack.Add(Index - 1);
		if (X < Size - 1) Stack.Add(Index + 1);
		if (Y > 0) Stack.Add(Index - Size);
		if (Y < Size - 1) Stack.Add(Index + Size);
	}
	int32 Opaque = 0;
	double Brightness = 0.0;
	for (const FColor& Pixel : Pixels)
	{
		if (Pixel.A)
		{
			++Opaque;
			Brightness += (Pixel.R + Pixel.G + Pixel.B) / 3.0;
		}
	}	// 확인용: 물건이 차지한 비율과 평균 밝기(0~255). 검게 찍히면 밝기가 한 자리.
	UE_LOG(LogTemp, Display, TEXT("[ItemIcon] %s cover=%.0f%% brightness=%.0f corner=(%d,%d,%d)"), *Mesh->GetName(),
		100.0 * Opaque / (Size * Size), Opaque ? Brightness / Opaque : 0.0, Corners[0].R, Corners[0].G, Corners[0].B);

	// ②-1 물건 둘레만 남기고 자른다(여백 4px). 총처럼 긴 물건은 길쭉한 그림이 되어 4×1 칸에 늘려 붙여도 비율이 맞는다.
	int32 MinX = Size, MinY = Size, MaxX = -1, MaxY = -1;
	for (int32 Y = 0; Y < Size; ++Y)
		for (int32 X = 0; X < Size; ++X)
			if (Pixels[Y * Size + X].A)
			{
				MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X);
				MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
			}
	int32 OutW = Size, OutH = Size;
	if (MaxX >= MinX && MaxY >= MinY)
	{
		MinX = FMath::Max(0, MinX - 4); MinY = FMath::Max(0, MinY - 4);
		MaxX = FMath::Min(Size - 1, MaxX + 4); MaxY = FMath::Min(Size - 1, MaxY + 4);
		OutW = MaxX - MinX + 1;
		OutH = MaxY - MinY + 1;
		TArray<FColor> Cropped;
		Cropped.SetNumUninitialized(OutW * OutH);
		for (int32 Y = 0; Y < OutH; ++Y)
			FMemory::Memcpy(&Cropped[Y * OutW], &Pixels[(MinY + Y) * Size + MinX], OutW * sizeof(FColor));
		Pixels = MoveTemp(Cropped);
	}
	// ③ 텍스처 에셋으로 저장(UI 용: 압축 UserInterface2D, 밉맵 없음).
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
