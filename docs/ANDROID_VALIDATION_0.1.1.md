# Validação Android 0.1.1-dev

Sessão local de 7 de novembro de 2026, em POCO M8 Pro 5G, Android 16/API 36,
Adreno 810 e tela de 2772 × 1280. Dados USA RSBE01, revisão 1, fornecidos pelo
jogador e verificados localmente. ROM, dados extraídos, traduções, APKs, chaves,
saves, capturas e logs do aparelho não são publicados neste repositório.

## Verificado no aparelho

- Importação pela interface de 5.325 arquivos, com rotação, saída para segundo
  plano e retomada. A publicação da instalação foi concluída e validada.
- Toque na abertura e no título, seleção do menu principal, Group e Solo,
  retorno pelo cabeçalho e HUD com apenas START nas páginas compatíveis.
- Rules: seleção de Time Limit e alteração de dois para um minuto. Item Switch:
  alteração de frequência de MED para NONE.
- Seleção de Mario, inclusão de CPU e escolha direta de Battlefield. Uma partida
  Mario contra CPU Yoshi chegou à tela de vitória e aos detalhes de resultados.
- Teste de entrada com 38 eventos Android aceitos em 12 fases. A gravação foi
  inspecionada: houve movimento, saltos, especial, smash pelo D-pad, escudos,
  combinações de movimento com ações e dano aplicado à CPU. Aceitação de eventos
  sozinha não foi usada como prova de efeitos no jogo; o agarrão não teve uma
  captura inequívoca nesta sessão.
- O gesto Voltar do Android abriu o menu do aplicativo. Testes do View passaram
  em 65 verificações, incluindo ponteiros independentes, arrasto, dois dedos,
  soltura fora da área, perda de foco, resize e cancelamento.

## Correções posteriores e limites

Os testes integrados ocorreram em builds intermediários de 0.1.1-dev. Após eles,
foram corrigidas a navegação longa entre opções e a saída de Item Switch pelo
seu título visível. Também foi incluída a ponte de arrasto/zoom para o modo foto.
O APK final é recompilado, assinado e verificado, mas esses percursos adicionais
ficaram para o teste manual do jogador, conforme solicitado.

Os adaptadores incluem diálogos comuns, menus genéricos, personagens/cenários,
Rules/More Rules, Item Switch, Movies, Names/teclado/exclusão, Screen/Deflicker,
lista de Controls, pausa fotográfica e resultados. Isso não equivale a validar
todas essas telas: pausa e vitória ainda exibiram o controle completo nas capturas
anteriores, e a prontidão de seus adaptadores precisa ser conferida.

Stage Switch, Sound/Sound Test, Rumble, configuração detalhada de Controls,
Training, Data Clear, transferência Wii Remote e outras páginas especiais ainda
não têm cobertura completa. Modelos ou estados não reconhecidos mantêm o HUD
virtual. Não há garantia de navegação exclusivamente por toque em todo o jogo.
Campanha, todos os cenários, mods e online não foram validados nesta sessão.

Não foi feita uma nova medição de FPS sustentados. O profiler agora usa toda a
janela observada e rejeita quadros antigos do buffer do compositor; os números
históricos de 0.1.0 não foram reutilizados como resultado de 0.1.1.

## Verificações de implementação

Os testes locais cobrem estado GameCube, limiares e cancelamento, toque deliberado,
projeção/perspectiva/Hor+, grafos de navegação, validação FST/REL, recuperação e
exclusão mútua da importação, preferências entre processos e falhas recuperáveis
de armazenamento. O conjunto .NET focado passou em oito casos de IR canônico e
análise ABI; há workflow sem dados privados para os testes de host.

O empacotamento exige o núcleo ARM64 real, os exports JNI de toque, recursos e
licenças do staging, assinatura válida e alinhamento ELF/ZIP de 16 KiB. A versão
tem `versionCode 2`, `versionName 0.1.1-dev` e assinatura local nova. A chave é
preservada fora do repositório para permitir futuras atualizações.
