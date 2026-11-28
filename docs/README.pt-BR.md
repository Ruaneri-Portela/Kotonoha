# Documentação

[English (US)](README.md) | **Português (Brasil)**

Comece pelo [README do projeto](../README.pt-BR.md) e escolha um assunto:

| Assunto | Guia |
| --- | --- |
| Identidade visual | [Carta de estilo oficial](../design/README.pt-BR.md) |
| Compilação | [Guias de compilação](BUILD_GUIDES.pt-BR.md) |
| Dependências nativas | [Homebrew / apt / MSYS2 UCRT / vcpkg / submódulos](DEPENDENCIES.pt-BR.md) |
| Windows (solução nativa) | [Visual Studio 18 / v145](BUILD_VISUAL_STUDIO.pt-BR.md) |
| Cross-build Windows | [MinGW em hosts macOS/Linux](BUILD_WINDOWS.pt-BR.md) |
| Android | [Android](BUILD_ANDROID.pt-BR.md) · [Gradle](../buildsystems/android/README.pt-BR.md) |
| Apple | [macOS](BUILD_MACOS.pt-BR.md) · [iOS](BUILD_IOS.pt-BR.md) · [Xcode](../buildsystems/xcode/README.pt-BR.md) |
| Navegador | [WebAssembly](BUILD_WEB.pt-BR.md) |
| Testes | [Validação](BUILD_VALIDATION.pt-BR.md) · [Grupos de teste](TESTING.pt-BR.md) |
| Licenças e procedência | [Auditoria do repositório](LICENSING.pt-BR.md) · [Avisos de terceiros](../THIRD_PARTY_NOTICES.pt-BR.md) |
| CI, artefatos e cache de dependências | [GitHub Actions e vcpkg](CI_VCPKG.pt-BR.md) |
| Distribuição, callbacks SDL e ZIP | [Deploy e CPack](BUILD_PACKAGES.pt-BR.md) |
| Launcher unificado e bridges | [Arquitetura do launcher](../launcher/README.pt-BR.md) · [Gerenciador desktop](../launcher/desktop/README.pt-BR.md) |
| Assets e patches | [Overlays e traduções](ASSET_OVERLAYS.pt-BR.md) |
| Caminhos de arquivos | [Política de diretórios](DIRECTORY_POLICY.pt-BR.md) |
| Interface, legendas e preferências | [Configurações do motor](ENGINE_SETTINGS.pt-BR.md) |
| Touchscreen, teclado e controles | [Entrada unificada](INPUT_CONTROLS.pt-BR.md) · [Matriz por port](INPUT.pt-BR.md) |
| Debug UI, applets e versão Git | [Depuração e diagnósticos SDL](DEBUG_TOOLS.pt-BR.md) · [Integração nos ports](PORT_DEBUG_INTEGRATION.pt-BR.md) |
| Ports de jogos | [Índice de ports](../games/README.pt-BR.md) · [Revisão de compatibilidade](PORT_COMPATIBILITY.pt-BR.md) |
| Estrutura interna | [Máquinas virtuais](../modules/vms/README.pt-BR.md) · [Provedores](../modules/providers/README.pt-BR.md) · [Formatos de arquivo](../modules/formats/README.pt-BR.md) |

## Padrão de documentação

Guias ativos são concisos e mantidos em **en-US** (`*.md`) e **pt-BR** (`*.pt-BR.md`). Preserve nomes de comandos, símbolos e caminhos ao traduzir. Atualize os dois arquivos e confira os links.

Arquivos `PORT_STATUS.md`, notas datadas em `games/*/docs/`, licenças/avisos de terceiros e instruções das pastas de assets podem conter evidências técnicas ou obrigações legais. São **registros de referência**, não garantias do estado atual. Ao atualizar um resumo, preserve investigações e utilize o [índice de ports](../games/README.pt-BR.md) como ponto de entrada.

## Filesystem virtual experimental

O [Filesystem V2](filesystem/README.pt-BR.md), experimental e somente para leitura, já está na árvore de código da `dev`, enquanto a migração dos leitores legados ocorre de forma incremental. Consulte a [especificação de arquitetura](filesystem/SPECIFICATION.pt-BR.md) como referência de projeto; nenhum port deve ser considerado inteiramente migrado sem verificação.

## QA do School Days HQ e segurança de dados

Preserve a suíte de regressão visual e funcional do School Days HQ (Windows x64 e Android SAF), utilizando apenas fixtures sintéticas e perfis isolados via `--documents` e `--config`. Consulte [FORK_WORKFLOW.pt-BR.md](FORK_WORKFLOW.pt-BR.md) e [KNOWN_STATE.pt-BR.md](KNOWN_STATE.pt-BR.md). Nunca siga junctions NTFS em rotinas de limpeza nem altere binários retail originais, arquivos GPK, traduções privadas, distribuições QA instaladas ou saves pessoais. Capturas históricas de QA documentam as condições da época, não necessariamente cada evolução posterior do motor.
