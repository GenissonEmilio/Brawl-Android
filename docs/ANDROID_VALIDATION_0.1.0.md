# Brawl Android 0.1.0-dev — validação

6 de novembro de 2026. Base upstream BrawlRecompiled
`a77be8847ec9d8cf6128f086fe9c493d105ecbce`. Port experimental; as verificações
abaixo distinguem preparo, testes isolados e validação integrada.

## Entrada e compilação

- ZIP fornecido mantido intacto; RVZ extraído com verificação CRC do ZIP.
- nodtool v2.0.0-alpha.10 obtido da release oficial, com SHA-256 verificado;
  partição de dados extraída com validação de hashes.
- ID RSBE01, revisão 1. Os 126 módulos correspondem ao layout de referência:
  nomes, IDs, tamanhos, BSS e CRC das imagens fixas.
- O `main_translate.dol` preparado corresponde ao SHA-256 fixado pelo upstream:
  `b91ca3e670f6ac2d87050284c81404e2d3e708d1aba5a79b93e0ac9c6e4faafd`.
- 120.674 funções traduzidas; 600 partes C++ com dados de inicialização privados.
  As traduções e os arquivos do disco ficam fora do Git.
- NDK 30, ARMv8-A, Release, Dawn Android estático, SDL3 3.4.4 e dependências
  verificadas. Dependências, runtime e código ARM64 são compilados de verdade.

## Correções

- Touch conectado ao PAD GameCube real do Brawl; o antigo caminho de Wii Remote
  usado pela interface NSMBW não controla este jogo.
- Dois analógicos e botões com função independente por dedo; eixos neutros não
  recebem ruído por arredondamento subpixel de MotionEvent.
- A tela de créditos aceita A/START touch, conserva uma pressão feita antes da
  prontidão e não reaproveita um botão mantido para avançar o próximo diálogo.
- Toques rápidos preservam a borda de pressão até uma leitura PAD. Cancelamento,
  perda de foco e pausa limpam pressões pendentes, botões e eixos.
- Importação valida o DOL antes de substituir a instalação. Troca de diretórios
  conserva backup, faz rollback e recupera transações interrompidas; NAND,
  configurações e cache ficam fora dessa transação.
- Save states ARM64 usam o buffer opaco completo de `_setjmp/_longjmp`, com SP
  capturado separadamente. O backend x86 anterior foi mantido.
- Corrigida a eliminação da área de segurança da pilha: o compilador reduzia um
  array nominal de 64 KiB para uma pequena posição. A montagem real agora reserva
  64 KiB, sem ler a pilha sobrescrita depois da troca de contexto.
- Mantida a thread SDL de 64 MiB exigida pela integração ART/JNI com as fibras.

## Testes concluídos

| Verificação | Resultado e alcance |
|---|---|
| Java de toque | 75 checks; geometria, posse dos dedos, analógicos e botões |
| View Android no POCO | 5 casos, 28 checks; MotionEvents reais com listener de teste, sem motor |
| Estado nativo de toque | 45 checks na implementação de produção; toque rápido, sobreposição, cancelamento, conexão e consumo independente da tela inicial |
| Transação de importação | 13 casos, 74 checks; falhas de rename, recuperação, checksum e preservação de NAND/Config/cache |
| Executáveis ARM64 no POCO | 8 suítes iniciais e uma suíte adicional de jump-buffer passaram |
| PSQ | 5.058.560 verificações e 24 casos de tipos reservados rejeitados corretamente |
| Jump-buffer de produção | Cópia de contexto, rollback real da pilha de uma fibra e comportamento da máscara de sinais |
| Guarda do ensaio ao vivo | 11 checks do parser de foco; fallback para `dumpsys window displays` no API 36 |
| Multitoque no jogo integrado | 38/38 MotionEvents aceitos, em 12 fases; movimentos, X/B/A, C-stick, Z, L/R, combinações com dois dedos e cancelamento |

Os executáveis nativos usam diretório de teste em `/data/local/tmp`, separado dos
dados do aplicativo. As fixtures de save são sintéticas. Passar nesses testes não
comprova uma partida, compatibilidade completa dos save states ou performance.

## Aparelho e dados instalados

POCO M8 Pro 5G, Adreno 810, Android API 36, depuração USB conectada. Pacote
`com.genisson.brawl.android`, separado dos outros ports. Foram transferidos 5.325
arquivos, 7.200.119.840 bytes; o SHA-256 do DOL no aparelho corresponde ao original
validado: `179574a14c484279efef84db58e55fe31bc2e3cf6c99efc0d20e7b322ec088be`.

