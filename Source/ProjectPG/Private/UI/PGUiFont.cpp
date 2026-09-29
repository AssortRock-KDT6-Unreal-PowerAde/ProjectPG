#include "UI/PGUiFont.h"

#include "Fonts/CompositeFont.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Objects/PGObjectTypes.h"
#include "Styling/CoreStyle.h"

namespace
{
	TSharedPtr<FStandaloneCompositeFont> BuildFont()
	{
		TSharedPtr<FStandaloneCompositeFont> Font = MakeShared<FStandaloneCompositeFont>();
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("PG/UI/Fonts"));
		for (const TCHAR* Weight : { TEXT("Regular"), TEXT("SemiBold"), TEXT("Bold") })
		{
			const FString File = Dir / FString::Printf(TEXT("Pretendard-%s.otf"), Weight);
			if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*File))
			{
				UE_LOG(LogPGObjects, Warning, TEXT("PGUiFont: %s not found, falling back to the engine font for '%s'"), *File, Weight);
				continue;
			}
			// LazyLoad: 글자가 처음 그려질 때 필요한 만큼만 읽는다. 1.5MB 짜리 세 개를 시작할 때 통째로 올리지 않는다.
			Font->DefaultTypeface.Fonts.Emplace(FName(Weight), File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		return Font;
	}
}

FSlateFontInfo PGUiFont::Get(int32 Size, FName Typeface)
{
	static const TSharedPtr<FStandaloneCompositeFont> Font = BuildFont();
	const bool bHasFace = Font.IsValid() && Font->DefaultTypeface.Fonts.ContainsByPredicate(
		[Typeface](const FTypefaceEntry& Entry) { return Entry.Name == Typeface; });
	if (!bHasFace)
		return FCoreStyle::GetDefaultFontStyle(Typeface == TEXT("Regular") ? "Regular" : "Bold", Size);
	return FSlateFontInfo(Font, static_cast<float>(Size), Typeface);
}
