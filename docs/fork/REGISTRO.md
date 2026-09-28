# Registro do fork SuperFW (SuperChis Prime / GBA SP)

Este documento é a memória permanente do fork `fefsouza10/superfw`. Ele diz o que
foi planejado, o que já foi adicionado ou alterado em relação ao SuperFW original
(davidgfnet/superfw) e o que ainda falta. **Precisa ser atualizado a cada sessão de
trabalho, antes de ela terminar.**

- Hardware alvo: SuperChis Prime num Game Boy Advance SP. Funciona com o build
  padrão `BOARD=chis`, sem nenhuma adaptação.
- Base do fork: `master` em `2a30933` ("Update README", v0.21, 24/09/2026).
- Branch de trabalho atual: `claude/project-thread-3djlx9`.
- Roteiro de testes no hardware: [`docs/fork/TESTES.md`](TESTES.md).

---

## 1. Como compilar

```sh
sudo apt-get install gcc-arm-none-eabi   # toolchain (Ubuntu/Debian)
make BOARD=chis clean
make BOARD=chis                          # gera superfw.gba (renomear para .fw ao gravar)
```

- Validado em 28/09/2026 no container de desenvolvimento, com o gcc-arm-none-eabi
  13.2 do Ubuntu 24.04. O build `chis` compila limpo (superfw.gba com ~1,6 MiB, e o
  limite é 2 MiB).
- `make clean` apaga os cabeçalhos gerados (`src/menu_messages.h`,
  `src/messages_data.h`). Não é para commitá-los.
- O CI (`.github/workflows/build-release.yml`) só roda em push para `master` e em
  tags. Os branches de trabalho não são compilados pelo CI.

**Teste no emulador (sem hardware).** `tools/emu/harness.c` roda o menu no mGBA
(libmgba) simulando o mínimo do SuperCard: o registrador de modo, a SDRAM gravável e
um cartão SD vindo de uma imagem FAT. O firmware precisa ser compilado com
`EMU_HARNESS=1`, que troca o driver do SD por registradores "mágicos" em
`0x09F00000` (`fatfs/diskio.c`). Nunca grave um build `EMU_HARNESS` no cartucho.

```sh
sudo apt-get install libmgba-dev dosfstools mtools
gcc -O1 -o harness tools/emu/harness.c -lmgba
make BOARD=chis EMU_HARNESS=1
mkfs.fat -C -F 32 sd.img 65536 && mmd -i sd.img ::/roms   # e mcopy dos arquivos
./harness superfw.gba sd.img roteiro.txt saida/   # roteiro: wait N / press R+D 3 / shot nome / trace KEYS
```

---

## 2. Estado das funcionalidades

