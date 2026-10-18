# 타일 맵에서 몬스터 외침·비명을 직접 쏴 보며 듣는 시험 (2026-10-18)
# 무엇을: 데디 서버(타일 맵, GM_InGame) + 클라 2개를 띄운다. 서버는 시험 옵션으로
#         플레이어마다 둘레 18~28m 에 몬스터를 놓고(1마리는 플레이어를 봄, 나머지는 등 돌림), 손에 소총을 쥐어 준다.
#         (시험 옵션 설명: Source/ProjectPG/Private/Dev/PGTestBattleSubsystem.h)
# 순서: 서버 켬 → 서버가 맵을 다 열고 접속을 받을 때까지 기다림(최대 10분) → 클라 2개 접속.
#       클라를 먼저 띄우면 20초 안에 접속 못 해 로비로 튕긴다.
# 창 위치: 보조 모니터(아래쪽 1920x1080)에 나란히.
# 끄기: 창을 닫거나, 이 스크립트가 알려 주는 번호로 Stop-Process.
# 실행: powershell -ExecutionPolicy Bypass -File Tools\test\EnemyVoiceTileTest.ps1
param(
    [string]$Engine = "E:\UE_5.6_Source",
    [int]$Enemies = 4,
    [string]$Rifle = "1010"
)
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$project = Join-Path $root "ProjectPG.uproject"
$exe = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor.exe"
$logs = Join-Path $root ("Saved\EnemyVoiceTileTest\" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force $logs | Out-Null
$map = "/Game/PG/Level/TestLevel_MapGenerating?game=/Game/PG/Blueprint/GM_InGame.GM_InGame_C"
$enemyClass = "/Game/PG/Blueprint/Characters/CBP_CustomEnemyCharacter.CBP_CustomEnemyCharacter_C"
$serverLog = Join-Path $logs "server.log"

# 서버가 맵의 무거운 작업까지 끝냈나("Design navigation" 줄 = 시설 레벨 붙이기·길 검사 끝).
# 왜: "맵 열림" 직후엔 시설 레벨을 붙이느라 한 프레임에 10초 넘게 멈춰서, 그때 접속하면 20초 제한에 걸려 로비로 튕긴다.
# 로그 파일은 서버가 켜지고 조금 뒤에야 생겨서, 파일이 없으면 "아직" 으로 본다.
function Test-ServerReady { (Test-Path $serverLog) -and [bool](Select-String -Path $serverLog -Pattern "Design navigation:" -Quiet) }
$server = Start-Process $exe -PassThru -WindowStyle Minimized -ArgumentList "`"$project`" $map -server -log -unattended -nullrhi -PGTestEnemies=$Enemies -PGTestEnemyClass=$enemyClass -PGTestRifle=$Rifle -abslog=`"$serverLog`""
Write-Host "서버 켬 (번호 $($server.Id)), 맵 여는 중..."
$deadline = (Get-Date).AddMinutes(10)
while ((Get-Date) -lt $deadline -and -not (Test-ServerReady)) {
    if ($server.HasExited) { Write-Host "서버가 꺼짐. 로그: $serverLog"; exit 1 }
    Start-Sleep 3
}
if (-not (Test-ServerReady)) { Write-Host "10분 안에 서버가 맵 준비를 못 끝냄. 로그: $serverLog"; exit 1 }

$clients = @()
foreach ($i in 0, 1) {
    $x = 888 + 962 * $i
    $clients += Start-Process $exe -PassThru -ArgumentList "`"$project`" 127.0.0.1 -game -windowed -WinX=$x -WinY=1470 -ResX=958 -ResY=540 -abslog=`"$(Join-Path $logs "client$($i + 1).log")`""
}
Write-Host "클라 2개 접속 중 (번호 $($clients[0].Id), $($clients[1].Id)). 서버 로그 [PGTest]·[EnemyAI] 줄: $serverLog"
