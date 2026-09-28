# Roteiro de testes no GBA SP (SuperChis Prime)

Firmware: `superfw-chis-3029df1.fw` (branch `claude/project-thread-3djlx9`, commit `3029df1`).
Inclui tudo da `ecfcfde` (capas, /COVERS + /IMGS, toques de botão, patches
IPS/UPS/BPS), que ainda não voltou testada, mais a busca por nome, o player de
vídeo, a economia de bateria no menu e as opções de suspensão (itens 5 a 9).

## Antes de começar
- Faça backup da pasta `/.superfw` e dos seus `.sav` do SD.
- Rode primeiro como `.gba`, iniciando pela SuperFW instalada, igual aos testes
  anteriores.
- Anote o resultado de cada item: OK, falhou (o que aconteceu) ou não testado.

## 1. Tamanho das capas
1. Na primeira vez, as capas aparecem grandes (120x80), como no começo.
2. Aba UI → "Capas": ←/→ alterna entre Desativado, Pequena, Média e Grande. Volte ao
   navegador e confira cada tamanho. Salve, reinicie e confira que o tamanho
   continua o escolhido.
3. Nos três tamanhos, os nomes longos e o tamanho do arquivo não passam por cima da
   capa.

## 2. Pastas /COVERS e /IMGS
1. Deixe o seu pacote antigo do EZ-Flash em `/IMGS` e copie a pasta `COVERS` do zip
   `COVERS-superfw-titulos.zip` para a raiz do SD.
2. Jogos que existem no pacote novo mostram a capa limpa (sem "chuvisco").
3. O Metroid Zero Mission ProjectM volta a mostrar capa: a do pacote novo se o código
   for BMXE, ou a do `/IMGS` se o hack usar outro código.
4. A velocidade continua boa no tamanho grande (cache e pré-carregamento).

## 3. Botões
1. Dê vários toques rápidos em ↓ logo ao entrar numa pasta, enquanto as capas
   carregam: a seleção anda exatamente um item por toque.
2. R+↑/↓ e a troca de abas com L/R continuam funcionando.

## 4. Patches IPS/UPS/BPS (zip `patch-demo-superfw.zip`)
1. Copie a pasta `patch-demo` para o SD e abra `demo-ips.gba`. A tela de informações
   mostra "Patch IPS: ligado [SELECT]". Carregue: fundo verde, "PATCH IPS /
   APLICADO!" e "IPS: ROM MAIOR" em amarelo embaixo.
2. Repita com `demo-ups.gba` (fundo laranja) e `demo-bps.gba` (fundo roxo).
3. Abra de novo `demo-ips.gba`, aperte SELECT ("desligado") e carregue: fundo azul,
   "ROM ORIGINAL / SEM PATCH" e sem a linha amarela de baixo.
4. Se tiver uma tradução ou um ROM hack em patch, teste com um jogo de verdade:
   ponha `Jogo.ips` (ou `.ups`/`.bps`) ao lado de `Jogo.gba`, com o mesmo nome.
   Confira que o save e o in-game menu funcionam, e anote quanto tempo o
   carregamento levou a mais.

## 5. Busca por nome
1. Numa pasta com muitos jogos, aperte R+Start: abre o teclado.
2. Digite algumas letras com A (por exemplo "POK"). O primeiro arquivo que contém
   o texto aparece; L/R passam para o anterior e o próximo.
3. Start leva a seleção até ele; B fecha sem mudar a seleção.

## 6. Player de vídeo
1. No PC, instale o Python 3 e rode `pip install numpy pillow imageio-ffmpeg`.
2. Converta um episódio: `python3 gbvconv.py episodio.mkv` (gera
   `episodio.gbv`, até 31 MB). Anote quanto tempo levou.
3. Copie o `.gbv` para o SD e abra pela SuperFW. Confira que imagem e som estão
   sincronizados do começo ao fim, sem travadas.
4. Botões: A/Start pausa; ←/→ ±10 s; L/R ±60 s; ↑/↓ volume; Select fixa a barra;
   B pausa e B de novo volta à SuperFW.
5. Diga se a qualidade da imagem ficou aceitável e em quais cenas ficou pior.

## 7. Suspender com outra combinação
1. Configurações gerais → "Acordar com": escolha outra combinação (por exemplo
   L+R+A) e salve.
2. Entre num jogo, abra o in-game menu: o item deve mostrar "Suspender (L+R+A)".
3. Suspenda, espere uns minutos e acorde com a combinação nova. L+R+Select não
   deve mais acordar.

## 8. Suspensão automática no menu
1. Configurações gerais → "Suspender no menu": 2 min. Salve.
2. Deixe o GBA parado no navegador: depois de 2 minutos a tela apaga.
3. Acorde com a combinação escolhida: o menu volta como estava, sem ter recebido
   os botões da combinação (não troca de aba, não abre nada).
4. Com "Nunca", a tela não deve apagar.

## 9. Tecla do in-game menu e bateria
1. Troque a "Tecla do menu" para outra combinação, salve e reinicie o GBA. A
   escolha agora continua (antes voltava para L+R+Start).
2. Se puder comparar, anote quanto a pilha dura parada no menu em relação ao build
   anterior (o menu agora deixa a CPU parada entre os quadros).
