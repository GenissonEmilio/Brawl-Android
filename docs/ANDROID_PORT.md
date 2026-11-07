# Brawl Android

Adaptação Android ARM64 do [BrawlRecompiled](https://github.com/josiahg2502-glitch/BrawlRecompiled),
baseada no recompilador estático e runtime WiiCompiled, Aurora/Dawn e SDL3.
O núcleo traduzido é compilado para ARM64. A interface Android originalmente
presente para NSMBW foi adaptada para os comandos GameCube usados pelo Brawl.
Histórico, créditos, GPLv3 e avisos de terceiros permanecem preservados.

A versão em desenvolvimento é `0.1.1-dev` (`versionCode 2`). A validação histórica
da versão 0.1.0 permanece em [ANDROID_VALIDATION_0.1.0.md](ANDROID_VALIDATION_0.1.0.md).
Os testes integrados e os limites da nova versão estão em
[ANDROID_VALIDATION_0.1.1.md](ANDROID_VALIDATION_0.1.1.md).

## Controles

- Analógico esquerdo: movimento contínuo, incluindo caminhada e movimento completo.
- A: ataque/confirmar; B: especial/voltar; X: pulo; L/R: escudo; Z: agarrar.
- D-pad direito: oito direções do C-stick para ataques, com centro neutro.
  START: pausa original. ⋮ ou o gesto Voltar do Android: opções do aplicativo.
- Nos menus compatíveis, toque no próprio item. Só START permanece no HUD;
  o cabeçalho/título permite retornar nos adaptadores correspondentes.
  Páginas ainda incompatíveis conservam os controles virtuais.
- No modo foto compatível, arraste o fundo para girar a câmera e use dois dedos
  para zoom. Os comandos originais de ajuda continuam sendo alvos de toque.
  Essa integração ainda precisa da validação descrita no relatório.
- As ações seguem a configuração GameCube do próprio jogo. O port não troca
  personagens automaticamente nem substitui a câmera de luta por uma câmera livre.
- Cada dedo conserva sua função até levantar. O analógico, o D-pad e os botões
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

## Compilação em Linux

O fluxo usa Python 3, .NET SDK capaz de compilar `net8.0`, JDK 17, Android SDK 36,
NDK `30.0.16248370`, CMake 3.31.6/Ninja e Gradle 8.14. Instale também Android
Build Tools 35.0.0, usados pelo verificador do APK. Execute os exemplos na raiz
do checkout, ajustando os caminhos das ferramentas já instaladas:

```bash
export JAVA_HOME=/PATH_TO_JDK_17
export ANDROID_HOME=/PATH_TO_ANDROID_SDK
export PATH="$JAVA_HOME/bin:/PATH_TO_GRADLE_8.14/bin:$PATH"
dotnet_exe=/PATH_TO_DOTNET/dotnet
# nodtool v2.0.0-alpha.10: somente a partição de dados, validando os hashes.
nodtool extract --validate --quiet --partition data USER_DISC.rvz ../.tools/private-brawl/Disc
git clone --depth 1 https://github.com/doldecomp/brawl.git ../.tools/brawl-decomp
codex-build -- python3 tools/prepare-brawl-inputs.py \
  --disc ../.tools/private-brawl/Disc \
  --decomp-config ../.tools/brawl-decomp/config/RSBE01_02 \
  --private-work ../.tools/private-brawl --dotnet "$dotnet_exe" --threads 2
codex-build -- python3 tools/build-android.py --sdk "$ANDROID_HOME" \
  --provision-dependencies --jobs 2 --translated-jobs 1
python3 tools/stage-android.py
(cd wiicompiled/android && codex-build -- gradle --no-daemon --max-workers=2 assembleRelease)
python3 tools/verify-android-apk.py \
  wiicompiled/android/app/build/outputs/apk/release/app-release.apk --sdk "$ANDROID_HOME"
```

`build-android.py` aceita hosts Linux e Windows x86_64. A primeira execução com
`--provision-dependencies` prepara as dependências verificadas; nas seguintes,
omita essa opção para reutilizar `../.tools/brawl-deps/sources.cmake`. O build
incremental reutiliza o diretório padrão `wiicompiled/android-deps-build`; use
`--build-dir` e o mesmo `--build-dir` no staging para um diretório alternativo.
O builder recusa caches de outra árvore/toolchain e NDK diferente do fixado,
salvo autorização explícita por `--allow-ndk-version-mismatch`.

O preparo usa `--bounded-ir` internamente por padrão: a SSA canônica permanece
serializada de forma compacta e as funções são decodificadas sob demanda. `--threads` aceita apenas
1 ou 2; reduza para 1 quando necessário. Se houver somente SDK/runtime .NET 10,
acrescente `--dotnet-roll-forward Major` ao preparo para executar a CLI `net8.0`
nesse runtime. Com SDK/runtime 8 essa opção não é necessária.

Neste notebook de 16 GiB, consulte `codex-build --status`, `/proc/meminfo`,
`/proc/pressure/memory` e os processos ativos antes de tarefas pesadas. O comando
`codex-build` representa o wrapper local instalado em `$HOME/.local/bin`, que
deve estar no `PATH`: ele limita o grupo de processos e preserva uma reserva
inicial de 3 GiB para o desktop. Serialize os trabalhos pesados e use no máximo
dois workers. O limite padrão é 2 GiB; só use `codex-build --memory-mib 3072 -- ...`
após medir a necessidade e confirmar a reserva exigida pelo executor. O preparo
das traduções com IR sob demanda precisou de 3 GiB nesta máquina. O link Android
Release omite o DWARF intermediário e limita o lld a dois workers; o incremental
medido usou pico de aproximadamente 1,5 GiB sob limite de 2 GiB. Debug conserva
informações de depuração. Não execute
o mesmo build sem o wrapper para contornar uma recusa.

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
  --private-work ../.tools/private-brawl --dotnet $dotnet --threads 2
./tools/build-android.ps1 -PythonExe $py -Jobs 2 -TranslatedJobs 2 `
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
preserve essa chave local e não a regenere entre atualizações. Não inclua a chave
nem sua senha no checkout. APKs e traduções geradas permanecem locais e privados.
Confira assinatura e alinhamento
com `apksigner verify` e `zipalign -c -P 16 4`, e execute
`python tools/verify-android-apk.py PATH_TO_FINAL_APK`.

Há 120.674 funções traduzidas em 600 partes. A primeira compilação pode demorar;
os objetos e as dependências são reutilizados em builds incrementais. A aplicação
usa ARMv8-A como base, mantendo ajustes específicos de processador explícitos.

## Verificação

- O workflow `android-host-checks.yml` executa testes Python, Java e geometria
  nativa sem dados privados, além de oito casos .NET de armazenamento canônico,
  dicionário sob demanda e análise ABI interprocedural, com até dois workers e
  sem servidor de compilação C#.
- `wiicompiled/android/tests/run-touch-tests.ps1`: lógica Java e compilação do View.
- `wiicompiled/android/tests/run-native-touch-tests.ps1`: estado nativo real de toque.
- `wiicompiled/android/tests/run-import-tests.ps1`: transações e recuperação da importação.
- `tools/run-android-checks.py`: executáveis ARM64 isolados; o runner gerado roda
  no aparelho, fora dos dados do aplicativo.
- `tools/profile-android.py --adb PATH_TO_ADB --seconds 45 --output artifacts/profile.json`:
  apresentações reais via SurfaceFlinger e PSS do processo do jogo.

A instrumentação do View pode ser compilada sem núcleo ou dados privados, usando
`-PbrawlApplicationId=com.genisson.brawl.android.validation -PbrawlFrontendOnly=true`
com `assembleDebug assembleDebugAndroidTest`. O pacote separado é obrigatório;
esse APK de testes não executa o jogo. O APK público continua exigindo o staging
do núcleo real e a verificação de empacotamento.

Em Settings > Display, V-Sync permite comparar FIFO e o modo não bloqueante
disponível no aparelho, sem reduzir resolução ou retirar efeitos. A escolha é
gravada antes da próxima inicialização. O contador do runtime e as apresentações
do compositor medem etapas diferentes; nenhum deles garante sozinho a velocidade
da simulação. A validação registra as condições e os limites das medições.

Os testes de entrada e os executáveis isolados não demonstram uma partida jogável.
Menus, gráficos, gameplay e performance precisam de validação integrada separada.
