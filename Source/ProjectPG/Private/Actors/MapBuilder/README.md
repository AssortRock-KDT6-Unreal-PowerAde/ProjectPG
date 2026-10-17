# MapBuilder

논리 칸(AMapTile) 격자를 보고 실제 맵(타일·시설·상자)을 세운다. 본체는 `../MapBuilder.cpp`, 이 폴더는 일꾼들.

```mermaid
flowchart TD
    A["판 시작 (BeginPlay)<br/>바닥·산 모양 끼우기, 맵 짓기 예약"] --> B["맵 짓기 (TryReserveFootprint)<br/>칸 읽기 → 시설 자리 → 타일 세우기 → 지점 찍기"]
    B --> N["길찾기 지도 담당 (UMapNavigationBuilder::StartBuild, 서버만)<br/>맵 크기 영역 넣고 지도를 뒤에서 굽기 시작"]
    N -. "엔진이 다 구웠다고 알림" .-> NR["길찾기 준비 끝 (HandleGenerationFinished)<br/>로그 seconds=, OnNavigationReady 알림<br/>몬스터를 내보내거나 움직이는 쪽은 이 뒤에"]
    N -. "DA_MapAssets 의 기다리는 시간(90초) 넘음" .-> NX["Error 로그: 남은 굽기 일 수<br/>기다리기는 계속(늦게 끝나면 late=true)"]
    B --> C["시설 레벨 불러오기 시작<br/>레벨이 다 보이면 다음 칸으로 (WatchFacilityDesignLevels)"]
    C --> D{"다 왔나?<br/>(CheckFacilityDesignLevels)"}
    D -- "아직" --> D
    D -- "다 옴" --> E["끼임 정리 (ResolveSafety)"]
    D -- "못 불러옴 / 30초 넘음" --> X["Error 로그: 안 온 레벨 이름"]
    X --> E
    E --> F["상자 놓기 (SpawnLootOnce)<br/>서버만 놓고 클라는 받은 수만 셈"]
    F --> G["검사기 한 번 (RunFinalChecks)"]
    G --> H["시간 걸리는 검사, 0.1초 타이머 (RunTimedChecks)<br/>다 끝나면 끔, 120초 넘으면 Error 로그 후 끔"]
```

길찾기 지도가 게임 중에 구워지려면 `Config/DefaultEngine.ini` 의 `RuntimeGeneration=Dynamic` 한 줄이 있어야 한다(없으면 영역을 넣어도 지도가 안 생김). 영역 높이·기다리는 시간은 `DA_MapAssets` 의 "길찾기" 칸.

## 0.1초마다 확인하는 것 (Tick) 과 이유

1. **새로 들어온 사람을 시작 구역에 세우기** (서버만)
   - 왜 매번 확인: 한 번에 안 끝날 때가 있다. 캐릭터가 아직 없거나 세울 자리가 없으면 2초 뒤 다시, 캐릭터가 바뀌면 같은 자리에 다시.
   - 나중에 바꿀 것: 매번 확인 대신 "누가 들어왔을 때 / 캐릭터가 생겼을 때 / 2초 뒤 다시"에만 세우기. 바꾸면 서버+2명으로 다시 시험해야 해서 이번엔 그대로 둠.
   - 부담: 기다리는 사람이 없으면 확인 한 번 하고 바로 끝남.
2. **내 캐릭터 주변 60 m 안에서 몬스터 길찾기가 벽을 피하도록 새로 고치기**
   - 왜 매번 확인: 캐릭터는 계속 움직이는데, "40 m 칸을 넘었다"고 엔진이 따로 알려 주지 않는다.
   - 부담: 칸이 바뀔 때만 실제로 새로 고친다. 평소엔 칸 비교 한 번.

흐름이 헷갈리면 Rider 의 Find Usages 로 함수를 거꾸로 따라가면 된다.

## 고친 뒤 확인 (같은 시드 시험)

`powershell -ExecutionPolicy Bypass -File Tools\test\MapSeedCheck.ps1` → 빌드하고 서버+클라를 띄워 상자 수·지문을 `Tools\test\MapSeedExpected.txt` 와 비교한다(약 4분). 맵을 일부러 바꿨으면 기대값 파일을 고친다.
