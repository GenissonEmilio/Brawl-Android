# Brawl Android

Adaptação Android ARM64 do [BrawlRecompiled](https://github.com/josiahg2502-glitch/BrawlRecompiled),
baseada no recompilador estático e runtime WiiCompiled, Aurora/Dawn e SDL3.
O núcleo traduzido é compilado para ARM64. A interface Android originalmente
presente para NSMBW foi adaptada para os comandos GameCube usados pelo Brawl.
Histórico, créditos, GPLv3 e avisos de terceiros permanecem preservados.

## Controles

- Analógico esquerdo: movimento contínuo, incluindo caminhada e movimento completo.
- A: ataque/confirmar; B: especial/voltar; X: pulo; L/R: escudo; Z: agarrar.
- C-stick: ataques direcionais. START: pausa original. ⋮: opções do aplicativo.
- As ações seguem a configuração GameCube do próprio jogo. O port não troca
  personagens automaticamente nem substitui a câmera de luta por uma câmera livre.
- Cada dedo conserva sua função até levantar. Os dois analógicos e os botões
  funcionam de forma independente. Um toque rápido sobrevive até uma leitura PAD;
  cancelamento, perda de foco, pausa e resize descartam entradas pendentes.

Pacote: `com.genisson.brawl.android`. O namespace Java/JNI histórico
`com.wither.nsmbw` foi mantido para preservar a integração SDL existente.
Não compartilha armazenamento com Galaxy, Futurama ou o NSMBW original.

## Dados

A entrada validada é USA `RSBE01`, revisão 1. O jogo extraído contém `sys/` e
`files/`. No aparelho, os dados ficam em
`Android/data/com.genisson.brawl.android/files/game`; saves ficam separados em
`NAND`. A interface aceita a pasta extraída ou um ZIP dessa pasta. Um ZIP contendo
somente RVZ precisa ser extraído com uma ferramenta de disco antes dessa importação.

O arquivo fornecido foi extraído e validado localmente com nodtool. Os 126 RELs
correspondem às identidades/CRCs do layout de referência. O preparo dos módulos
reproduz exatamente o SHA-256 do `main_translate.dol` exigido pelo upstream:
`b91ca3e670f6ac2d87050284c81404e2d3e708d1aba5a79b93e0ac9c6e4faafd`.
O `main.dol` compatível tem SHA-256
`179574a14c484279efef84db58e55fe31bc2e3cf6c99efc0d20e7b322ec088be`.

O preparo das traduções e seus dados binários ocorre localmente a partir da imagem
do jogador. Imagens, arquivos extraídos, traduções geradas, bibliotecas compiladas,
saves, logs de dispositivo e chaves não são versionados nem publicados neste fluxo.

## Compilação em Windows

Ferramentas usadas: .NET SDK 8.0.427, JDK 17, Android SDK 36, NDK 30.0.16248370,
CMake 3.31.6/Ninja e Gradle 8.14. As dependências nativas usam versões e SHA-256
fixados por `tools/provision-brawl-deps.py`. Dawn Android é ligado estaticamente;
o NDK 30 passou pelas verificações das interfaces C/C++ do pacote originalmente NDK 29.

Exemplo na raiz do checkout, com toolchains em `../.tools`:

```powershell
$py = 'PATH_TO_PYTHON_3'
$dotnet = (Resolve-Path ../.tools/dotnet8/dotnet.exe).Path
# nodtool v2.0.0-alpha.10: somente a partição de dados, com validação dos hashes.
nodtool extract --validate --quiet --partition data USER_DISC.rvz ../.tools/private-brawl/Disc
git clone --depth 1 https://github.com/doldecomp/brawl.git ../.tools/brawl-decomp
& $py tools/prepare-brawl-inputs.py --disc ../.tools/private-brawl/Disc `
  --decomp-config ../.tools/brawl-decomp/config/RSBE01_02 `
  --private-work ../.tools/private-brawl --dotnet $dotnet --threads 3
./tools/build-android.ps1 -PythonExe $py -Jobs 4 -TranslatedJobs 4 `
  -BuildDirectory "$PWD/wiicompiled/android-deps-build"
& $py tools/stage-android.py
$env:JAVA_HOME = (Resolve-Path ../.tools/jdk17).Path
$env:ANDROID_HOME = (Resolve-Path ../.tools/android-sdk).Path
Push-Location wiicompiled/android
gradle --no-daemon --max-workers=2 assembleRelease
Pop-Location
```

O arquivo `app-release.apk` sai de `wiicompiled/android/app/build/outputs/apk/release`.
O Gradle usa sua chave local de depuração como fallback. Para distribuir uma
atualização, assine externamente com a mesma chave privada da instalação anterior;
não inclua a chave nem sua senha no checkout. Confira assinatura e alinhamento
com `apksigner verify` e `zipalign -c -P 16 4`, e execute
`python tools/verify-android-apk.py PATH_TO_FINAL_APK`.

Há 120.674 funções traduzidas em 600 partes. A primeira compilação pode demorar;
os objetos e as dependências são reutilizados em builds incrementais. A aplicação
usa ARMv8-A como base, mantendo ajustes específicos de processador explícitos.

## Verificação

- `wiicompiled/android/tests/run-touch-tests.ps1`: lógica Java e compilação do View.
- `wiicompiled/android/tests/run-native-touch-tests.ps1`: estado nativo real de toque.
- `wiicompiled/android/tests/run-import-tests.ps1`: transações e recuperação da importação.
- `tools/run-android-checks.py`: executáveis ARM64 isolados; o runner gerado roda
  no aparelho, fora dos dados do aplicativo.
- `tools/profile-android.py --adb PATH_TO_ADB --seconds 45 --output artifacts/profile.json`:
  apresentações reais via SurfaceFlinger e PSS do processo do jogo.

Em Settings > Display, V-Sync permite comparar FIFO e o modo não bloqueante
disponível no aparelho, sem reduzir resolução ou retirar efeitos. A escolha é
gravada antes da próxima inicialização. O contador do runtime e as apresentações
do compositor medem etapas diferentes; nenhum deles garante sozinho a velocidade
da simulação. A validação registra as condições e os limites das medições.

Os testes de entrada e os executáveis isolados não demonstram uma partida jogável.
Menus, gráficos, gameplay e performance precisam de validação integrada separada.
