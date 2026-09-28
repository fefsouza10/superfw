# Roteiro de testes no GBA SP (SuperChis Prime)

Firmware: `superfw-chis-89be7ee.fw` (branch `claude/project-thread-3djlx9`, commit `89be7ee`).
Teste anterior: `303ea6f` (28/09). Os resultados estão em `REGISTRO.md`.

## Antes de começar
- Faça backup da pasta `/.superfw` e dos seus `.sav` do SD.
- Rode primeiro como `.gba`, iniciando pela SuperFW instalada, igual ao teste anterior.
- Anote o resultado de cada item: OK, falhou (o que aconteceu) ou não testado.

## 1. Crash da aba de configurações (corrigido)
1. Com L/R, vá até a aba de configurações gerais. Ela abre sem travar.
2. Role a lista inteira com ↓ e ↑, mude uns valores com ←/→ e volte com L.
3. Vá até a aba UI, desative "Mostrar capas", salve e reinicie. Confira que as capas
   somem e que a opção continua desativada. Depois reative. (Este é o 2.6 que ficou
   pendente.)

## 2. Troca de abas (mudou)
1. L/R agora trocam de aba **ao soltar** o botão, não ao apertar. Confira que isso
   não incomoda no uso.
2. Nos popups (carregar ROM etc.), L/R continuam trocando de sub-página.

## 3. Capas (60x40, com cache)
1. A capa aparece menor no canto inferior direito. Ela está mais bonita do que antes?
2. Segurar ↓ para rolar a lista: nada carrega no caminho.
3. Pare num jogo por ~1/3 s: o menu carrega em silêncio as capas dos jogos em volta.
   Depois, ↑/↓ pela página deve mostrar cada capa **na hora**.
4. Volte a uma pasta ou a um jogo já visitado: a capa aparece na hora.
5. Dê toques rápidos em ↓ logo depois de parar num jogo: nenhum toque se perde, e a
   navegação não "engasga".
6. Os nomes longos e o tamanho do arquivo não passam por cima da capa.

## 4. Pular por letra (mudou)
1. No navegador do SD, segure R e aperte ↓: vai para o primeiro jogo da próxima letra.
   Com R segurado, cada ↓ pula mais uma letra. Ao soltar R, **não** troca de aba.
2. R+↑ volta para o início da letra anterior. No topo da lista, vai para a última letra.
3. O mesmo na aba da NOR.
4. Ao pular várias letras seguidas, nenhuma capa carrega até você soltar os botões.

## 5. Favoritos com Start
1. Navegador do SD: Start num arquivo mostra "Adicionado aos favoritos", e Start de
   novo remove. Em pastas, o Start não faz nada.
2. Aba da NOR: Start favorita o jogo (era o item 3.3 que falhou), e Start de novo
   remove.
3. Na aba Favoritos, abra o jogo da NOR com A.
4. Aba Recentes: Start num jogo do SD e num da NOR continua funcionando.

## 6. Sleep (só o teste longo que faltou)
1. Jogo do SD dormindo por 5 a 10 minutos. Depois de acordar com L+R+Select, o jogo
   continua sem travar e o save funciona?
2. O mesmo com um jogo da NOR.
3. O LED verde fica aceso porque é o LED de energia, ligado direto ao interruptor do
   SP. Isso é esperado.
