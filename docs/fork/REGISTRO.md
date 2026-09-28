# Registro do fork SuperFW (SuperChis Prime / GBA SP)

Este documento é a memória permanente do fork `fefsouza10/superfw`. Ele diz o que
foi planejado, o que já foi adicionado ou alterado em relação ao SuperFW original
(davidgfnet/superfw) e o que ainda falta. **Precisa ser atualizado a cada sessão de
trabalho, antes de ela terminar.**

- Hardware alvo: SuperChis Prime num Game Boy Advance SP. Funciona com o build
  padrão `BOARD=chis`, sem nenhuma adaptação.
- Base do fork: `master` em `2a30933` ("Update README", v0.21, 24/09/2026).
- Branch de trabalho atual: `claude/project-thread-3djlx9`.

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

---

## 2. Estado das funcionalidades

| # | Funcionalidade                       | Estado       | Prioridade |
|---|--------------------------------------|--------------|------------|
| A | Capas (cover-art) no navegador (PR #69 upstream) | Pronto p/ teste no hardware | 1 (principal) |
| B | Navegação: pular por letra + favoritos | Planejado  | 2 |
| C | Modo suspender (sleep) no in-game menu | Planejado  | 3 |
| D | Captura de tela (screenshot) pelo in-game menu | Planejado | 4 |

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
- Espaço do IGM: ~48,6 KB de 60 KB em EWRAM e 4 KB de IWRAM. Todo código novo no IGM
  precisa ser enxuto.
- Espaço do firmware principal (`chis`): EWRAM ~96% usada após as capas. Vale
  medir a cada funcionalidade (`--print-memory-usage` no log do make).
- Nos navegadores, L/R trocam de aba, Select abre o gerenciador de arquivos,
  ←/→ pulam uma página e Start está livre.
