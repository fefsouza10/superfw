# Roteiro de testes no GBA SP (SuperChis Prime)

Firmware: `superfw-chis-303ea6f.fw` (branch `claude/project-thread-3djlx9`, commit `303ea6f`).

## Antes de começar
- Faça backup da pasta `/.superfw` e dos seus `.sav` do SD.
- Teste primeiro sem gravar na flash: copie o `.fw` para o SD com a extensão `.gba` e
  inicie pela SuperFW instalada. Se ele não bootar assim, tente de novo com o in-game
  menu e os patches desligados no popup de carregamento.
- Anote o resultado de cada item: OK, falhou (o que aconteceu) ou não testado.

## 1. Básico (regressão das otimizações de memória)
1. O menu abre e os ícones das abas aparecem certos, sem ícones "fantasmas" e sem
   ícones faltando.
2. Navegar pelas abas com L/R, abrir popups (Select, carregar ROM) e fechar com B.
3. Carregar um jogo pelo SD e um pela NOR (se tiver), jogar um pouco e salvar.
4. In-game menu: abrir, fazer um savestate, carregar o savestate e voltar ao jogo.

## 2. Capas
1. Coloque um pacote de capas do EZ-Flash Omega no SD: `/IMGS/<1ª letra>/<2ª letra>/<CÓDIGO>.bmp`
   (por exemplo `/IMGS/B/P/BPEE.bmp`, BMP de 120x80 e 16 bits).
2. No navegador do SD, ao parar sobre um `.gba` que tem capa, ela aparece no canto
   inferior direito depois de um instante. Os nomes longos não passam por cima dela.
3. Rolar a lista rápido (segurar ↓) não deve travar. A capa só aparece quando você para.
4. A capa também aparece nas abas Recentes (inclusive para jogos da NOR), Favoritos e
   na aba da NOR.
5. As cores estão boas, com o dithering, e sem cores trocadas (vermelho e azul
   invertidos, por exemplo).
6. Aba UI → "Mostrar capas" → Desativado: as capas somem. Salvar, reiniciar e
   conferir que a opção continua como você deixou.
7. Pastas, arquivos que não são `.gba` e jogos sem capa não mostram painel nenhum.
8. A aba Info (logo) não deixa as cores da capa erradas quando você volta ao navegador.

## 3. Favoritos
1. Navegador do SD → Select → "Adicionar aos favoritos". Aparece a mensagem e surge a
   aba com a estrela.
2. Select de novo no mesmo arquivo: agora o botão diz "Remover dos favoritos".
3. Aba Recentes → Start num jogo do SD e num jogo da NOR: os dois vão para os favoritos.
   Start de novo remove.
4. Aba Favoritos: A abre o jogo e Select pede confirmação e remove.
5. Remover todos os favoritos faz a aba sumir, e o L/R deixa de parar nela.
6. Desligar e ligar: os favoritos continuam lá (`/.superfw/favorites.txt`).
7. Com "Recentes" desativado na aba UI, L/R continuam navegando certo entre as abas.

## 4. Pular por letra
1. Numa pasta com muitos jogos, o Start pula para a primeira entrada com a próxima
   letra inicial.
2. Na última letra, o Start volta ao topo da lista.
3. Na aba da NOR, o Start funciona da mesma forma.

## 5. Sleep (in-game menu)
1. Abrir o in-game menu: a última opção é "Suspender (L+R+Sel)", e todas as 7 opções
   cabem na tela.
2. Escolher Suspender: a tela apaga, e a luz do SP também apaga (ou não; anote).
3. Pressionar L+R+Select: o jogo volta de onde parou, com som e imagem normais.
4. **Teste crítico:** jogo carregado do SD, dormindo por 5 a 10 minutos. Depois de
   acordar, o jogo continua sem travar e sem gráficos corrompidos? Salve e confira se
   o save ficou bom.
5. O mesmo teste com um jogo rodando da NOR.
6. Opcional: deixar dormindo por mais tempo e comparar o gasto de bateria.

## Se algo falhar
Anote o que aconteceu e em qual item. Se possível, habilite o log
(`make ENABLE_DISK_LOGGING=1`, sob pedido) para investigar.
