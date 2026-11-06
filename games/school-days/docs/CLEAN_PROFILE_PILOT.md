# Rota piloto: perfil limpo, START e abertura do episódio 1

## Execução em 04/11/2026

O original v1.02 foi aberto a partir da cópia isolada `D-empty` do laboratório,
com `Save` vazio. O observador externo registrou um clique em START na posição
normalizada `(0.21, 0.82)` e 27 mudanças visíveis de quadro durante a janela de
16 segundos. As capturas amostrais mostraram tela preta inicial, logotipo
SCHOOL DAYS HQ e, depois, o telop `Episode 1` sobre vídeo. O jogo e o
observador foram encerrados após a amostra.

A build School Days de QA usou os mesmos GPK externos e um diretório de
`--documents` isolado e vazio. Sua ponte de desenvolvimento confirmou a cena
`00/00-00-A00` após START. As capturas nos marcos aproximados de 1, 4 e 9
segundos mostraram tela preta, logotipo e telop `Episode 1` sobre vídeo.
O primeiro clique tentado na build ocorreu durante o splash do Kotonoha e
apenas o dispensou; o ensaio válido clicou START depois de exibir o título.

As imagens e o JSONL ficaram apenas em
`build/school-days-release-audit/pilot-clean/`, ignorado pelo Git e contendo
arte original. Não são material de distribuição.

## Resultado e limite

A entrada no episódio 1 funciona nas duas versões no perfil limpo. O original
não expôs SceneKey por esse observador; a identificação interna da cena nele
permanece uma inferência do roteiro e do vídeo. Nos marcos de 9 segundos, as
capturas mostram quadros diferentes da mesma abertura. Isso ainda não prova
diferença de velocidade: o instante do clique, a transição e os métodos de
captura não foram sincronizados no mesmo referencial temporal.

## Próxima comparação

1. Fazer a mesma ação com capturas periódicas dos dois lados e alinhar pelo
   primeiro quadro da vinheta, não pelo instante de lançamento do processo.
2. Medir duração da vinheta, primeiro quadro do episódio e início do áudio.
   Se houver desvio estável, localizar se nasce na transição de título, no
   relógio do ORS ou no início do WMV.
3. Continuar a rota até a primeira escolha, testar Back, Save, saída e Load;
   comparar a sequência de cenas e os arquivos de perfil após cada ação.
4. Só investigar endereços do original com Ghidra/REA quando um desvio
   reproduzível indicar qual regra está faltando.

Nenhum código do jogo ou do motor foi alterado com base nesta amostra.
