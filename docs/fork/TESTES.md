# Roteiro de testes no GBA SP (SuperChis Prime)

Firmware: `superfw-chis-91feaa5.fw` (branch `claude/project-thread-3djlx9`, commit `91feaa5`).
Teste anterior: `f142163` (28/09): capas pequenas e Metroid ZM sem capa, corrigidos aqui.

## Antes de começar
- Faça backup da pasta `/.superfw` e dos seus `.sav` do SD.
- Rode primeiro como `.gba`, iniciando pela SuperFW instalada, igual ao teste anterior.
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