O APK integrado contém a biblioteca ARM64 real, SDL/Dawn estáticos, recursos de
runtime e o ícone vetorial. Sua assinatura, conteúdo exato de bibliotecas/assets,
arquitetura, alinhamento ELF/ZIP de 16 KiB e ausência de `debuggable` foram
verificados. A instalação final substitui o pacote de preparação e preserva os
dados importados.

## Validação integrada

- Inicialização, créditos por touch, vídeo de abertura e menus originais abertos.
- Seleção de Mario, inclusão de CPU e escolha do Battlefield feitas pelo PAD touch.
- Lutas reais Mario × Pit e Mario × Diddy Kong executadas; tela de resultados
  também alcançada. A demonstração automática foi observada separadamente e não
  foi usada como prova de controle do jogador.
- Nas capturas do ensaio ao vivo foram observados deslocamento, bola de fogo,
  dano no adversário e escudo. O aceite das 38 injeções, sozinho, não comprova
  cada animação, combo ou agarrão; X, C-stick e Z também têm os testes de
  geometria/estado/View listados acima.
- Save normal criado pelo jogo. Os arquivos `autosv0.bin` e `autosv1.bin`
  permaneceram após atualização e reinicialização; o jogo reconheceu a instalação
  existente sem exigir nova criação de save.
- Save state F5 criado durante `scMelee`, com 12 threads; após retomar e mover o
  personagem, F7 restaurou a mesma cena pausada e o processo continuou funcionando.
  Foi um ensaio local, sem afirmar compatibilidade com todas as cenas.
- Saída para a tela inicial do Android e retorno conservaram o processo do jogo.
  `Start Menu` encerrou o processo separado ao sair, liberando sua memória. A
  aplicação final ficou instalada com os dados e saves; pacotes QA e executáveis
  temporários do aparelho foram removidos.

## Performance observada

Depuração USB conectada; 16:9, 1×/480 linhas, efeitos mantidos, interpolação
desativada, tela de 120 Hz. Sem captura de tela durante as janelas medidas.
Os números abaixo são apresentações reais do SurfaceFlinger, não velocidade
da simulação nem a configuração nominal de 60 FPS do menu.

| Sessão de luta, 45 segundos | Apresentações/s | Intervalo p95 | PSS observado |
|---|---:|---:|---:|
| Mario × Pit, primeira sessão, V-Sync ligado | 43,73 | 41,63 ms | 1.039–1.074 MiB |
| Mario × Diddy Kong, nova sessão, V-Sync desligado | 42,42 | 41,63 ms | 578–586 MiB |

A primeira sessão também carregou personagens da demonstração automática antes
da luta. Os cenários, adversários e caches dessas sessões não são idênticos;
esses números não demonstram ganho causal ao desligar V-Sync nem vazamento de
memória. Ensaios adicionais com pausa, restauração e transição para resultados
foram excluídos da comparação de luta. Não foi demonstrado ganho consistente de
V-Sync desligado: o padrão ligado foi mantido e a opção ficou disponível no
launcher, funcionando na próxima inicialização.

Os logs mostram trechos de codificação de render de aproximadamente 16–18 ms
mesmo sem pipelines pendentes. O tempo amplo do jogo também inclui esperas do
renderizador, portanto não permite atribuir todo o custo ao código traduzido.
60 FPS constantes **não foram alcançados**. Não foram reduzidos efeitos nem
resolução para produzir um número melhor.

## Artefato local

`artifacts/Brawl-Android-0.1.0-dev-arm64.apk`, 304.544.453 bytes.

- SHA-256: `07a5e12717d78081bb471d72601316ebe884dbc476b752c30b0f76c8f1c3de52`.
- Certificado SHA-256: `02f71167d475dfa1039260ebbabfdbf7b55f40f86ab212de49b89fc90810e0ac`.
- Versão `0.1.0-dev`, código 1, ARM64, Android 12/API 31 ou superior.

APK, traduções e dados de inicialização gerados a partir da entrada privada são
artefatos locais ignorados pelo Git. A publicação do código-fonte não inclui
esses arquivos nem constitui uma release do APK.

## Limites

Campanha Subspace, todos os personagens/cenários, controle físico, LAN/online e
mods não foram validados. A câmera de luta segue o jogo original; o C-stick controla ataques.
A primeira compilação e criação de pipelines podem levar tempo. O alvo inicial
usa 480 linhas e interpolação desativada. Permanecem quedas e picos de frametime,
especialmente durante carregamento e criação de pipelines. Este é um port
experimental jogável no escopo descrito, não uma validação do jogo inteiro.
