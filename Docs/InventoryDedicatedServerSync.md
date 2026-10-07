# 로비 WebSocket 갱신 및 InGame 인벤토리 동기화 검증

## 데이터 흐름

- GameMode_InLobby에서는 WebSocket을 활성화하고 강제 로컬 이동 모드를 해제한다. 최초 조회와 이후 재조회 응답 모두 인벤토리·장비·UI에 반영한다.
- 로비의 이동/장착 응답 후에는 GET_INVENTORY로 다시 조회한다. 수신 대기 플래그는 이벤트 전달 전에 해제하며, 수신 장비 스냅샷을 적용할 때 이동/장착 요청을 다시 보내지 않는다.
- GameMode_InGame에서만 다음 최초 로드 및 데디서버 복제 정책을 사용한다.
- 각 로컬 플레이어는 로비 WebSocket 인벤토리 캐시를 사용한다. 캐시가 없다면 최초 GET_INVENTORY 응답을 기다린다.
- PlayerController_InGame이 해당 플레이어의 최초 스냅샷을 서버에 한 번 전달한다. 서버는 컨테이너/아이템 중복, 격자 크기, 배치 겹침, 순환 참조 등을 검사한다.
- 최초 스냅샷의 진위는 백엔드와 대조하지 않는다. 클라이언트가 전달한 초기 보유량을 신뢰하는 개발용 방식이며, 운영용으로는 인증된 백엔드 조회 또는 서명된 데이터 검증이 필요하다.
- 이후 이동은 소유한 PlayerController의 서버 RPC에서 실행한다. 서버의 원본 아이템, 출발 컨테이너, 목적지 배치, 개인 인벤토리 소유권과 보관함 접근 거리/시야를 검사한다.
- PlayerState 인벤토리 스냅샷은 COND_OwnerOnly, InteractActor 인벤토리는 해당 액터가 네트워크상 관련 있는 클라이언트에게 복제한다. UI는 RepNotify 이후 OnInventoryUpdated로 갱신된다.
- 장비도 인벤토리의 장비 슬롯 컨테이너에서 재구성한다. 빈 장비 슬롯에 장착할 수 있으며, 이미 차 있는 슬롯은 먼저 해제해야 한다.
- InGame 서버는 검증을 통과한 PlayerState 초기 스냅샷에 누락된 장비 슬롯을 생성한다. 기존 슬롯 GUID는 보존하고 생성된 슬롯은 소유 클라이언트에 복제한다. Pocket/Stash만 전달하는 빈 계정도 다른 플레이어에게 받은 장비를 장착할 수 있다. 공유 보관함에는 장비 슬롯을 생성하지 않는다.
- 이 변경은 인게임 DB 저장이나 로비 복귀 후 저장을 구현하지 않는다.

## 준비

1. 에디터를 재시작하여 변경된 UPROPERTY/UFUNCTION 및 블루프린트 부모 클래스 변경을 반영한다.
2. 테스트 맵의 GameMode가 GameMode_InGame 파생 클래스인지, PlayerController와 PlayerState가 각각 PlayerController_InGame 및 CustomPlayerState 파생 클래스인지 확인한다.
3. InteractActor 파생 보관함의 Replicates가 활성화되어 있는지 확인한다. 기존 블루프린트가 Replicates=false를 별도로 저장했다면 재설정한다.
4. 보관함 초기 아이템 생성은 서버 권한에서 실행한다. MyActorGuid는 PostInitializeComponents에서 생성되므로 Construction Script가 아닌 BeginPlay 이후 MyActorGuid와 InventoryComp를 사용한다.
5. 서로 다른 계정으로 로그인한 별도 클라이언트 2개와 데디서버를 실행한다. GameInstance 캐시가 필요한 흐름이므로 인게임 맵만 바로 연 PIE에서는 WebSocket 로그인이 선행되어야 한다.

## 다중 클라이언트 시나리오

