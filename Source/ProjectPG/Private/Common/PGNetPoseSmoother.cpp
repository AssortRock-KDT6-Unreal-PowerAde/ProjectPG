#include "Common/PGNetPoseSmoother.h"

#include "HAL/IConsoleManager.h"

// 시험용 스위치(9/28): 0 이면 예전처럼 받은 위치로 바로 옮긴다 — 부드럽게 하기 전후를 같은 조건으로 재 보려고.
static TAutoConsoleVariable<int32> CVarPGNetSmooth(TEXT("PG.NetSmooth"), 1,
	TEXT("1 = smooth replicated vehicle poses; the flying-car driver predicts (default). 0 = snap like before. 2 = driver only follows the server (9/28 first try)."));

bool FPGNetPoseSmoother::IsEnabled()
{
	return CVarPGNetSmooth.GetValueOnGameThread() != 0;
}

int32 FPGNetPoseSmoother::Mode()
{
	return CVarPGNetSmooth.GetValueOnGameThread();
}
