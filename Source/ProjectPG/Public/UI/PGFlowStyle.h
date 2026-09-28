// 흐름 화면(타이틀·로비·결과)과 환경설정·일시 정지 화면이 같이 쓰는 색과 판 모양. (2026-09-22)
//
// 왜 따로 뺐나: 원래 색은 PGFlowWidgets.cpp 안에만 있었다. 환경설정·일시 정지 화면이 다른 cpp 에 생기면서 같은 노랑·같은 판 색을
//   두 군데에 적게 됐는데, 한쪽만 고치면 화면마다 색이 달라진다. 그래서 값은 여기 한 곳에만 둔다.
// 왜 변수 대신 함수인가: 전역 상수를 헤더에 두면 이 헤더를 넣은 cpp 마다 따로 만들어지고, 만들어지는 순서도 보장이 없다.
//   함수 안의 static 은 처음 부를 때 한 번만 만들어진다.
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

namespace PGFlowStyle
{
	// 색은 디자인 값(sRGB)으로 적고 선형으로 바꿔 쓴다. UMG 는 선형 색을 받는다 — 그냥 넣으면 노랑이 허옇게 뜬다.
	inline FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float Alpha = 1.0f)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(R, G, B));
		Color.A = Alpha;
		return Color;
	}

	// 둥근 모서리 단색 브러시. 이미지 없이 색만으로 판·버튼을 그린다.
	inline FSlateBrush FlatBrush(const FLinearColor& Color, float Radius)
	{
		FSlateBrush Brush;
		Brush.DrawAs = Radius > 0.0f ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(Radius, FSlateColor(FLinearColor::Transparent), 0.0f);
		return Brush;
	}

#define PG_FLOW_STYLE_COLOR(Name, R, G, B, A) inline const FLinearColor& Name() { static const FLinearColor Value = Srgb(R, G, B, A); return Value; }
	PG_FLOW_STYLE_COLOR(Accent, 0xE8, 0xC5, 0x47, 1.0f)         // 노란 강조 #E8C547
	PG_FLOW_STYLE_COLOR(AccentHover, 0xF2, 0xD4, 0x62, 1.0f)
	PG_FLOW_STYLE_COLOR(AccentPressed, 0xC9, 0xA8, 0x34, 1.0f)
	PG_FLOW_STYLE_COLOR(Ink, 0x14, 0x14, 0x14, 1.0f)            // 노란 버튼 위 글자
	PG_FLOW_STYLE_COLOR(Text, 0xF2, 0xF2, 0xF2, 1.0f)
	PG_FLOW_STYLE_COLOR(TextDim, 0xA8, 0xA8, 0xA8, 1.0f)
	PG_FLOW_STYLE_COLOR(TextFaint, 0x78, 0x78, 0x78, 1.0f)
	PG_FLOW_STYLE_COLOR(Panel, 0x0E, 0x0F, 0x12, 0.80f)         // 어두운 반투명 판
	PG_FLOW_STYLE_COLOR(PanelSolid, 0x0E, 0x0F, 0x12, 0.94f)    // 다른 화면 위에 겹쳐 뜨는 판(뒤 글자가 비치면 안 된다)
	PG_FLOW_STYLE_COLOR(Bar, 0x08, 0x09, 0x0B, 0.78f)           // 위 메뉴 줄
	PG_FLOW_STYLE_COLOR(Button, 0x1C, 0x1D, 0x22, 0.88f)
	PG_FLOW_STYLE_COLOR(ButtonHover, 0x2C, 0x2E, 0x35, 0.95f)
	PG_FLOW_STYLE_COLOR(RowTint, 0xFF, 0xFF, 0xFF, 0.04f)
	PG_FLOW_STYLE_COLOR(Divider, 0xFF, 0xFF, 0xFF, 0.10f)
	PG_FLOW_STYLE_COLOR(Good, 0x2E, 0x8B, 0x45, 0.92f)
	PG_FLOW_STYLE_COLOR(Bad, 0xA8, 0x26, 0x22, 0.92f)
	PG_FLOW_STYLE_COLOR(Warn, 0xB8, 0x74, 0x14, 0.92f)
	PG_FLOW_STYLE_COLOR(Neutral, 0x44, 0x46, 0x4C, 0.92f)
	PG_FLOW_STYLE_COLOR(GoodText, 0x7C, 0xD6, 0x8E, 1.0f)
	PG_FLOW_STYLE_COLOR(BadText, 0xF0, 0x7A, 0x72, 1.0f)
#undef PG_FLOW_STYLE_COLOR
}
