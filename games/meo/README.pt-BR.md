# Miss Each Other (MEO)

[English (US)](README.md) | **Português (Brasil)**

Miss Each Other utiliza o runtime Overflow CMP / Summer Radish e o provedor `SummerAssets`. O alvo independente é `MEO`, com identificador `me.hirameki.kotonoha.meo`. O título da janela é **Miss Each Other**; o nome `meo` permanece como identificador interno.

## Compilação e inicialização

Configure o CMake com `-DKOTONOHA_BUILD_MEO=ON` e compile o alvo `MEO`. Informe dados originais obtidos legalmente por `--meo DIRECTORY|ISO`, `MEO_DIR` ou pela seleção de pasta do launcher. Dados locais podem ficar em `games/meo/game-assets/`.

## Testes e limitações

Os CTests opcionais `MEO.Presentation` e `MEO.Audio` exigem `-DMEO_DATA_DIR=/caminho/para/dados`. Eles verificam inicialização visível, decodificação de BGM e SND e solicitação de música e voz na abertura, mas não comprovam fidelidade integral da história, rotas ou plataformas. O provedor de arquivos CMP é compartilhado com Summer Radish, mas o jogo mantém identidade própria. A música `.BGM` e as vozes `.SND` do MEO tocam pelo decoder ADPCM próprio, com os 24 frames de script por segundo do jogo original.
