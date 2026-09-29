Video-to-GBA Converter by fefsouza10
====================================

Turns any video into a `.gbv` file for the SuperFW video player (240x160, up to
30 frames per second, with sound). *Português abaixo.*

1. Install Python 3 (python.org; on Windows, tick "Add Python to PATH").
2. Install the libraries once:
   `pip install numpy pillow imageio-ffmpeg`
3. Convert a video (mkv, mp4, avi, anything ffmpeg reads):
   `python gbvconv.py episode.mkv`
   This writes `episode.gbv` next to the source, 31MB at most. Conversion takes
   about as long as the video itself, with live progress in the terminal.
4. Copy the `.gbv` to any folder on the SD card and open it from SuperFW.

Every frame is kept. What adapts to the 31MB limit is the picture quality, so a
20-minute episode fits, and shorter videos look better.

Options: `-o out.gbv` (output name), `--max-mb 31.5` (size limit),
`--spv 264` (better sound, less room for the picture), `--no-audio`,
`--res 120x80` (half resolution, scaled up: cleaner but blurrier).

Player buttons: A or Start pause; Left/Right seek 10 s; L/R seek 60 s; Up/Down
volume; Select pins the time bar; B pauses, and B again exits.

The player is built into the `chis` firmware. For the `sd` firmware, copy
`gbvplayer.gba` (attached to every release) to `/.superfw/emulators/`.

---

Conversor de vídeo (português)
------------------------------

Transforma qualquer vídeo num arquivo `.gbv` para o player da SuperFW.

1. Instale o Python 3 (python.org; no Windows, marque "Add Python to PATH").
2. Instale as bibliotecas uma vez: `pip install numpy pillow imageio-ffmpeg`
3. Converta: `python gbvconv.py episodio.mkv`. Isso gera `episodio.gbv` ao lado
   do original, com no máximo 31 MB. A conversão leva mais ou menos o tempo do
   próprio vídeo, com o progresso no terminal (as mensagens são em inglês).
4. Copie o `.gbv` para qualquer pasta do SD e abra pela SuperFW.

Todos os quadros são mantidos; o que se ajusta ao limite de 31 MB é a qualidade
da imagem. Opções: `-o saida.gbv`, `--max-mb 31.5`, `--spv 264` (som melhor),
`--no-audio`, `--res 120x80` (meia resolução ampliada).

Botões no player: A ou Start pausa; ←/→ ±10 s; L/R ±60 s; ↑/↓ volume; Select fixa
a barra de tempo; B pausa, e B de novo sai.

O player vem embutido na firmware `chis`. Na `sd`, copie o `gbvplayer.gba` (anexado a
toda release) para `/.superfw/emulators/`.
