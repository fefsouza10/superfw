# SuperFW v0.21, custom version by fefsouza10 (1)

A fork of [SuperFW](https://github.com/davidgfnet/superfw) v0.21 by davidgf, made for the
SuperChis Prime on a Game Boy Advance SP. *Versão em português abaixo.*

## Which file do I need?

| File | Carts | Notes |
|------|-------|-------|
| `superfw-chis.fw` | SuperChis, SuperChis Prime | Everything, including the video player |
| `superfw-sd.fw` | Supercard SD and clones | Everything except the built-in video player (no room left in its 512KB flash). Copy `gbvplayer.gba` to `/.superfw/emulators/` to play videos |
| `superfw-lite.fw` | Supercard Lite | Everything except the built-in emulators and the video player (496KB flash), as in the original firmware |
| `COVERS-superfw.zip` | all | Title screens of ~2700 GBA games, 8-bit BMPs with their own palette (the best looking). Unzip to the root of the SD card |
| `IMGS-superfw.zip` | all | The same title screens in the EZ-Flash Omega format (16-bit BMP, 120x80). Optional: covers games missing from `/COVERS` |
| `video-converter.zip` | PC | Video-to-GBA Converter by fefsouza10 (Python), turns any video into a `.gbv` file |
| `gbvplayer.gba` | sd | The video player, for the `sd` firmware only |

Try the firmware first by renaming it to `.gba` and opening it from your current
SuperFW. To flash it, open the `.fw` from the browser (unlock updates with
Down+B+Start in the Info tab). You can go back to an official SuperFW release the
same way.

## What's new

### Cover art
- The selected game's cover shows in the SD, NOR, Recent and Favorites tabs (based
  on davidgfnet/superfw#69 by mikermak).
- Two folders are searched: `/COVERS/X/Y/CODE.bmp` (8-bit BMP with its own palette,
  up to 216 colors, sharpest) and `/IMGS/X/Y/CODE.bmp` (EZ-Flash Omega pack, 16-bit).
- Size option in the UI tab: Disabled, Small (60x40), Medium (90x60), Large (120x80).
- 40 covers are cached in the cart SDRAM and the neighbours are preloaded, so
  scrolling is instant.
- New **carousel view** (UI tab, "Game list view"): the selected cover in the
  middle, previous and next games as thumbnails on the sides, name and size below.
  Left/Right change game, Up/Down change page.
- `tools/covers/convert_covers.py` builds the packs from EZ-Flash images, PNGs or
  the libretro-thumbnails title screens (`--ezflash` writes the IMGS format).

### Navigation and favorites
- R+Down / R+Up jump to the next / previous initial letter (SD and NOR).
- L/R switch tabs on release, so they also work as modifiers.
- Start adds or removes a favorite (SD, NOR, Recent); a Favorites tab appears
  when there are favorites (`/.superfw/favorites.txt`). Select removes an entry.
- R+Start opens a search-by-name keyboard in the SD tab.
- Quick button taps are never lost, even while a cover loads.

### Sleep and battery
- In-game menu "Sleep" entry: screen off, sound muted, CPU stopped (BIOS Stop).
  The wake combo resumes the game. Works for SD and NOR games, with savestates.
- "Wake from sleep" setting picks the combo (default L+R+Select).
- "Menu auto sleep" puts the GBA to sleep after 2/5/10/15 idle minutes in the menu.
- The menu halts the CPU between frames instead of busy-waiting.

### IPS, UPS and BPS soft-patching
- `Game.gba` + `Game.ips` (or `.ups`/`.bps`) next to it: the patch is applied while
  loading, the ROM file is not touched. Select on the ROM info page turns it on or
  off. UPS/BPS checksums are verified. Patched ROMs can grow up to 32MB.

### Video player
- `.gbv` files play full screen (240x160) at up to 30 fps, with sound. About 20
  to 25 minutes fit in one file (31MB, loaded into the cart SDRAM).
- A or Start pause; Left/Right seek 10 s; L/R seek 60 s; Up/Down volume; Select
  pins the time bar; B pauses and B again exits.
- The GBV2 format reuses the parts of the picture that did not change or only
  moved, with a per-frame quality search. Old GBV1 files still play.
- Video-to-GBA Converter by fefsouza10: any video ffmpeg reads, live progress
  with an animated GBA in the terminal.

### Fixes
- In-game menu hot-key setting was never read back after a reboot.
- Settings tab could hang (GCC 13 `-fipa-ra` bug in Thumb code): built with
  `-fno-ipa-ra`.
- In-game menu crash caused by a libgcc table shifting the menu code; the linker
  script now checks the layout.
- A single button press was sometimes taken as two in the menu.
- Videos converted on some PCs had no sound.

### Other
- Info tab: "custom version by fefsouza10".
- README in English and Portuguese, with tutorials and a button reference.
- `tools/emu/harness.c`: runs the menu in libmgba with a fake SD card for testing
  without hardware (screenshots, key scripts, audio capture).

---

# SuperFW v0.21, versão personalizada por fefsouza10 (1)

Um fork do [SuperFW](https://github.com/davidgfnet/superfw) v0.21, do davidgf, feito para
o SuperChis Prime num Game Boy Advance SP.

## Qual arquivo baixar?

| Arquivo | Cartuchos | Observações |
|---------|-----------|-------------|
| `superfw-chis.fw` | SuperChis, SuperChis Prime | Tudo, inclusive o player de vídeo |
| `superfw-sd.fw` | Supercard SD e clones | Tudo, menos o player de vídeo embutido (não cabe mais nos 512 KB da flash). Copie o `gbvplayer.gba` para `/.superfw/emulators/` para ver vídeos |
| `superfw-lite.fw` | Supercard Lite | Tudo, menos os emuladores embutidos e o player de vídeo (flash de 496 KB), como na firmware original |
| `COVERS-superfw.zip` | todos | Telas de título de uns 2700 jogos de GBA, BMP de 8 bits com paleta própria (as mais bonitas). Descompacte na raiz do SD |
| `IMGS-superfw.zip` | todos | As mesmas telas no formato do EZ-Flash Omega (BMP de 16 bits, 120x80). Opcional: cobre jogos que faltarem no `/COVERS` |
| `video-converter.zip` | PC | Video-to-GBA Converter by fefsouza10 (Python), transforma qualquer vídeo num `.gbv` |
| `gbvplayer.gba` | sd | O player de vídeo, só para a firmware `sd` |

Teste a firmware primeiro renomeando para `.gba` e abrindo pela SuperFW que você
já tem. Para gravar, abra o `.fw` no navegador (libere a atualização com
↓+B+Start na aba Info). Dá para voltar a uma versão oficial da SuperFW do mesmo
jeito.

## Novidades

### Capas
- A capa do jogo selecionado aparece nas abas do SD, da NOR, de Recentes e de
  Favoritos (baseado em davidgfnet/superfw#69, de mikermak).
- Duas pastas: `/COVERS/X/Y/CODIGO.bmp` (BMP de 8 bits com paleta própria, até
  216 cores, a mais nítida) e `/IMGS/X/Y/CODIGO.bmp` (pacote do EZ-Flash Omega,
  16 bits).
- Tamanho na aba UI: Desativado, Pequena (60x40), Média (90x60), Grande (120x80).
- 40 capas ficam em cache na SDRAM do cartucho e as vizinhas são pré-carregadas:
  a rolagem é instantânea.
- Novo **modo carrossel** (aba UI, "Exibição"): a capa selecionada no meio, as do
  jogo anterior e do seguinte como miniaturas nas laterais, nome e tamanho
  embaixo. ←/→ trocam de jogo, ↑/↓ trocam de página.
- `tools/covers/convert_covers.py` monta os pacotes a partir de imagens do
  EZ-Flash, de PNGs ou das telas de título do libretro-thumbnails (`--ezflash`
  gera o formato IMGS).

### Navegação e favoritos
- R+↓ / R+↑ pulam para a próxima / anterior letra inicial (SD e NOR).
- L/R trocam de aba ao soltar, então também servem de modificador.
- Start adiciona ou remove dos favoritos (SD, NOR, Recentes); a aba Favoritos
  aparece quando há favoritos (`/.superfw/favorites.txt`). Select remove um item.
- R+Start abre o teclado de busca por nome na aba do SD.
- Toques rápidos nos botões não se perdem, mesmo com uma capa carregando.

### Suspender e bateria
- Item "Suspender" no in-game menu: apaga a tela, silencia o som e para a CPU
  (Stop do BIOS). A combinação de acordar volta ao jogo. Funciona com jogos do SD
  e da NOR, com savestates.
- A opção "Acordar com" escolhe a combinação (padrão L+R+Select).
- "Suspender no menu" suspende o GBA depois de 2/5/10/15 minutos parado no menu.
- O menu deixa a CPU parada entre os quadros, em vez de ficar esperando ativamente.

### Patches IPS, UPS e BPS na hora de carregar
- `Jogo.gba` + `Jogo.ips` (ou `.ups`/`.bps`) ao lado: o patch é aplicado ao
  carregar, sem mexer no arquivo da ROM. Select na tela de informações liga ou
  desliga. Os checksums de UPS/BPS são conferidos. A ROM com patch pode crescer
  até 32 MB.

### Player de vídeo
- Arquivos `.gbv` tocam em tela cheia (240x160) a até 30 quadros por segundo, com
  som. Cabem uns 20 a 25 minutos num arquivo (31 MB, carregado na SDRAM).
- A ou Start pausa; ←/→ ±10 s; L/R ±60 s; ↑/↓ volume; Select fixa a barra de
  tempo; B pausa e B de novo sai.
- O formato GBV2 reaproveita o que não mudou ou só se moveu na imagem, com busca
  de qualidade quadro a quadro. Arquivos GBV1 antigos continuam tocando.
- Video-to-GBA Converter by fefsouza10: aceita qualquer vídeo que o ffmpeg lê, com
  progresso ao vivo e um GBA animado no terminal.

### Correções
- A tecla do in-game menu escolhida nas configurações não era lida depois de
  reiniciar.
- A aba de configurações podia travar (bug do GCC 13 com `-fipa-ra` no Thumb):
  agora compila com `-fno-ipa-ra`.
- O in-game menu travava por causa de uma tabela da libgcc que deslocava o código
  do menu; o script do linker agora confere o layout.
- Um único toque num botão às vezes contava como dois no menu.
- Vídeos convertidos em alguns PCs ficavam sem som.

### Outros
- Aba Info: "custom version by fefsouza10".
- README em inglês e português, com tutoriais e referência de botões.
- `tools/emu/harness.c`: roda o menu no libmgba com um SD simulado, para testar
  sem hardware (capturas de tela, roteiros de botões, captura de áudio).
