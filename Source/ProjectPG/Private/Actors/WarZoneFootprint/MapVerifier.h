#pragma once//중복방지

//언리얼 권장 Core쓰고 필요한건 따로 헤더쓰라고 함. Engine쓰면 컴파일 느려짐.
#include "CoreMinimal.h"
// public UObeject 쓰려고.
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
// Generated_body쓰려고.
#include "MapVerifier.generated.h"

// 트래지언트 : 잠깐쓰는. 디스크나 레벨에 저장하지 않는다. 
UCLASS(Transient)
// 상속 UObject 이유: 언리얼 관리 받는 가장 가벼운 클래스
// 현재 클래스는 월드에 놓일 필요가 없는애라서. 
class UMapVerifier : public UObject
{
	// 클래스에 적용될 언리얼 헤더들에 대한 프로그램을 빌드 돌릴시 컴파일 전에 생성.
	GENERATED_BODY()
public:
	// 검사기->맵 제대로 만들어졌는지 확인
	// 그럼 뭐 필요함? 맵 정보.
	// 액터에 손닿아야 하는데 스스로 알수 없다.
	// 맵주소를 받아야 함. 누군가 건네줘야 한다. 받는 함수가 있어야 함.
	// 함수 괄호 안에 맵 액터 주소니까 
	// 받는 주소 어디에? 검사함수들이 계속 써야해서 맴버 변수 Map에 적어둔다.
	// Init : 처음 채워넣기
	// InMap : 밖에서 들어온 값. 언리얼 습관
	void Init(AWarZoneFootprintPreview* InMap);
	// 부모소속 함수. 그걸 바꿔써야함
	// UBobject에 이미 있는데 자기가 어느 월드에 있는지 모름
	// 검사기 선쏘기, 길찾기 확인 같은일에 월드가 필요. 
	// 그래서 맵 액터의 월드를 빌려 쓴다. 
	// 모양도 부모와 같아야한다. 
	// 빌려쓰기다. 내 월드는 맵액터의 월드라고 대답
	virtual UWorld* GetWorld()const override;
	// PCG뿌리는 기계.
	// DressingPCGComponent 맵 액터에 달린 풀 뿌리는 기계.
	// 맵 액터(AWarZoneFootprintPreview)의 Tick 이 매 프레임 부른다.
	// bLoggedPCGDressing 가 true 면 바로 끝낸다. 아니면 true 로 바꾸고 로그를 찍는다.
	void VerifyPCGDressing();
	// 칸마다 위에서 아래로 막대기(선)을 꽂아 바닥 구멍 찾기. 
	void VerifyWorldCollision();
	// DesignLevel : 따로 만든 시설 조각
	// Separation : 떨어져 있음
	// 큰 시설이 서로서로 잘 떨어져 있는가? 
	// 겹친 짝이 하나라도 있으면 로그에 pass=false라고 뜸
	void VerifyDesignLevelSeparation();
	// Tactical(전투용) Layout(배치) Quality(품질)
	// 타일이 깔린 뒤, 플레이어가 막히거나 끼이거나 빠지는 곳이 없나 확인.
	void VerifyTacticalLayoutQuality();
	// Travel(이동) + Cove(엄폐물, 몸을 숨길 것) + Density(촘촘함) 
	// = 돌아다니는 길에 숨을 곳이 충분히 있나?
	void VerifyTravelCoverDensity();
	// GamePlayPoint(게임지점:시작,상자,몬스터,출구,퀘스트자리)
	// Distribution(퍼진모양)= "게임 지점들이 골고루 알맞게 퍼져있나?"
	void VerifyGameplayPointDistribution();
	// 길 찾기. 몬스터 AI가 다닐 길 찾기 지도가 지대로 깔림? 
	void VerifyNavigation();
	// Critical(꼭 필요한) Routes(길) = "시작 자리에서 출구랑 큰 건물까지 걸어서 갈 수 있나?"
	// 물에 막혀서 출구에 못 가는 맵이면 탈출을 못 하니까, 판마다 확인한다.
	void VerifyCriticalRoutes();
	// Traversable(걸어서 넘을 수 있는) Elevation(높이) = "출구까지 가는 길에 못 올라가는 턱이 없나?"
	// 출구 검사(VerifyCriticalRoutes)는 칸이 이어졌나만 보고 높이는 안 본다. 이 검사는 높이까지 본다.
	void VerifyTraversableElevation();
	// Coplanar(같은 높이의 평면) Surfaces(바닥면) = "바닥 두 장이 같은 높이에 겹쳐서 깜빡이는 곳이 없나?"
	// 멀리서 보면 바닥이 줄무늬처럼 지글거리는 그래픽 버그(Z-파이팅)를 찾는다.
	void VerifyCoplanarSurfaces();
	// Local(이 컴퓨터) Performance(성능) = "게임이 너무 버벅이지 않나?"
	// 맵이 다 깔린 뒤 50프레임 평균 fps·액터 수·메모리를 로그에 남긴다(합격/불합격 없이 숫자만).
	void VerifyLocalPerformance(float DeltaSeconds);

	
private:
	// 언리얼 한테 포인터 관리해달라.
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;	
	
	// 플래그라는 말로도 씀.
	// 덤불 검사를 이미 했는지 표시. 로그를 한 번만 찍으려고.
	bool bLoggedPCGDressing = false;
	// 시설끼리 겹침 검사 보고서를 이미 썼는지 표시. 로그를 한 번만 찍으려고.
	bool bLoggedDesignLevelSeparation = false;
	// 타일 배치  품질 검사 보고서를 이미 썼는지 표시.
	bool bLoggedTacticalLayoutQuality = false;
	// 숨을 곳이 충분한가? 
	bool bLoggedTravelCoverDensity = false;
	// 스티커가 이미 true거나
	// 목록이 아직 비었다. 
	// 지점 안전 관리가 아직 안끝났다. 
	// 셋다 통화하면 스티커를 ture로 붙이고 검사해서 로그찍음
	bool bLoggedGameplayPointDistribution = false;
	// 바닥 구멍 검사 끝남
	bool bLoggedWorldCollision = false;
	// "출구까지 갈 수 있나" 검사 보고서를 이미 썼는지 표시. 로그를 한 번만 찍으려고.
	bool bLoggedCriticalRoutes = false;
	// "못 올라가는 턱" 검사 보고서를 이미 썼는지 표시. 로그를 한 번만 찍으려고.
	bool bLoggedTraversableElevation = false;
	// "바닥 겹침 깜빡임" 검사 보고서를 이미 썼는지 표시. 로그를 한 번만 찍으려고.
	bool bLoggedCoplanarSurfaces = false;
	// 길찾기 지도 검사 끝남. 출구 검사·성능 검사가 이걸 보고 시작한다(순서 신호).
	bool bLoggedNavigation = false;
	// 성능 검사용 수첩: 지금까지 잰 프레임 수(50장 되면 로그).
	int32 PerformanceSampleCount = 0;
	// 성능 검사용 수첩: 잰 프레임들에 걸린 시간 합계(초). 평균 = 합계 ÷ 프레임 수.
	double PerformanceDeltaSecondsTotal = 0.0;

};

