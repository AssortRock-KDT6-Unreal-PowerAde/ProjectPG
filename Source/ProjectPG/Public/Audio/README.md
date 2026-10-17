# 게임 소리 (메타사운드)

Wwise 없이 엔진 내장 메타사운드만 쓴다. 팀원 누구나 플러그인 없이 받을 수 있다.

## 누가 무엇을 맡나

- **메타사운드 에셋** (`Content/PG/Audio/MetaSounds`): 소리 모양. 층, 필터, 거리별 섞기, 실내 메아리.
- **소리 표 `DA_PGSounds`** (`Content/PG/Audio`): 무엇을 언제 틀지. 총 종류·재질·몽타주 → 소리, 음량, 보폭.
- **C++** (`Audio/`): 사건이 언제·어디서·어떤 재질에서 났는지만 알아내서 넘긴다. 팀원 클래스는 고치지 않는다.
- **BP 자식 `BP_PGCharacterSound`** (선택): `On…` 이벤트를 덮어쓰면 동작을 BP 로 바꿀 수 있다.

## 흐름

```mermaid
flowchart TD
  S[서버: 발사·장전·피격·죽음 몽타주 재생] -->|이미 있는 멀티캐스트| C[모든 클라: 같은 몽타주 재생]
  C -->|몽타주 시작 알림| K[UPGCharacterSoundComponent<br/>클라에만, 캐릭터마다 자동으로 붙음]
  K -->|발사 표시 시각까지 기다림| G[OnGunshot → MS_Gunshot<br/>Wave/FarWave/Indoor 입력]
  K -->|보이기용 선 직접 쏨| I[OnBulletImpact → 재질별 탄착음]
  K -->|소리 표 MontageSounds| M[OnMontageSound → 장전·피격·죽음]
  MV[0.1초마다 이동 확인] --> K
  K -->|걸은 거리 ≥ 보폭| F[OnFootstep → 재질별 발소리]
  K -->|공중 → 땅| L[착지 소리]
  UI[마우스 입력 엿보기] -->|버튼 위에서 누름/올라감| U[UI.Click / UI.Hover]
  BP[BP 이벤트: 문·상자·피날레 등] -->|PlayPGSound 노드| N[Cues 이름 소리]
  G & I & M & F & L & U & N --> P[UPGAudioSubsystem::PlayCue<br/>녹음 하나 고르기 + 음높이 흔들기]
  P --> A[감쇠 에셋 ATT_*: 거리 → 크기·고음 깎기<br/>각 클라 귀 위치 기준이라 사람마다 다르게 들림]
```

## 메타사운드 입력 이름 (C++ 과 맞춰야 함)

- `Wave` (WaveAsset): 가까이서 들리는 녹음. 매번 표의 후보 중 하나.
- `FarWave` (WaveAsset): 멀리서 들리는 녹음(총소리만).
- `Indoor` (Float 0/1): 지붕 아래면 1 → 메아리를 진하게.
- 거리: 메타사운드가 `UE.Attenuation` 인터페이스로 직접 받는다. C++ 은 넘기지 않는다.

## 재질

`Config/DefaultEngine.ini` 의 SurfaceType1~6 = Concrete, Metal, Wood, Dirt, Grass, Flesh 이다.
물리 재질 `PM_*` 를 머티리얼이나 랜드스케이프 레이어에 지정하면 그 바닥에서 소리가 바뀐다.
지정이 안 된 곳은 Default 칸 소리를 쓴다.

## 새 소리 붙이는 법 (C++ 없이)

1. wav 를 `Content/PG/Audio/Waves/<용도>` 에 가져온다.
2. `DA_PGSounds` → `Cues` 에 이름 하나를 추가한다. Sound 는 `MS_OneShot`, Waves 에는 후보를 넣는다.
3. BP 에서 `PlayPGSound(이름, 위치)` 를 부른다. 이 노드는 **모든 클라에서 도는 이벤트**(멀티캐스트·OnRep)에서 불러야 모두에게 들린다.

## 라이선스

- `K_` 는 Kenney(CC0)라 어디든 써도 된다.
- `S_` 는 Sonniss GDC 번들이다. 게임에는 넣을 수 있지만 원본 재배포는 금지다. 공개 저장소에 올릴 때는 빼거나 바꾼다.
