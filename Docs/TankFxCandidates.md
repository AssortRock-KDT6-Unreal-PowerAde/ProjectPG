# 탱크 포 이펙트 후보 (9/18 조사, 아직 연결 안 함)

사용자 결정: 이펙트·소리는 나중에 붙인다(소리는 Wwise). 그때 `APGTankPawn::MulticastShotFx` 에서 디버그 선 대신 아래를 재생하면 된다.
전부 프로젝트 안 에셋. 재생 모습은 아직 눈으로 확인 안 함(조사는 -nullrhi 커맨드렛).

| 용도 | 에셋 | 종류 | 배율 제안 |
|---|---|---|---|
| 포구 화염 | /Game/AK-47/FX/MuzzleFlash/P_AssaultRifle_MuzzleFlash | Cascade | ×3 (LOD 1500 넘으면 거의 안 보임 — 확인 필요) |
| 발사 흙먼지 | /Game/ParagonRampage/FX/Particles/Abilities/Lunge/FX/P_Rampage_Jumpdust | Cascade | ×1.5 |
| 착탄(땅) | /Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld | Cascade | ×1.2 (가장 자연스러움) |
| 착탄(땅, 크게) | /Game/ParagonRampage/FX/Particles/Abilities/Lunge/FX/P_Rampage_Lunge_Impact | Cascade | ×1.5~2 (충격파 링이 약간 판타지) |
| 착탄(물) | P_Rampage_Rock_HitWorld_WaterImpact | Cascade | ×1.2 |
| 남는 연기 | /Game/Fishermans_Cabin/VFX/VFX_Niagara/NS_Smoke | Niagara | ×2~4 |
| 잔불 | /Game/Fishermans_Cabin/VFX/VFX_Niagara/NS_Fireplace | Niagara | 부서진 탱크·로봇 |

- 진짜 불덩이 폭발은 프로젝트에 없음. 필요하면 Starter Content 의 P_Explosion 을 추가하거나 Niagara 템플릿으로 새로 만든다.
- 소리: 쓸 만한 건 AK-47 팩(AK47_Fire_Cue 등)뿐. 폭발·엔진 소리 없음 → Wwise 때 따로 구한다.
