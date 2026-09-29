# Roteiro de testes no GBA SP (SuperChis Prime)

Rodada da próxima versão: tempo de jogo e conversor de vídeo melhorado.
Firmware: `superfw-chis-tempo.fw` (branch `claude/project-thread-3djlx9`, o commit
está no nome do arquivo entregue). Tudo o que foi validado na v0.21-fefsouza10.1
continua igual e não precisa ser repetido.

## Antes de começar
- Faça backup da pasta `/.superfw` e dos seus `.sav` do SD.
- Os itens 1 e 2 podem ser testados rodando como `.gba`. O item 3 (NOR) só vale
  com a firmware gravada, porque o código que conta o tempo nos jogos da NOR fica
  na flash da firmware.
- Anote o resultado de cada item: OK, falhou (o que aconteceu) ou não testado.

## 1. Tempo de jogo, jogo com save SRAM/EEPROM ou sem save (SD)
1. Abra um jogo desses (quase todos que não são Pokémon) com o in-game menu
   ligado e jogue uns 5 minutos.
2. Abra o in-game menu: ao lado de "Voltar ao jogo" aparece "5m jogados" (mais ou
   menos). Volte ao jogo.
3. Salve no jogo normalmente e desligue o console direto, sem abrir o menu.
4. Ligue de novo e abra a tela de informações do mesmo jogo: tem que mostrar
   "Tempo de jogo: 5m" (ou o que você jogou).
5. Carregue o jogo: o save tem que estar lá, igual (o contador usa 4 bytes livres
   da SRAM e devolve os originais antes de gravar o `.sav`).
6. Jogue mais um pouco: o total soma com o anterior.

## 2. Tempo de jogo, jogo com save Flash (Pokémon)
1. Jogue uns minutos, abra o in-game menu (confira o tempo) e volte ao jogo.
2. Desligue o console. Ao ligar, a tela de informações mostra o tempo até a hora
   em que o menu foi aberto (o tempo depois disso se perde nesses jogos).
3. Confira que o save do Pokémon continua perfeito.

## 3. Tempo de jogo na NOR (só com a firmware gravada)
1. Grave a firmware (`.fw`) e abra um jogo da NOR com o in-game menu.
2. Repita o item 1 (ou 2, se for Pokémon) com ele.
3. Se o jogo tiver mais de 28 MB e foi gravado na NOR com uma versão antiga, grave
   de novo antes.

## 4. Conversor de vídeo (`conversor-video-superfw.zip` novo)
1. Converta de novo o episódio de sempre com `--parts 2`:
   `python3 gbvconv.py episodio.mkv --parts 2`.
2. Veja as duas partes: as cenas com movimento e as transições (fades) devem estar
   bem melhores que antes.
3. Diga em que tipo de cena ainda fica ruim.
