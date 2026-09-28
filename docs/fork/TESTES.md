# Roteiro de testes no GBA SP (SuperChis Prime)

Firmware: `superfw-chis-a504000.fw` (branch `claude/project-thread-3djlx9`, commit `a504000`).
Teste anterior: `303ea6f` (28/09). Os resultados estão em `REGISTRO.md`.

## Antes de começar
- Faça backup da pasta `/.superfw` e dos seus `.sav` do SD.
- Rode primeiro como `.gba`, iniciando pela SuperFW instalada, igual ao teste anterior.
- Anote o resultado de cada item: OK, falhou (o que aconteceu) ou não testado.

## 1. Capas maiores (76x50) com o pacote atual (16 bits)
1. Com o seu pacote atual do EZ-Flash, as capas aparecem 25% maiores. A velocidade e
   o cache continuam como antes.
2. Os nomes longos e o tamanho do arquivo não passam por cima da capa.

## 2. Capas de alta qualidade (8 bits)
1. Faça backup da sua pasta `/IMGS` e troque-a pela do pacote
   `IMGS-superfw-titulos.zip` (ou pela saída do `convert_covers.py`).
2. As capas ficam bem mais bonitas, sem o "chuvisco" do dithering.
3. A velocidade: parar num jogo novo, pré-carregar os vizinhos e voltar a jogos já
   vistos continuam tão rápidos quanto antes, ou mais.
4. Passe por várias capas seguidas: não aparecem cores erradas (paleta de uma capa
   na imagem de outra).
5. Um jogo sem capa no pacote não mostra painel nenhum.

## 3. Regressão rápida
1. A aba Info (logo) e voltar ao navegador: as cores da capa estão certas.
2. Aba UI → "Mostrar capas" continua ligando e desligando as capas.

