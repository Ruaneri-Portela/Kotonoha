# Estrutura do CMake

[English (US)](README.md) | **Português (Brasil)**

O arquivo `CMakeLists.txt` na raiz do repositório apenas orquestra a
configuração. Mantenha definições de plataforma e opções compartilhadas no
nível superior e organize as responsabilidades nos diretórios abaixo:

- `dependencies/`: descoberta e seleção de SDL3 / SDL3_ttf, Dear ImGui
  (submódulo fixado ou vcpkg), FFmpeg, libass e json-c, além da inicialização
  dos submódulos necessários (`KotonohaDepsProvider.cmake`).
- `games/`: lista canônica de jogos, criação e seleção de alvos e configuração
  específica de aplicativos por plataforma.
- `packaging/`: assets, implantação de bibliotecas de runtime compartilhadas,
  bundles `.app` no macOS, Python, arquivos ZIP nativos via CPack, layout de
  saída e templates PWA por jogo para WebAssembly.

`BuildVersion.cmake`, `KotonohaAndroid.cmake`, `KotonohaOptions.cmake`,
`KotonohaReport.cmake` e `KotonohaTestGroups.cmake` permanecem na raiz de
`buildsystems/cmake/` por tratarem de configuração comum do projeto e relatórios,
e não de uma categoria específica.

Ao mover módulos auxiliares, atualize invocações em modo script (`-P`), filtros
de caminhos nos workflows de CI e os testes de contrato em `tools/`. Não
adicione includes legados de compatibilidade nem atalhos específicos de
compilador para launchers.
