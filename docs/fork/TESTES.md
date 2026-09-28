# Roteiro de testes no GBA SP (SuperChis Prime)

Firmware: `superfw-chis-ecfcfde.fw` (branch `claude/project-thread-3djlx9`, commit `ecfcfde`).
Inclui tudo da `91feaa5` (tamanho das capas, /COVERS + /IMGS, toques de botão),
que ainda não voltou testada, e os patches IPS/UPS/BPS.

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
