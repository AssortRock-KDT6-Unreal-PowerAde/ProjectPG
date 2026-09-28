#include "Objects/PGItemReceiverInterface.h"

#include "GameFramework/Actor.h"

namespace
{
	// const 인터페이스 함수도 Execute_ 는 non-const UObject*를 받는다. 캐스트를 한 곳에 모아 둔다.
	UObject* MutableTarget(const AActor* Target)
	{
		return const_cast<AActor*>(Target);
	}
}

bool UPGItemReceiverLibrary::GiveItem(AActor* Target, FName ItemId, int32 Count)
{
	if (!IsValid(Target) || ItemId.IsNone() || Count <= 0)
		return false;
	if (!Target->GetClass()->ImplementsInterface(UPGItemReceiver::StaticClass()))
		return false;
	return IPGItemReceiver::Execute_ReceiveItem(Target, ItemId, Count);
}

bool UPGItemReceiverLibrary::HasItem(const AActor* Target, FName ItemId, int32 Count)
{
	if (!IsValid(Target) || ItemId.IsNone() || Count <= 0)
		return false;
	if (!Target->GetClass()->ImplementsInterface(UPGItemReceiver::StaticClass()))
		return false;
	return IPGItemReceiver::Execute_HasItem(MutableTarget(Target), ItemId, Count);
}

bool UPGItemReceiverLibrary::ConsumeItem(AActor* Target, FName ItemId, int32 Count)
{
	if (!IsValid(Target) || ItemId.IsNone() || Count <= 0)
		return false;
	if (!Target->GetClass()->ImplementsInterface(UPGItemReceiver::StaticClass()))
		return false;
	return IPGItemReceiver::Execute_ConsumeItem(Target, ItemId, Count);
}

FName UPGItemReceiverLibrary::GetPlayerKey(const AActor* Target)
{
	if (!IsValid(Target))
		return NAME_None;
	if (Target->GetClass()->ImplementsInterface(UPGItemReceiver::StaticClass()))
		return IPGItemReceiver::Execute_GetPlayerKey(MutableTarget(Target));
	// 인터페이스가 없으면 액터 이름으로라도 구분한다. 테스트 폰이 여기에 해당한다.
	return Target->GetFName();
}
