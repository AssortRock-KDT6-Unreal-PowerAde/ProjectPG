// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#pragma once

#include "Logging/LogMacros.h"

// 몬스터 AI 로그 분류(LogEnemyAI). 실제로 만드는 곳은 CustomEnemyAIController.cpp 한 곳.
//    왜: .cpp 마다 따로 만들면, 패키지 빌드처럼 여러 .cpp 를 한 파일로 묶어 컴파일할 때 이름이 겹쳐 빌드가 깨진다.
//        (에디터 빌드는 고친 파일만 따로 컴파일해서 통과해 버려 못 잡았다.)
DECLARE_LOG_CATEGORY_EXTERN(LogEnemyAI, Log, All);
