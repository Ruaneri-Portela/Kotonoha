# Auditoria de organização e licenciamento do repositório

[English (US)](LICENSING.md) | **Português (Brasil)**

Retrato da auditoria: **22/11/2026**, branch `dev`. Esta é uma auditoria
de fontes e do repositório, não um parecer jurídico nem confirmação de
execução de todos os jogos em todas as plataformas.

## Escopo e organização

- Motor compartilhado C++17 em `lib/`, VMs em `modules/vms/`, providers de
  formatos em `modules/providers/` e runtimes dos jogos em `games/`.
- A árvore atual contém **16 aplicações (15 ports + Example)**, além do
  launcher unificado. Prefira `games/README.pt-BR.md` e o manifesto de build
  a números fixos desatualizados.
- Compilação e empacotamento ficam em `buildsystems/`; verificações em `tools/`.
- Preserve `PORT_STATUS.md` e investigações históricas. Atualize os guias
  ativos em inglês e português simultaneamente.
- Preserve URLs de origem, licenças e créditos dos contribuidores. Uma
  referência externa usada para interoperabilidade não autoriza
  automaticamente copiar seu código para o Kotonoha.

## Achados e medidas

| Prioridade | Achado | Medida / verificação restante |
| --- | --- | --- |
| Alta | Faltava licença na raiz, embora `lib/LICENSE` contivesse MIT | Adicionar `LICENSE` para o código original do Kotonoha; distinguir terceiros em `THIRD_PARTY_NOTICES.md` |
| Alta | O CPack de `.app` macOS copiava bundles inteiros, podendo incluir `game-assets` locais | Excluir `game-assets` por padrão nos dois formatos; opção explícita só para arquivos cuja distribuição seja autorizada |
| Alta | O Gradle Android incluía arquivos `game-assets` comerciais automaticamente | Alterar o padrão para `-PbundleGameAssets=false`, não incluir esses dados em `dist` e empacotar avisos básicos de licença |
| Alta | Os ZIPs não instalavam explicitamente avisos de licença | Incluir avisos básicos em todos os componentes, inclusive em `.app/Contents/Resources/Legal`. **Antes de distribuir**, reunir licenças de todas as bibliotecas de terceiros realmente empacotadas |
| Média | A `.gitignore` tinha padrões de debug concatenados | Separar os padrões; verificar segredos e archives comerciais no Git |
| Média | Contagens de aplicações estavam desatualizadas em README/AGENTS/guias | Alinhar os documentos ao manifesto de 16 aplicações |
| Média | `assets/Anime4K/SOURCE.txt` documentava apenas três shaders, apesar de haver muito mais | Tornar a lacuna explícita; comparar a revisão de cada variante com a origem antes de redistribuí-la |
| Depende do titular | `assets/SPLASH.PNG`, dois ícones e `lib/tests/fixtures/SvgTestFont.ttf` não apresentam documentação clara de origem no material auditado | Confirmar autoria, condições da fonte e direitos de distribuição; não pressupor aprovação |
| Contínua | Ainda não foi demonstrada compatibilidade total de N2System/LucaSystem com todos os jogos, saves e plataformas | Manter os checklists nas issues #83 e #75 até existirem provas verificáveis |

## Checklist de distribuição

1. Levantar as bibliotecas e os recursos efetivamente incluídos em cada
   pacote Windows, Linux, macOS, iOS, Android e WebAssembly. `vcpkg.json`
   não equivale à lista final de binários.
2. Incluir os avisos aplicáveis para SDL, SDL_ttf, Dear ImGui, FFmpeg, libass,
   CPython e dependências transitivas distribuídas. Conferir separadamente
   opções GPL ou nonfree na configuração do FFmpeg.
3. Inspecionar os arquivos gerados pelo `cpack` em cada plataforma: ausência
   de `game-assets/` por padrão, presença das licenças e ausência de fontes
   de teste ou arquivos comerciais acidentais.
4. Preservar créditos e licenças de Anime4K, ArtCNN e porções do open-amagami.
5. Executar `python3 tools/ValidateRepositoryHygiene.py` antes de publicar.

Veja o inventário em [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