| # | Funcionalidade                       | Estado       | Prioridade |
|---|--------------------------------------|--------------|------------|
| A | Capas (cover-art) no navegador (PR #69 upstream) | Cache validado; capas 76x50 em alta qualidade p/ teste | 1 (principal) |
| B | Navegação: pular por letra + favoritos | Validado no GBA SP (28/09) | 2 |
| C | Modo suspender (sleep) no in-game menu | Validado no GBA SP (28/09, >10 min, SD e NOR) | 3 |
| D | Captura de tela (screenshot) pelo in-game menu | Pausada (decisão do usuário em 28/09) | 4 |

Estados possíveis: Planejado → Em andamento → Pronto p/ teste no hardware → Validado no GBA SP.

---

## 3. Plano por funcionalidade

### A. Capas (cover-art) no navegador (baseado em davidgfnet/superfw#69)

**O que o PR traz** (commit `47946a1`, autor mikermak, 23/06/2026):
- Arquivos novos `src/coverart.c` e `src/coverart.h`. O PR também muda o `Makefile`
  e o `src/menu.c` (render_recent, render_browser e render_flashbrowser).
- Lê imagens no formato do pacote do EZ-Flash Omega: `/IMGS/{c0}/{c1}/{CODE}.bmp`,
  em BMP de 120x80 e 16 bits, identificadas pelo código do jogo (4 caracteres) do
  cabeçalho da ROM.
- Converte cada pixel para um cubo de cores 6x6x6 (216 cores), nos índices 20..235
  da paleta do modo 4. Desenha num painel no canto inferior direito e encurta as
  linhas da lista que passam por cima dele.
- Nos jogos da NOR, usa o código do jogo já guardado, sem ler a ROM.

**Situação:** o PR partiu de `251752e`. O master tem 31 commits a mais desde então,
e o merge conflita em `Makefile` e `src/menu.c`.

**Plano:**
1. Trazer o commit do PR para o branch, dando o crédito ao autor original, e
   resolver os conflitos com o `menu.c` atual. Os pontos de conflito são as funções
   de render dos navegadores e a lista `INFILES` do Makefile.
2. Adicionar a opção "Mostrar capas" (ligado/desligado) na aba UI:
   - enum `UiSet*` em `src/menu.c:100`, que hoje vai até `UiSetSave`;
   - variável e chave em `src/settings.c`, gravadas no `ui-settings.txt` junto de
     `hide_hidden`;
   - textos novos em `res/lang/*.json`, incluindo `pt.json`, validados com
     `tools/lang-checker.py`.
3. Evitar travadas ao rolar a lista rápido: só ler a capa depois que a seleção
   ficar parada por uns ~8 frames. Hoje cada mudança de seleção dispara
   `preload_gba_rom` e a leitura do BMP no SD.
4. Melhorar a qualidade: aplicar dithering ordenado (Bayer 4x4) na conversão para o
   cubo de 216 cores, para reduzir as faixas de cor. É barato, feito uma vez por
   imagem.
5. Opcional: aceitar também o caminho `/.superfw/covers/{CODE}.bmp`, mantendo
   `/IMGS` como padrão.
6. Testar no GBA SP com o pacote de imagens do EZ-Flash Omega, nas três listas
   (recentes, SD e NOR), em jogos com e sem capa.

**Riscos:** o painel ocupa 120x80 da tela e pode apertar os nomes longos. A paleta
também é disputada: o logo da aba Info usa a mesma faixa, e o PR já reaplica a
paleta a cada frame por causa disso.

### B. Navegação: pular por letra + favoritos

Hoje os botões L/R trocam de aba (`src/menu.c:~3270`), Select abre o gerenciador
de arquivos e ←/→ pulam uma página. O Start não é usado nos navegadores.

**B1. Pular por letra**
1. No navegador de ROMs do SD (`keypress_menu_browse`, `src/menu.c:~2880`), o
   Start abre um seletor de letra (0-9, A-Z). Escolhida a letra, a seleção pula
   para o primeiro item cujo `sortname` comece com ela. As pastas vêm primeiro
   (`filesort`, `menu.c:456`), então a busca começa pelos arquivos quando não houver
   pasta com essa letra.
2. Fazer o mesmo no navegador da NOR (`romsort`).
3. Alternativa mais simples, se o seletor ficar pesado: Start pula direto para a
   próxima letra inicial diferente.

**B2. Favoritos**
1. Criar uma aba "Favoritos" (`MENUTAB_FAVORITES`, no enum de `src/menu.c:49`),
   logo depois de "Recentes". Ela aparece só quando houver favoritos, igual à aba
   de recentes.
2. Guardar em `/.superfw/favorites.txt`, no mesmo formato do `recent.txt`.
   Generalizar `src/recent.c` para receber o caminho do arquivo:
   `recent_flush()` hoje usa `RECENT_FILEPATH` fixo (`recent.c:36`), e
   `recent_load()` já recebe o caminho.
3. Adicionar "Adicionar/Remover dos favoritos" no popup do gerenciador de arquivos
   (enum `FiMgr*`, `menu.c:204`), tanto para arquivos do SD quanto para jogos da
   NOR (`FLAG_RECENT_NOR`).
4. Na aba Favoritos, A abre o jogo (reaproveitar a lógica de
   `keypress_menu_recent`) e Select remove o favorito, com confirmação.
5. Memória: um array `t_rentry favorites[N]` no `sdr_state`. Cada entrada tem
   ~260 bytes; começar com N = 32 e conferir o espaço disponível.
6. Mostrar a capa (funcionalidade A) também na aba Favoritos.

### C. Modo suspender (sleep) no in-game menu

**Objetivo:** pausar o jogo e desligar a tela e o som, gastando o mínimo de bateria
no SP. Acordar com L+R+Select e voltar ao jogo.

**Plano:**
1. Fazer primeiro um protótipo para testar o hardware (ver riscos). A ação nova
   `action_sleep` em `src/ingame_menu.c` deve, nesta ordem:
   - salvar `REG_IE`, `REG_IME`, `REG_DISPCNT`, `SOUNDCNT_X` e `REG_KEYCNT`;
   - esperar os botões serem soltos;
   - ligar o forced blank (`DISPCNT |= 0x80`) e desligar o som (`SOUNDCNT_X = 0`);
   - configurar `REG_KEYCNT = 0xC000 | L | R | SELECT` (IRQ só com os três juntos)
     e `IE = KEYPAD`, e limpar `IF`;
   - chamar a instrução de parada do BIOS (Stop, SWI 0x03);
   - ao acordar, restaurar tudo, esperar os botões serem soltos e voltar ao jogo.
2. Colocar o item "Suspender" no menu principal do IGM (`mainacts` e
   `draw_main_menu`, `src/ingame_menu.c:~600` e `:~1072`), com textos novos nos
   `res/lang/*.json` (chaves `IMENU_*`).
3. Opcional: um atalho direto para dormir sem abrir o menu, usando o mecanismo de
   hotkey que já existe (`SettHotkey`).

**Riscos, a validar no protótipo antes de tudo:**
- **SDRAM do cart durante o Stop.** O Stop para os clocks do GBA. Se o CPLD do
  SuperChis depender desse clock para o refresh da SDRAM, a ROM carregada se
  corrompe. Os jogos rodando da NOR não têm esse problema. Plano B: um "sono leve",
  com a tela em forced blank, o som desligado e um laço de Halt (SWI 0x02)
  esperando a combinação de botões. Economiza menos, mas é seguro.
- **Contexto de IRQ.** O IGM roda dentro do tratador de IRQ do jogo, com o bit I do
  CPSR ligado. É preciso confirmar que o Stop acorda pela IRQ do teclado mesmo
  assim. Pelo GBATEK, basta o IE; o IME e o CPSR não importam para acordar.
- **Espaço no IGM.** O binário do IGM usa ~48,6 KB de 60 KB na EWRAM
  (`ldscripts/gba_ingame.ld`). O sleep é pequeno e cabe.

### D. Captura de tela pelo in-game menu

**Problema central:** o GBA não tem um framebuffer que dê para ler de volta. Nos
modos de tiles (0, 1 e 2), que são a maioria dos jogos, a imagem só existe como
tiles, mapas, sprites e registradores, e precisa ser redesenhada em software. Além
disso, o IGM já está com ~48,6 de 60 KB ocupados.

**Plano, em duas fases para caber no IGM:**
1. **No IGM (pouco código):** o item "Capturar tela" grava um dump bruto do estado
   de vídeo em `/.superfw/screenshots/pending/NNNN.gbv`: VRAM 96 KB, paleta 1 KB,
   OAM 1 KB e os registradores de I/O de vídeo, ~99 KB no total. Dá para
   reaproveitar `take_mem_snapshot` e os dados de VRAM/paleta/OAM/IO já "spillados"
   (`src/ingame_menu.c:147`), porque o IGM sobrescreve parte da VRAM ao abrir.
2. **No firmware principal (tem espaço sobrando, até 2 MiB):** no boot, ou ao voltar
   ao menu, converter os dumps pendentes para `/.superfw/screenshots/<jogo>_NNNN.bmp`
   (240x160, 16 bits) e apagar o `.gbv`. Esse é o mesmo padrão do
   `pending-save.txt`.
3. **Renderizador em C portátil** (`src/gbarender.c`), compilado tanto no firmware
   quanto no host, para testar com `tests/` como os outros testes (`tests/Makefile`):
   - fase 1: modos 3, 4 e 5 (bitmap), BGs de texto 4bpp e 8bpp com scroll e flip,
     sprites normais e prioridades;
   - fase 2: BGs afins (modos 1 e 2), sprites afins, janelas e alpha blending.
4. Script opcional `tools/gbv2bmp.py` para converter os dumps no PC.

**Descoberta de 28/09/2026, que bloqueia a qualidade:** os registradores de scroll dos
BGs (`BGxHOFS/VOFS`), os parâmetros afins (`BG2/3 PA–PD`, `X/Y`), as janelas
(`WINxH/V`, `WININ/OUT`) e o `BLDY` são **somente escrita** no GBA. O IGM não
consegue ler esses valores (o README já cita isso como a causa dos deslocamentos
depois do in-game menu). Por isso, nos modos de tiles, que são a maioria dos jogos, a
captura sairia com os fundos desalinhados sempre que o jogo usar scroll, e errada nos
BGs afins. Só os modos bitmap (3/4/5) e as telas sem scroll sairiam corretos.
**Decisão (28/09/2026): pausada pelo usuário.** Se for retomada, a versão barata seria
gravar um dump no IGM (reaproveitando o `writefd_mem_snapshot`) e converter no PC com
`tools/gbv2bmp.py`, sem gastar a EWRAM do firmware, que está em 97%.

**Limitações conhecidas:** efeitos de raster (HDMA ou mudanças de registrador no
meio do frame, como ondas e parallax) não aparecem na captura, porque ela guarda
só um estado.

---

## 4. Ordem de execução sugerida

1. **A (capas):** é o principal pedido. O código já existe e só precisa ser
   portado e ajustado.
2. **B (navegação):** mexe só no menu principal e tem risco baixo.
3. **C (sleep):** o protótipo vem primeiro, porque o risco da SDRAM decide a
   abordagem.
4. **D (screenshot):** é a maior e deve ser feita em fases.

Cada funcionalidade vai num commit (ou PR) próprio, compila com `BOARD=chis` antes
do push e passa pelo teste do usuário no GBA SP antes de ser marcada como
"Validado".

---

## 5. Registro de alterações

Entradas mais novas primeiro. Formato: data, o que mudou, arquivos e estado.

### 2026-09-28 — Capas maiores (76x50) e em alta qualidade
- Resultado do teste da a504000: tudo OK. Sleep de mais de 10 min com jogo da NOR
  e do SD, com savestates, funcionou. Pedidos: capas mais bonitas e 25% maiores.
- **Tamanho:** 76x50 (antes 60x40). O painel foi para (160,90). A largura é par
  porque o framebuffer é escrito 16 bits por vez.
- **Formato de alta qualidade:** a firmware passa a aceitar BMP de **8 bits com
  paleta própria** (até 216 cores, até 76x50) no mesmo caminho `/IMGS/X/Y/CODE.bmp`.
  - A imagem é copiada como está, sem dithering no console, e usa a própria paleta
    nos índices 20..235.
  - O arquivo tem ~4,7 KB contra 19 KB do BMP de 16 bits e não precisa de
    conversão, então carrega mais rápido.
  - O BMP de 16 bits do EZ-Flash Omega continua funcionando, agora reduzido para
    76x50 (média 2x2 em posições proporcionais, lendo as linhas em fluxo) com o
    cubo fixo.
- **Conversor** `tools/covers/convert_covers.py` (Python + Pillow): redimensiona com
  Lanczos, gera uma paleta ótima de 216 cores de 15 bits (median cut) e aplica
  Floyd–Steinberg. Aceita três fontes:
  - a pasta IMGS do EZ-Flash (lê o BMP de 16 bits no formato nativo do GBA);
  - uma pasta de imagens `CODE.png`;
  - download direto do libretro-thumbnails (títulos ou `--boxart`), usando os
    códigos do DAT No-Intro do libretro-database.
- Cache: cada entrada guarda também a paleta. Agora são 40 capas, e o
  `t_sdram_state` ocupa 15.176.044 B (28 KB de folga).
- EWRAM `chis`: 90,7% (233.036 B). Build para teste: `superfw-chis-f142163.fw`; pacote pronto com 2.692 capas: `IMGS-superfw-titulos.zip`.

### 2026-09-28 — Capas instantâneas (cache + pré-carregamento)
- Medido no emulador: converter uma capa gastava ~5M ciclos (~0,3 s). O motivo eram
  3 divisões por pixel, e o Thumb não tem instrução de divisão. Agora a conversão
  usa uma tabela (`q80`) e soma os 4 pixels do bloco 2x2 de uma vez (canais
  espalhados num `uint32_t`). O tempo caiu para ~1,2M ciclos (~70 ms), fora a
  leitura do SD.
- **Cache na SDRAM** (`sdr_state->covercache`, ~132 KB, `src/coverart.c`):
  - 48 capas prontas (LRU), indexadas pelo código do jogo.
  - Um mapa caminho→código do jogo (1024 entradas, hash FNV-1a + tamanho), que
    evita reler o cabeçalho da ROM.
  - Uma lista de códigos sem capa.
  - Voltar a um jogo já visto mostra a capa no mesmo quadro.
- **Pré-carregamento:** depois de 20 quadros parado, com nenhum botão apertado, o
  menu carrega as capas dos vizinhos da seleção, a mais próxima primeiro. Faz uma
  leitura do SD por quadro, no navegador do SD e na aba da NOR. Ao rolar para
  qualquer jogo da página, a capa aparece na hora.
- O atraso para carregar a capa de um jogo fora do cache caiu de 15 para 4 quadros
  depois de soltar os botões.
- **Botões:** o tratador do VBlank agora guarda os botões apertados em cada quadro
  (`latched_keys`, `src/main.c`). Um toque curto durante um carregamento não se
  perde mais (antes, um toque de 2 quadros durante um carregamento sumia).
- A API mudou: `coverart_init(cache)`, `coverart_update_gcode(gcode)` e
  `coverart_prefetch*()`.
- EWRAM: `chis` 90,4% (232.252 B). O `t_sdram_state` ocupa 15.121.868 B, com
  82 KB de folga até o limite de 14,5 MB.

### 2026-09-28 — Correções do 1º teste no hardware (build 303ea6f)
- **Crash na aba de configurações gerais (corrigido).** Reproduzido no emulador
  (`tools/emu/harness.c`, novo). Causa: bug do GCC 13 com `-fipa-ra` no Thumb.
  Com o `-Os` do `menu.c`, `render_icon_trans()` deixou de ser inline. O epílogo
  dela devolve o controle com `pop {r0}; bx r0` e destrói o `r0`, mas o IPA-RA diz
  ao chamador que o `r0` sobrevive. O laço de `render_settings()` guardava o `x` em
  `r0`, nunca terminava e escrevia ícones para além de `fobjs[64]`. Assim ele
  sobrescrevia a IWRAM até o tratador de IRQ. Correção: `-fno-ipa-ra` em
  `BASEFLAGS` (`Makefile`), que vale para todos os binários (+48 bytes).
- **Capas pela metade (60x40).** Cada bloco 2x2 do BMP vira um pixel, pela média
  das cores e com o dithering aplicado sobre a média, que tem mais precisão
  (`src/coverart.c`). O painel foi para (176,100). O buffer na SDRAM caiu para
  2.400 B. O arquivo continua sendo o BMP 120x80 do EZ-Flash Omega.
- **Carregamento da capa:** só começa depois que todos os botões são soltos, com
  15 quadros (~1/4 s) parado no mesmo item (`COVER_LOAD_DELAY`, antes 6). Rolar a
  lista ou pular de letra segurando R não carrega nada no caminho.
- **Pular por letra:** R+↓ vai para a próxima letra e R+↑ volta para a anterior
  (no SD e na NOR). A troca de aba por L/R passou a acontecer **ao soltar** o botão,
  e só se nenhum outro botão foi apertado junto (`get_keypress`). O Start não pula
  mais de letra.
- **Favoritos:** Start marca e desmarca o favorito também no navegador do SD (em
  arquivos) e na aba da NOR. Continua valendo na aba Recentes.
- **Sleep:** funcionou no teste. O LED verde do SP continua aceso porque é o LED de
  energia, ligado direto ao interruptor. O software não o controla. Esse é o mesmo
  comportamento do sleep dos jogos comerciais.
- EWRAM do `chis`: 90,2% (231.812 B). IGM inalterado (48.960 B).
- Build para teste: `superfw-chis-89be7ee.fw`, com o roteiro em `docs/fork/TESTES.md`.

### 2026-09-28 — Otimizações de memória e de desenho dos ícones
- A imagem da capa (`cover_pix`, 9.600 B) saiu da EWRAM e foi para a SDRAM do cart
  (`sdr_state->coverpix`, `src/menu.c`), recebida por `coverart_init()`
  (`src/coverart.c/.h`), que substitui a chamada de `coverart_invalidate()` no
  `menu_init()`. A SDRAM só recebe escritas de 16 bits: cada linha é montada numa
  pilha local e copiada com `dma_memcpy16`, e o letterbox usa `dma_memset16`. Isso
  segue o padrão do resto do menu, que evita escrita de byte na SDRAM. As chaves
  (`cover_key`, `pending_key`) ficaram na EWRAM, mas foram de 512 para `MAX_FN_LEN`
  (256).
- `src/menu.c` passou a ser compilado com `#pragma GCC optimize ("Os")`.
- A lista de ícones (`t_oamobj`) agora tem o layout de uma entrada de OAM e é copiada
  com um único DMA em `menu_flip()`, uma ideia do PR upstream #80. Há uma proteção
  para `objnum == 0`, que o PR não tinha: um DMA com contagem 0 copia 0x4000 unidades.
- Resultado na EWRAM: `chis` 96,9% → **89,9%** (231.172 B), `sd` 84,6% e `lite` 84,7%.
  O IGM não mudou (79,7%).
- Verificação: as três variantes compilam sem warnings novos. O teste de host das
  capas foi refeito com o buffer externo (BMP bottom-up e top-down, letterbox,
  debounce e dithering).

### 2026-09-28 — Análise de memória e dos PRs upstream #79 e #80
- **Memória EWRAM (firmware `chis`, 96,9%):** o linker recusa o build se estourar
  (região `EWRAM` de 251 KB em `ldscripts/gba_ewram.ld`), então não há risco de falha
  silenciosa em tempo de execução. A pilha fica na IWRAM e não existe heap. O limite
  só restringe as próximas funcionalidades.
  Otimizações medidas, ainda não aplicadas:
  - Mover `cover_pix`, `cover_key` e `pending_key` (`src/coverart.c`, 10.624 B de
    `.sbss`) para o `sdr_state` na SDRAM libera ~10,4 KB.
  - Compilar `src/menu.c` com `-Os` (via `#pragma GCC optimize ("Os")`, como já faz o
    `recent.c`) reduz o `.text` em ~7,4 KB. O build com essa mudança ficou em 93,8%.
  - As duas juntas deixam o firmware em ~89%.
- **PR #79** (Sam Casteel, "Adding Key Repeat"): **não adotar**. O master já tem
  repetição de teclas com aceleração (commit `959ca6c`), e o PR conflita em
  `main.c` e `menu.c`.
- **PR #80** (Sam Casteel, "rendering speedup" + "fw naming"): **não adotar como
  está**. Ele commita binários e cabeçalhos gerados (`superfw-chis.fw`,
  `src/menu_messages.h`, `src/messages_data.h`). Também troca os padrões do Makefile
  (`BOARD=chis`, `COMPRESSION_RATIO=10`) e o nome do arquivo gerado, o que quebraria o
  `mv superfw.gba` do `build-release.yml`. A única parte útil é copiar o OAM com um
  DMA em vez de um laço (`menu_flip`). O ganho é pequeno e pode ser portado à parte,
  se valer a pena.

### 2026-09-28 — Sleep no in-game menu (funcionalidade C, protótipo)
- Item novo "Suspender (L+R+Sel)" no fim do menu principal do IGM
  (`action_sleep()`, `src/ingame_menu.c`). As linhas do menu passaram de 19 para
  17 px para caber o 7º item.
- O que ele faz: espera todos os botões serem soltos, liga o forced blank e deixa só
  a IRQ do teclado ligada (`REG_IE = 0x1000`, `KEYCNT = 0xC304`, que dispara quando
  L+R+Select estão pressionados juntos). Então chama o BIOS Stop (`swi 0x03`). Ao
  acordar, restaura IE/KEYCNT/DISPCNT, espera soltar os botões e volta direto ao jogo.
- O som não é mexido: o IGM já zera o `SOUNDCNT_L` ao entrar. Zerar o `SOUNDCNT_X`
  apagaria os registradores de PSG do jogo.
- Texto `IMENU_MAIN6_SLEEP` em en/pt/es.
- IGM: 48.960 de 60 KB de EWRAM.
- **Pontos a validar no GBA SP** (o protótipo existe para isso):
  1. Se a tela e a luz apagam e se L+R+Select acorda o console.
  2. **Se um jogo carregado do SD (na SDRAM) continua funcionando depois de acordar**,
     de preferência após alguns minutos dormindo. Se travar ou corromper, o CPLD não
     faz o refresh da SDRAM sem o clock, e vale partir para o plano B ("sono leve"
     com Halt).
  3. O mesmo teste com um jogo rodando da NOR, que não depende da SDRAM.
  4. Se o save em SRAM continua íntegro.

### 2026-09-28 — Navegação (funcionalidade B)
- **Pular por letra:** no navegador do SD e no da NOR, o **Start** leva à primeira
  entrada com uma inicial diferente da atual, voltando ao topo no fim da lista
  (`next_initial()`, `src/menu.c`). No SD usa o `sortname`, que já vem em minúsculas
  e com acentos transliterados. O seletor de letra A–Z planejado virou esse pulo
  sequencial, que ocupa bem menos código numa EWRAM apertada.
- **Favoritos:**
  - Aba nova `MENUTAB_FAVORITES`, logo depois de Recentes, com ícone de estrela
    (`res/icons.png`, `ICON_FAVORITES` em `res/iconcv.py`, `src/res/icons.h`
    regenerado). A aba só aparece quando há favoritos.
  - Gravados em `/.superfw/favorites.txt` (`FAVORITES_FILEPATH`, `src/config.h`), no
    mesmo formato do `recent.txt`. `recent_flush()` agora recebe o caminho do arquivo
    (`src/recent.c/.h`).
  - Recentes e Favoritos usam o mesmo código de lista (`t_rlist`, `render_rlist()`,
    `keypress_rlist()`). Até 200 entradas, em `sdr_state->favorites` (SDRAM).
  - Como marcar: no navegador do SD, Select → "Adicionar/Remover dos favoritos"
    (`FiMgrFavorite`; os botões desse popup passaram de 30 para 24 px de espaçamento).
    Na aba Recentes, o **Start** marca ou desmarca o jogo, o que serve também para
    jogos da NOR. Na aba Favoritos, o **Select** remove, com confirmação.
  - A barra de abas mostra só as abas visíveis, e L/R pulam as que estão escondidas
    (`tab_visible()`, `tab_step()`).
  - Textos novos `MSG_FMGR_FAVADD/FAVDEL`, `MSG_Q6_DELFAV` e `MSG_OK_FAVADD/FAVDEL`,
    com tradução em pt/es. `tools/lang-checker.py` confirmou que tudo cabe nos popups.
- Verificação: `sd`, `lite` e `chis` compilam sem warnings novos. A EWRAM do `chis`
  foi para 96,8% (248.840 B).
- **Falta**: testar no GBA SP. `res/icons.xcf` não foi atualizado com a estrela; o
  PNG é a fonte usada pelo `iconcv.py`.
- **Para depois**: favoritar direto do navegador da NOR. Hoje o Select de lá só
  apaga, então o caminho é pela aba Recentes.

### 2026-09-28 — Capas (funcionalidade A)
- **Commit `7a210ce`**: o PR davidgfnet/superfw#69 foi portado com cherry-pick, mantendo
  o autor original (mikermak). Os conflitos em `Makefile` (entrada `src/recent.c` nova
  no master) e em `render_browser` (o master não mostra tamanho para pastas) foram
  resolvidos mantendo os dois comportamentos.
- **Commit seguinte** (melhorias sobre o PR):
  - Opção "Mostrar capas" na aba UI (`UiSetCover`, `src/menu.c`), gravada como
    `show_covers=` em `ui-settings.txt` (`src/settings.c`). O padrão é ligada. Texto
    `MSG_UIS_COVER` em `res/messages.py`, `pt.json` e `es.json`; os outros idiomas
    caem no inglês. As linhas da aba UI passaram de 20 para 18 px para caber a nova
    opção acima do botão Salvar.
  - A capa só é lida do SD depois que a seleção fica parada por `COVER_LOAD_DELAY` = 6
    frames do menu (`needs_load()`, `src/coverart.c`). Ao voltar para a entrada que já
    estava carregada, a capa reaparece sem nova leitura.
  - Dithering ordenado Bayer 4x4 na conversão para o cubo de 216 cores
    (`dither6()`, `src/coverart.c`).
  - Na aba Recentes, os jogos da NOR agora mostram a capa, buscando o código do jogo
    em `nordata`. No PR original eles ficavam sem capa, porque o caminho não existe no SD.
  - `coverart_invalidate()` passou a ser chamado em `menu_init()`, porque os buffers
    ficam em `.sbss` (EWRAM não zerada no boot).
- Verificação: os builds `sd`, `lite` e `chis` compilam sem warnings novos. Um teste
  no host (stubs de FatFS, fora do repositório) validou a leitura de BMP bottom-up
  e top-down, o letterbox, o debounce, a volta sem releitura e o dithering (média do
  canal 2,38 contra o ideal de 2,42).
- **Falta**: testar no GBA SP com o pacote `/IMGS` do EZ-Flash Omega.
- Atenção: a EWRAM do firmware `chis` está em ~96% (246.864 de 251 KB). As próximas
  funcionalidades devem guardar dados grandes no `sdr_state` (SDRAM) e não em `.sbss`.

### 2026-09-28 — Planejamento
- Criado este documento com o plano das funcionalidades A–D.
- Verificado que o build `BOARD=chis` compila no container (gcc-arm-none-eabi 13.2).
- Analisado o PR upstream davidgfnet/superfw#69 (cover-art). Ele conflita com o
  master atual em `Makefile` e `src/menu.c`.
- Nenhuma mudança de código ainda.

---

## 6. Notas técnicas e descobertas

- Registrador de modo do CPLD: `0x09FFFFFE` com magic `0xA55A` (`src/supercard_driver.c`).
  No SuperChis, o banco de SRAM usa o bit 3 (`sram_superchis_bank`) e o mapa de
  blocos da NOR usa `0x100 | bloco` (`set_superchis_normap`).
- A NOR é tratada como 128 MiB, em blocos de 4 MiB (`src/flash_mgr.h`).
- Espaço do IGM: ~49 KB de 60 KB em EWRAM e 4 KB de IWRAM. Todo código novo no IGM
  precisa ser enxuto.
- Espaço do firmware principal (`chis`): EWRAM ~90% usada após as otimizações de 28/09. Vale
  medir a cada funcionalidade (`--print-memory-usage` no log do make).
- Capas de alta qualidade: `python3 tools/covers/convert_covers.py --imgs <pasta IMGS> --out saida`
  ou `--libretro "Nintendo - Game Boy Advance.dat"` (baixa os títulos do
  libretro-thumbnails). Copie `saida/IMGS` para a raiz do SD.
- Capas: o pacote oficial do EZ-Flash Omega (`https://www.ezflash.cn/zip/IMGS.zip`,
  citado no README de mikermak/retroid-super-flash) usa BMP de 16 bits no formato
  nativo do GBA. Capas feitas pela comunidade (EZ Omega Thumbmaker, guias) costumam
  ser de 24 bits, que o firmware **ainda não lê**. É uma melhoria possível.
- Botões nos navegadores: L/R trocam de aba ao soltar, Select abre o gerenciador de
  arquivos (na NOR, apaga o jogo), ←/→ pulam uma página, R+↓/R+↑ pulam para a
  próxima/anterior letra inicial (SD e NOR) e Start marca/desmarca o favorito (SD,
  NOR e Recentes).
- **Cuidado com `-Os`/funções não-inline no Thumb:** o GCC 13 com `-fipa-ra` gera
  código errado quando uma função `void` volta com `pop {r0}; bx r0` (ver entrada de
  28/09, "Correções do 1º teste"). O `Makefile` agora usa `-fno-ipa-ra`, e ele não
  deve ser removido.