| 시나리오 | 기대 결과 |
| --- | --- |
| A/B 최초 접속 | 각자 자신의 Pocket/Stash/장비/가방을 표시한다. A의 PlayerState 인벤토리 내용은 B에 복제되지 않는다. |
| A/B가 같은 보관함 열기 | 같은 MyActorGuid와 동일한 항목을 표시한다. |
| A가 보관함 아이템을 Pocket으로 이동 | A의 Pocket에 추가되고 A/B 보관함에서 사라진다. B 개인 인벤토리는 변하지 않는다. |
| A가 개인 아이템을 보관함에 넣기 | A 개인 인벤토리에서 사라지고 A/B 보관함에 같은 위치/회전으로 나타난다. |
| A/B가 같은 아이템을 동시에 획득 | 서버에서 먼저 처리한 요청만 성공한다. 원본에서 이미 사라진 아이템 요청은 거절되며 총 수량은 유지된다. |
| 가득 찬 목적지/겹치는 배치 | 서버가 요청을 거절하며 원본과 목적지 모두 보존된다. |
| 가방 이동 | 가방과 모든 중첩 가방/내부 아이템 컨테이너가 함께 이동한다. 이전 소유자에 내부 아이템이 남지 않는다. |
| 가방을 자기 자신 또는 자신의 하위 가방에 넣기 | 순환 참조로 거절된다. |
| 보관함 → 빈 장비 슬롯 → Pocket/보관함 | 장비 UI와 보관함이 서버 결과에 따라 함께 갱신되며 중복 항목이 생기지 않는다. |
| A 장비 → 공용 보관함 → 장비 없이 접속한 B의 Pocket → B 장착 | B의 서버 생성 장비 슬롯으로 이동하고 A/보관함에는 같은 아이템이 남지 않는다. 보관함에서 B 장비 슬롯으로 직접 이동해도 성공한다. |
| 보관함을 연 채 멀리 이동/벽 뒤 이동 후 드래그 | 서버 접근 검사에서 거절되고 아이템은 원본에 남는다. |
| 다른 플레이어 개인 인벤토리를 RPC 대상으로 지정 | 소유권 검사에서 거절된다. |
| 최초 초기화 RPC 재전송 | 기존 서버 인벤토리를 덮어쓰지 않는다. |
| 이동 후 지연 WebSocket 응답/캐시 재생/다른 보관함 생성 | 서버 인벤토리가 과거 캐시로 복원되지 않는다. |
| 보관함을 비운 뒤 닫기/다시 열기, 늦게 접속한 클라이언트 | 빈 컨테이너를 포함한 최신 서버 상태를 표시한다. |
| 패킷 지연/손실 환경 | 클라이언트의 선행 삭제 없이 서버 상태 도착 후 UI가 갱신된다. |
| 로비 인벤토리 이동 | 기존 WebSocket 요청 경로를 유지한다. |
| 로비 인벤토리 연속 재조회 | 최초 이후 응답도 수량·위치·크기·장비에 반영한다. 제거된 아이템과 해제된 장비가 다시 나타나지 않는다. |
| InGame에서 로비 복귀 | WebSocket 사용 정책을 복원하고 백엔드의 최신 인벤토리를 조회한다. |

## 자동화 테스트

Unreal Session Frontend → Automation에서 `ProjectPG.Inventory`를 검색하여 실행한다.

- `SnapshotSerialization`: 컨테이너 GUID/크기/장비 슬롯과 아이템 회전·종류·장착 여부 등 리플렉션 직렬화 확인.
- `InitialSnapshotValidation`: 비정상 크기/중복 GUID/누락 루트 거절, 첫 적용 성공, 두 번째 적용 거절, 빈 컨테이너 유지 확인.
- `LobbyRepeatedRefresh`: 반복 수신에 따른 수량·크기 갱신 및 빈 목록 수신 시 이전 아이템 제거 확인.
- `Equipment.Reequip`: 2×3 장비의 반복 장착/해제, 장착 플래그, UI 알림 시점의 인벤토리 일치 및 PlayerState 소유 장비 자동 해제 확인.
- `Equipment.FailurePreservesState`: 원본이 없는 장착과 공간이 없는 해제를 거절하고 기존 장비·아이템·UI 알림 상태를 유지하는지 확인.
- `Equipment.SnapshotReequip`: 로비 장비/인벤토리 응답 이후 원본을 유지하며 해제·재장착할 수 있는지 확인.
- `Equipment.SharedTransfer`: InGame 서버 관리 분기에서 빈 B의 장비 슬롯 생성, 기존 A 슬롯 보존, 복제 스냅샷의 슬롯 포함, A 장비 → 공유 컨테이너 → B Pocket → 장착 및 직접 장착을 확인한다. 소유자·장착 플래그·UI 알림·총 아이템 수량도 검사한다.
- `RuntimeTablePackaging`: 실제 TableLoader의 활성 테이블과 ItemTable 1003 행, 항상 쿠킹 설정을 확인한다.

자동화 테스트는 실제 네트워크 복제나 동시 접속을 대체하지 않는다. 위 다중 클라이언트 시나리오를 별도로 실행해야 한다.

## 검증 결과

- ProjectPG 에디터 및 ProjectPGServer Win64 Development 빌드 성공.
- Unreal 명령줄 자동화 테스트 `ProjectPG.Inventory`: 8개 성공, 실패 0개, 미실행 0개. 기존 장비 테스트 3개는 LogTemp Warning 진단 출력 때문에 보고서에서 succeededWithWarnings로 분류된다.
- 결과 파일: `Saved/Automation/SharedEquipment/index.json`, 실행 로그: `Saved/Logs/SharedEquipmentAutomation.log`.
- 로비 회귀 테스트는 컴포넌트에 전달된 응답의 적용을 검증한다. 실제 WebSocket 백엔드 요청/응답과 데디서버·클라이언트 2개 실행은 별도 검증이 필요하다.
- 장비 테스트는 독립적인 아이템 테이블과 UI 갱신 델리게이트를 검증한다. 실제 위젯 아이콘 렌더링은 에디터에서 장착·드래그 해제·재장착으로 별도 확인한다.

## 서버 적용

- 이번 슬롯 수정은 서버 코드 변경이다. 기존 서버를 종료하고 최신 빌드로 다시 실행한 뒤 로비에서 새로 접속한다. 이미 초기화된 세션은 다시 초기화하지 않는다.
- 패키지 서버는 최신 ProjectPGServer 빌드로 다시 스테이징/패키징해야 한다. `Saved/StagedBuilds/WindowsServer`와 이전 `Saved/InventoryTravelValidation/WindowsServer`는 이번 작업에서 교체하지 않았다.
- `/Game/PG/Table` 항상 쿠킹 설정을 유지한다. 이전 테이블 누락 문제와 이번 빈 계정의 슬롯 누락은 별개다. 에디터 빌드만으로 배포 폴더의 서버가 갱신되지는 않는다.
