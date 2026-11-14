# Documentação

[English (US)](README.md) | **Português (Brasil)**

Comece pelo [README do projeto](../README.pt-BR.md) e escolha um assunto:

| Assunto | Guia |
| --- | --- |
| Compilação | [Guias de compilação](BUILD_GUIDES.pt-BR.md) |
| Android | [Android](BUILD_ANDROID.pt-BR.md) · [Gradle](../buildsystems/android/README.pt-BR.md) |
| Apple | [macOS](BUILD_MACOS.pt-BR.md) · [iOS](BUILD_IOS.pt-BR.md) · [Xcode](../buildsystems/xcode/README.pt-BR.md) |
| Navegador | [WebAssembly](BUILD_WEB.pt-BR.md) |
| Testes | [Validação](BUILD_VALIDATION.pt-BR.md) |
| Assets e patches | [Overlays e traduções](ASSET_OVERLAYS.pt-BR.md) |
| Caminhos de arquivos | [Política de diretórios](DIRECTORY_POLICY.pt-BR.md) |
| Interface, legendas e preferências | [Configurações do motor](ENGINE_SETTINGS.pt-BR.md) |
| Ports de jogos | [Índice de ports](../games/README.pt-BR.md) |
| Estrutura interna | [Máquinas virtuais](../modules/vms/README.pt-BR.md) · [Provedores](../modules/providers/README.pt-BR.md) |

## Padrão de documentação

Guias ativos são concisos e mantidos em **en-US** (`*.md`) e **pt-BR** (`*.pt-BR.md`). Preserve nomes de comandos, símbolos e caminhos ao traduzir. Atualize os dois arquivos e confira os links.

Arquivos `PORT_STATUS.md`, notas datadas em `games/*/docs/`, licenças/avisos de terceiros e instruções das pastas de assets podem conter evidências técnicas ou obrigações legais. São **registros de referência**, não garantias do estado atual. Ao atualizar um resumo, preserve investigações e utilize o [índice de ports](../games/README.pt-BR.md) como ponto de entrada.

## Filesystem virtual experimental

A implementação inicial de [Filesystem V2](filesystem/README.pt-BR.md) está isolada em um PR de rascunho. A [especificação original](filesystem/SPECIFICATION.pt-BR.md) é a referência; nenhum port será considerado migrado sem testes.
