// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectPG.h"
#include "Modules/ModuleManager.h"
#include "Misc/NetworkVersion.h"
#include "Misc/Crc.h"

// 1. 커스텀 버전을 반환할 함수 정의
uint32 MyCustomGetNetworkVersion()
{
    uint32 Version = FCrc::StrCrc32(TEXT("SameVersion561"));
    UE_LOG(LogTemp, Warning, TEXT(">>> Global Custom Network Version Called! Return: %u"), Version);
    return Version;
}

// 2. 커스텀 모듈 클래스 정의
class FProjectPGModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();

        // 엔진 초기화 직후 네트워크 버전 델리게이트 바인딩
        FNetworkVersion::GetLocalNetworkVersionOverride.BindStatic(&MyCustomGetNetworkVersion);
    }

    virtual void ShutdownModule() override
    {
        FDefaultGameModuleImpl::ShutdownModule();
    }
};

// 3. 매크로에 FDefaultGameModuleImpl 대신 우리가 만든 FProjectPGModule을 연결합니다.
IMPLEMENT_PRIMARY_GAME_MODULE(FProjectPGModule, ProjectPG, "ProjectPG");