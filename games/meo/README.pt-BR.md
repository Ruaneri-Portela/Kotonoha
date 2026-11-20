# Miss Each Other (MEO)

[English (US)](README.md) | **Português (Brasil)**

Miss Each Other utiliza o runtime Overflow CMP / Summer Radish e o provedor `SummerAssets`. O alvo independente é `MEO`, com identificador `me.hirameki.kotonoha.meo`. O título da janela é **Miss Each Other**; o nome `meo` permanece como identificador interno.

## Compilação e inicialização

Configure o CMake com `-DKOTONOHA_BUILD_MEO=ON` e compile o alvo `MEO`. Informe dados originais obtidos legalmente por `--meo DIRECTORY|ISO`, `MEO_DIR` ou pela seleção de pasta do launcher. Dados locais podem ficar em `games/meo/game-assets/`.

## Testes e limitações

O CTest opcional `MEO.Presentation` exige `-DMEO_DATA_DIR=/caminho/para/dados`. Ele verifica inicialização e renderização visível, mas não comprova fidelidade integral de cenas, rotas ou plataformas. O codec CMP é compartilhado com Summer Radish, mas o jogo mantém identidade própria.
