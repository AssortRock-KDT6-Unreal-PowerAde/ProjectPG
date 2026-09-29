// 게임 UI 글씨체 한 곳. 위젯은 PGUiFont::Get(크기, 굵기) 만 부른다.
//
// 글꼴 파일은 Content/PG/UI/Fonts 의 Pretendard-{Regular,SemiBold,Bold}.otf (SIL OFL, 같은 폴더에 라이선스).
// 폰트 에셋(.uasset)이 아니라 파일을 직접 읽어 합성 폰트를 만든다.
// 왜: 폰트 에셋 임포트는 에디터 UI(Slate)가 떠 있어야만 돼서 자동화 스크립트로 못 만들었다. 파일 방식은 글꼴을 바꿀 때도
//     같은 이름으로 파일만 덮어쓰면 된다. 단, 패키징할 때는 프로젝트 세팅 > Packaging >
//     "Additional Non-Asset Directories to Copy" 에 PG/UI/Fonts 를 넣어야 파일이 같이 나간다.
// 파일이 없으면 엔진 기본 글꼴(Roboto)로 돌아간다 — 한글은 나오지만 밋밋하다.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

namespace PGUiFont
{
	// Typeface: "Regular" / "SemiBold" / "Bold"
	PROJECTPG_API FSlateFontInfo Get(int32 Size, FName Typeface = TEXT("Regular"));
}
