# Compatibilidade compartilhada de superfícies e transparência

[English (US)](GRAPHICS_SURFACE_COMPATIBILITY.md)

## Problema

Arte de jogos pode usar canal alfa, cor-chave RGB ou paletas indexadas com
índice transparente. Transferir a imagem decodificada diretamente para a
textura do SDL sem resolver a cor-chave pode produzir fundos opacos. Na
interpolação linear, pixels transparentes com RGB preto podem criar bordas
escuras.

## Implementação compartilhada

`prepareSurfaceForLinearSampling(SDL_Surface *)` cria uma nova superfície
RGBA32 para os caminhos de upload do motor, preservando a origem, o modo de
mesclagem, a paleta e a cor-chave. Para imagens com cor-chave, o blitter do
SDL respeita o formato e o índice original ao copiar pixels visíveis para
uma nova superfície transparente RGBA32. Isso funciona até quando dois
índices têm a mesma cor RGB. Nos demais casos, a conversão usa
`SDL_ConvertSurface`.

Após normalizar, o algoritmo existente copia as cores RGB para a margem de
um pixel completamente transparente. Nenhum alfa nem pixel visível é
alterado. A preparação é aplicada ao `AssetProvider::stageTexture`, à
escala de imagens do provider (incluindo upscaling) e às imagens de abertura
do motor.

## Testes e limites

- `Kotonoha.MediaAnalysis`: recursos sintéticos RGB24, BGRA32, paleta
  indexada, preservação da superfície de origem, overlay ampliado por
  interpolação linear e composição sobre um canvas opaco de 800 × 600.
- `TextureAlphaStandalone.cpp`: C++17 independente de SDL e recursos de
  jogos; verifica bordas de alfa, pitch com padding e dimensões inválidas.
  O workflow `texture-alpha.yml` roda nos PRs e em pushes para `dev`.
- A comparação visual real por GPU, plataforma e upscaler continua sendo
  necessária em testes integrados.
- Este avanço não decodifica formatos proprietários desconhecidos nem
  implementa telas de jogo específicas ou chamadas nativas de scripts.
  Nenhuma arte comercial é incorporada ao projeto.
