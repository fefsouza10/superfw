# Roteiro de testes no GBA SP (SuperChis Prime)

Firmware: `superfw-chis-HASH.fw` (branch `claude/project-thread-3djlx9`, commit `HASH`).
Corrige o in-game menu que travava na `3029df1`, traz o vídeo com imagem melhor
e com som, e o novo modo carrossel das capas. Os itens 1 a 5 da rodada anterior
passaram e não precisam ser repetidos.

## Antes de começar
- Faça backup da pasta `/.superfw` e dos seus `.sav` do SD.
- Rode primeiro como `.gba`, iniciando pela SuperFW instalada, igual aos testes
  anteriores.
- Anote o resultado de cada item: OK, falhou (o que aconteceu) ou não testado.

## 1. In-game menu (era o item 7 que travava)
1. Abra um jogo pelo SD e aperte a tecla do menu (L+R+Start, ou a que você
   escolheu): o menu tem que abrir normalmente.
2. Teste "Voltar ao jogo", um savestate (salvar e carregar) e o "Suspender".
3. Repita com um jogo da NOR.
4. Abra um jogo com patch IPS (`demo-ips.gba` do zip de patches) e abra o menu
   nele também.

## 2. Suspender com outra combinação (item 7 da rodada anterior)
1. Configurações gerais → "Acordar com": escolha outra combinação (por exemplo
   L+R+A) e salve.
2. Entre num jogo, abra o in-game menu: o item deve mostrar "Suspender (L+R+A)".
3. Suspenda, espere uns minutos e acorde com a combinação nova. L+R+Select não
   deve mais acordar.

## 3. Suspensão automática no menu (item 8)
1. Configurações gerais → "Suspender no menu": 2 min. Salve.
2. Deixe o GBA parado no navegador: depois de 2 minutos a tela apaga.
3. Acorde com a combinação escolhida: o menu volta como estava, sem ter recebido
   os botões da combinação (não troca de aba, não abre nada).
4. Com "Nunca", a tela não deve apagar.

## 4. Tecla do in-game menu e bateria (item 9)
1. Troque a "Tecla do menu" para outra combinação, salve e reinicie o GBA. A
   escolha continua (antes voltava para L+R+Start), e o menu abre com ela no jogo.
2. Se puder comparar, anote quanto a pilha dura parada no menu.

## 5. Vídeo (zip `conversor-video-superfw.zip`)
1. Copie `exemplo-desenho.gbv` para o SD e abra: agora tem som e a imagem é bem
   mais nítida que antes.
2. Converta de novo o episódio que você testou (os `.gbv` antigos ainda tocam,
   mas com a qualidade antiga): `python3 gbvconv.py episodio.mkv`. A conversão
   agora leva mais ou menos o tempo do vídeo (uns 20 min para 20 min). Anote o
   tempo.
3. Confira som e imagem sincronizados do começo ao fim, sem travadas, inclusive
   depois de avançar/voltar com ←/→ e L/R.
4. Diga como ficou a qualidade e em que tipo de cena ainda fica ruim.

## 6. Carrossel de capas (novo)
1. Aba UI → "Exibição": ←/→ troca para "Carrossel". Salve.
2. Na aba do SD, numa pasta de jogos: a capa do jogo selecionado fica no meio, o
   anterior e o seguinte aparecem pequenos nas laterais, e o nome e o tamanho
   embaixo.
3. ←/→ vão para o jogo anterior/seguinte; ↑/↓ pulam uma página; R+↑/↓ pulam de
   letra; A abre; Start marca favorito; B volta de pasta; Select abre o
   gerenciador de arquivos.
4. Confira nas abas da NOR, Recentes e Favoritos.
5. Pastas e arquivos sem capa mostram só o ícone. Nomes longos rolam na tela.
6. Teste os três tamanhos de capa com o carrossel e diga qual ficou melhor.
7. Volte para "Lista": a lista fica igual a antes.
