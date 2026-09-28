
SuperFW
=======

An alternative firmware for Supercard GBA flash carts (and derivatives/clones)

> **Este é um fork** do [SuperFW](https://github.com/davidgfnet/superfw) com
> novidades pensadas para o SuperChis Prime num Game Boy Advance SP (build
> `BOARD=chis`, que também serve para os outros cartuchos). Veja abaixo em
> [português](#novidades-deste-fork-português) e em
> [English](#whats-new-in-this-fork-english). O texto original do projeto vem
> depois delas.

Novidades deste fork (português)
--------------------------------

**Capas dos jogos no navegador**
- Mostra a capa do jogo selecionado nas abas do SD, da NOR, de Recentes e de
  Favoritos. A capa é achada pelo código do jogo (4 letras do cabeçalho da ROM).
- Aceita duas pastas na raiz do SD, nesta ordem:
  - `/COVERS/X/Y/CODIGO.bmp`: BMP de 8 bits com paleta própria (até 216 cores,
    até 120x80). É o formato mais bonito e o mais rápido de carregar.
  - `/IMGS/X/Y/CODIGO.bmp`: o pacote do EZ-Flash Omega (BMP de 16 bits, 120x80).
    Pode ficar junto do `/COVERS`, cobrindo o que faltar nele (por exemplo, ROM
    hacks).
  - `X` e `Y` são a 1ª e a 2ª letras do código (Pokémon Emerald, BPEE, fica em
    `/COVERS/B/P/BPEE.bmp`).
- Tamanho escolhido na aba UI, na opção "Capas": Desativado, Pequena (60x40),
  Média (90x60) ou Grande (120x80, o padrão).
- As capas ficam num cache na SDRAM (40 capas) e as dos jogos vizinhos são
  pré-carregadas quando a seleção fica parada, então aparecem na hora ao rolar a
  lista.
- O script `tools/covers/convert_covers.py` (Python + Pillow) monta a pasta
  `COVERS`. Ele converte o pacote do EZ-Flash, uma pasta de PNGs ou baixa as
  imagens do libretro-thumbnails:
  `python3 tools/covers/convert_covers.py --libretro "Nintendo - Game Boy Advance.dat" --out SD/`

**Navegação mais rápida e favoritos**
- R+↓ pula para a próxima letra inicial, e R+↑ volta para a anterior (no SD e na
  NOR).
- L e R trocam de aba ao **soltar** o botão, e só quando não foram usados junto com
  outro botão.
- Start marca ou desmarca um favorito nas abas do SD, da NOR e de Recentes. Os
  favoritos ganham uma aba própria e ficam em `/.superfw/favorites.txt`.
- Toques rápidos nos botões não se perdem mais, nem durante o carregamento de uma
  capa.

**Suspender (sleep) e economia de bateria**
- O item "Suspender" do in-game menu apaga a tela, desliga o som e põe o GBA no
  modo de parada do BIOS. A combinação de botões mostrada ao lado do item acorda e
  volta ao jogo. Funciona com jogos carregados do SD e da NOR (testado por mais de
  10 minutos, com savestates). O LED do SP continua aceso, porque ele é ligado
  direto ao interruptor.
- Na aba de configurações gerais:
  - "Acordar com" escolhe a combinação que acorda o GBA (padrão L+R+Select).
  - "Suspender no menu" suspende o GBA sozinho depois de 2, 5, 10 ou 15 minutos
    parado no menu da SuperFW (padrão: Nunca). Acorda com a mesma combinação.
- O menu agora deixa a CPU parada enquanto espera o próximo quadro da tela, em vez
  de ficar consultando o vídeo sem parar. Isso gasta menos pilha sem mudar nada no
  uso.

**Patches IPS, UPS e BPS na hora de carregar (soft-patching)**
- Para jogar uma tradução ou um ROM hack sem alterar a ROM, basta colocar o patch
  ao lado da ROM, com o mesmo nome: `Jogo.gba` + `Jogo.ips` (ou `.ups`, ou
  `.bps`).
- A tela de informações do jogo mostra "Patch IPS: ligado [SELECT]". O Select liga
  e desliga o patch antes de carregar. Se o patch UPS ou BPS foi feito para outra
  versão da ROM, a tela avisa e ele não é aplicado.
- A ROM pode crescer com o patch, até 32 MB.
- Só vale para jogos carregados do SD (não para a gravação na NOR). A detecção de
  save e os patches da base de dados usam o cabeçalho da ROM original.
- `tools/patch-demo/mkdemo.py` cria uma ROM de teste com um patch de cada tipo.

**Busca por nome**
- R+Start abre um teclado na tela, na aba do SD. A digita a letra escolhida, e a
  tela mostra o primeiro arquivo ou pasta da pasta atual cujo nome contém o texto.
  L/R passam para o resultado anterior ou o próximo, Start vai até ele e B fecha.

**Player de vídeo (.gbv)**
- Arquivos `.gbv` abrem num player próprio: tela cheia 240x160, som, e até
  30 quadros por segundo. Cabem cerca de 20 a 25 minutos num arquivo (o limite é
  31 MB, porque o vídeo é carregado na SDRAM do cartucho).
- Botões: A ou Start pausa; ←/→ voltam ou avançam 10 s; L/R, 60 s; ↑/↓ mudam o
  volume; Select fixa a barra de tempo na tela. B pausa, e B de novo sai para a
  SuperFW.
- O conversor `tools/video/gbvconv.py` roda no PC (Windows, Linux ou macOS) e
  aceita qualquer vídeo que o ffmpeg lê (mkv, mp4, avi...). Ele mantém todos os
  quadros do vídeo original e reduz a qualidade da imagem até o arquivo caber no
  limite:
  ```
  pip install numpy pillow imageio-ffmpeg
  python3 tools/video/gbvconv.py episodio.mkv      # gera episodio.gbv
  ```
  Um episódio de 20 minutos leva uns 5 minutos para converter.

**Correções**
- A tecla do in-game menu escolhida nas configurações voltava para L+R+Start
  depois de reiniciar (a opção era gravada, mas não era lida). Agora ela é
  mantida.
- A aba de configurações gerais podia travar o menu por causa de um bug do
  GCC 13 (`-fipa-ra` no Thumb). O projeto agora compila com `-fno-ipa-ra`.

O histórico completo das mudanças deste fork está em
[`docs/fork/REGISTRO.md`](docs/fork/REGISTRO.md).

What's new in this fork (English)
---------------------------------

**Cover art in the browser**
- Shows the cover of the selected game in the SD, NOR, Recent and Favorites tabs.
  Covers are matched by the game code (the 4-letter code in the ROM header).
- Two folders at the root of the SD card are searched, in this order:
  - `/COVERS/X/Y/CODE.bmp`: 8-bit BMP with its own palette (up to 216 colors and
    120x80). It looks best and loads fastest.
  - `/IMGS/X/Y/CODE.bmp`: the EZ-Flash Omega pack (16-bit BMP, 120x80). It can
    stay next to `/COVERS` to cover whatever is missing there, such as ROM hacks.
  - `X` and `Y` are the 1st and 2nd letters of the code (Pokémon Emerald, BPEE,
    goes in `/COVERS/B/P/BPEE.bmp`).
- The size is picked in the UI tab, "Cover art" option: Disabled, Small (60x40),
  Medium (90x60) or Large (120x80, the default).
- Covers are cached in SDRAM (40 covers), and the neighbours of the selection are
  preloaded while it stays still, so they show up instantly when scrolling.
- `tools/covers/convert_covers.py` (Python + Pillow) builds the `COVERS` folder
  from the EZ-Flash pack, from a folder of PNGs, or by downloading the
  libretro-thumbnails images:
  `python3 tools/covers/convert_covers.py --libretro "Nintendo - Game Boy Advance.dat" --out SD/`

**Faster navigation and favorites**
- R+Down jumps to the next initial letter, and R+Up goes back to the previous one
  (SD and NOR).
- L and R switch tabs on **release**, and only when they were not used together
  with another button.
- Start adds or removes a favorite in the SD, NOR and Recent tabs. Favorites get
  their own tab and are stored in `/.superfw/favorites.txt`.
- Quick button taps are no longer lost, even while a cover is loading.

**Sleep and battery saving**
- The "Sleep" in-game menu entry blanks the screen, mutes the sound and puts the
  GBA in BIOS Stop mode. The button combo shown next to the entry wakes it up and
  resumes the game. It works with games loaded from SD and from NOR (tested for
  more than 10 minutes, with savestates). The SP LED stays on, because it is wired
  to the power switch.
- In the general settings tab:
  - "Wake from sleep" picks the combo that wakes the GBA (default L+R+Select).
  - "Menu auto sleep" puts the GBA to sleep after 2, 5, 10 or 15 idle minutes in
    the SuperFW menu (default: Never). The same combo wakes it up.
- The menu now halts the CPU while it waits for the next video frame, instead of
  busy-polling the display. This saves battery without changing anything else.

**IPS, UPS and BPS soft-patching**
- To play a translation or a ROM hack without modifying the ROM, put the patch
  next to the ROM with the same name: `Game.gba` + `Game.ips` (or `.ups`, or
  `.bps`).
- The game info page shows "IPS patch: on [SELECT]". Select turns the patch on and
  off before loading. If a UPS or BPS patch was made for a different ROM, the page
  says so and the patch is not applied.
- Patched ROMs can grow, up to 32MB.
- Only for games loaded from the SD card (not for NOR flashing). Save type
  detection and database patches use the original ROM header.
- `tools/patch-demo/mkdemo.py` builds a test ROM with one patch of each kind.

**Search by name**
- R+Start opens an on-screen keyboard in the SD tab. A types the selected letter,
  and the first file or folder in the current folder whose name contains the text
  is shown. L/R go to the previous or next match, Start jumps to it and B closes
  the keyboard.

**Video player (.gbv)**
- `.gbv` files open in a built-in player: full screen 240x160, sound, and up to
  30 frames per second. About 20 to 25 minutes fit in one file (the limit is
  31MB, because the video is loaded into the cart SDRAM).
- Buttons: A or Start pauses; Left/Right seek 10s; L/R, 60s; Up/Down change the
  volume; Select pins the time bar. B pauses, and B again exits to SuperFW.
- The converter `tools/video/gbvconv.py` runs on a PC (Windows, Linux or macOS)
  and takes any video ffmpeg can read (mkv, mp4, avi...). It keeps every frame of
  the source and lowers the picture quality until the file fits:
  ```
  pip install numpy pillow imageio-ffmpeg
  python3 tools/video/gbvconv.py episode.mkv      # writes episode.gbv
  ```
  A 20-minute episode takes about 5 minutes to convert.

**Fixes**
- The in-game menu hot-key picked in the settings went back to L+R+Start after a
  reboot (it was saved but never read back). It is kept now.
- The general settings tab could hang the menu because of a GCC 13 bug
  (`-fipa-ra` in Thumb code). The project now builds with `-fno-ipa-ra`.

The full change log of this fork (in Portuguese) is in
[`docs/fork/REGISTRO.md`](docs/fork/REGISTRO.md).

Original README
---------------

This project aims to provide a more modern and better firmware for Supercard
flash carts (which are still widely used and very cheaply available). The goal
is to add many features only present in more expensive or sophisticated flash
carts. Unfortunately we are limited to the actual hardware so certain features
are impossible or very complex to implement.

Find the website and documentation at https://superfw.davidgf.net/


Installation
------------

Check https://superfw.davidgf.net/docs/install/flash/ for more details.

The firmware can be chain-loaded using another firmware (ie. the default
SuperCard firmware or SCFW) and loaded as a regular game. It can also be
installed on the internal flash device. Installing it enables some nice
features such as SDHC and exFAT compatibility.

To install the firmware you can simply load it first, and then use SuperFW
to flash itself on the flash. You will need to enable flashing in the Info
tab and then pick the .fw file and flash it. It is strongly recommended to
reboot your GBA after flashing.

Flashing is also possible using an NDS. This is particularly useful if you
_brick_ your Supercard (ie. interrupting flashing, low battery conditions
and similar situations could cause a bad flash). You will need an NDS device
and a Slot-1 cart as well. Download the .nds ROM for your Slot-1 cart at
https://github.com/davidgfnet/superfw-nds-flasher-tool/releases/ and launch
it with your Supercard on your Slot-2. You should be able to flash (as well
as backup) your flash.

GB/GBC Emulation
----------------

GameBoy and GameBoy Color ROMs can be played by using the built-in Goombacolor
emulator binary (the Lite build doesn't ship any emulator though). Picking any
.gb/.gbc file will load the emulator and the ROM and start its execution.

Other devices can also be played as long as the right emulator is installed in
the SD card (and supported by SuperFW).

Check https://superfw.davidgf.net/docs/usermanual/emulators/ for details.

ROM patching
------------

The firmware contains a patch database to patch several features. A custom
database can also be loaded from the SD card and used instead (so more games
and improvements can be added). The patches contain information about:

 - WaitCNT patches: Also called white/black screen patches, prevent games from
   updating the WAITCNT waitstates (the supercard has a slow memory). Without
   a correct patch the game won't even boot.
 - Flash/EEPROM offsets: Indicate where the relevant storage routines are so
   that they can be patched and converted to SRAM storage.
 - IRQ handler patches: Used to patch user IRQ handler routine and install a
   custom one. Used to enable in-game menu.
 - RTC patches: Used for games that contained an RTC IC in theri cart, to keep
   track of time (both time and date). There's only a handful such ROMs.

More information at https://superfw.davidgf.net/docs/usermanual/patches/

These patches are generated mostly automatically, check out the patch repo at:
https://github.com/davidgfnet/gba-patch-gen
It is also possible to use the web-based patch generator for better patches:
https://patchtool.superfw.davidgf.net/

In-game menu
------------

SuperFW features an in-game menu that allows users to pause the current game
and perform certain actions such as:

  - Resuming and resetting the game
  - Going back to the SuperFW menu (witout having to reboot your GBA)
  - Handling saves (for games that allow saving)
  - Creating and restoring savestates
  - Applying/using cheat codes
  - Changing the RTC time (for games that use an RTC)

This menu is a bit of a hack that requires patching the ROM to work. For this
reason, some games won't work well with it or will suffer from bugs (usually
graphical bugs). In this case it is advised to not use the in-game menu.

Many graphical glitches will result in the screen being "offseted" to the
left/right/up/down. In many cases this is not an issue (besides making it
harder for the user to see and play) and it goes away when entering a new
zone/level/menu. This is due to the GBA featuring some "write-only" registers,
that is, registers that can be written but never read back. For this reason
we cannot properly save and restore said registers.

Saving games
------------

Save games are stored in the cart's SRAM and preserved by the cart battery
(note that if the battery is dead the game will be lost). On reboot SuperFW
will write the savegame to the SD card to preserve it and allow loading
another save game.

When using the in-game menu, you might enter the menu and select any of the
saving options, which will write the save to the SD card. This is a good
way to save your games if you prefer to manually handle save files (ie.
disabling autosave and manually choosing when to save).

For Flash-based games (around 300 games) and EEPROM-based games (around 1400
games) it is possible to patch games so that they write directly to the SD
save game, this is called Direct-Saving mode. This makes saving more reliable
(no need for a battery!) and simpler to use (no need to reboot to ensure
saving or using the in-game menu). Games that use Flash or EEPROM will display
an option for direct-saving (this is the default choice in Auto mode).

Files and configuration on the SD card
--------------------------------------

All SuperFW related files are stored under "/.superfw" at the root of the card.
The following files are usually created:

 - .superfw/settings.txt: User settings, loaded on startup.
 - .superfw/ui-settings.txt: UI settings, loaded on startup.
 - .superfw/recent.txt: Recently played ROMs, in order.
 - .superfw/pending-save.txt: SRAM save information (temp file).
 - .superfw/pending-sram-test.txt: SRAM test flag (temp file).

Other noteworthy paths:

 - .superfw/config/: Per-ROM load configuration.
 - .superfw/patches/: Patch cache (created by PatchEngine).
 - .superfw/cheats/: Cheat database, contains .cht files.
 - .superfw/emulators/: Emulator ROMs, used to play other device's ROMs.

Limits
------

The following restrictions apply to the firmware due to memory/storage/cpu
constraints:

 - Maximum ROM size: 32MiB (Supercard's memory size)
 - File path and name limit: 255 utf-8 bytes (not exactly characters!)
 - Maximum number of files+dirs in a directory: 15360 (16384 upstream; this
   fork uses the difference for the cover cache)

Licenses
--------

Most of SuperFW was written by davidgf and is published under GPL license.
Some components use third party code, such as: nanoprintf (public domain),
heapsort (3-BSD) and fatfs (1-BSD-like). apultra and upkr (only used at
build-time) were re-implemented in C++ using the original sources as reference
and an LLM (they are under zlib and public domain respecitvely). Some
linkerscript/crt0 code was adapted from AntonioND's work under CC0.


