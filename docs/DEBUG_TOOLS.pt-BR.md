# Debug UI, versão do build e diagnósticos SDL

[English (US)](DEBUG_TOOLS.md) | **Português (Brasil)**

## Versão automática baseada no Git

O CMake raiz inclui `buildsystems/cmake/BuildVersion.cmake` antes de
compilar a engine e os portes. Durante a configuração consulta a branch,
o SHA completo e curto do commit e se os arquivos rastreados sofreram
modificações. Gera o cabeçalho
`build/generated/Kotonoha/core/BuildVersion.hpp` (o caminho depende do
diretório de build). O código-fonte não é modificado.

Exemplo de versão:
`0.1.0+feature-wasm-custom-folders-20261114.0123456789ab`;
com alterações locais, termina em `.dirty`. A versão base segue o
`project(... VERSION ...)` do CMake. Pacotes sem metadados Git mostram
`unknown`. HEAD e a referência da branch são acompanhados pelo CMake
para regenerar o cabeçalho depois de trocar de branch ou criar um commit,
em geradores que suportam dependências de reconfiguração.

Todos os jogos compartilham o parser `CommandLine`:

```sh
./build/outputs/Kotonoha/Kotonoha --version
./build/outputs/Kotonoha/Kotonoha -V
```

O programa imprime versão, branch, commit e estado dos arquivos
rastreados, sem precisar inicializar a janela ou carregar dados do jogo.
`--help` continua disponível.

## Interfaces

O launcher exibe a versão no cabeçalho e no rodapé e possui a aba
**System info** com os diagnósticos SDL3. O seletor de origem dos arquivos
no HTML do WebAssembly também mostra a versão completa antes do jogo iniciar.

Dentro do jogo, abra a Debug UI (**F10**), escolha **System info** no
menu Applets, clique na opção **System info** do painel principal ou acesse
a aba homônima em Options. A janela de informações é independente e apenas
para leitura; **Refresh information** atualiza os dados e
**Copy diagnostic report** copia o relatório para a área de transferência.

São exibidos: versão SDL compilada e carregada, Git/CMake, plataforma,
núcleos lógicos de CPU, RAM total instalada (MiB), cache/SIMD, backend
gráfico, textura máxima, VSync, janela, densidade de pixels, monitores e
taxas de atualização, estado da bateria, controles, dispositivos de toque
e saídas de áudio.

A API SDL3 utilizada **não informa** de forma portátil o modelo da CPU,
a RAM livre/consumida pelo processo nem a VRAM. O nome físico da GPU aparece
somente quando o renderer SDL GPU disponibiliza a propriedade do dispositivo;
com OpenGL/Metal/outros renderers pode não estar disponível. Nenhum valor
desconhecido é inventado. Os diagnósticos são locais, sem telemetria.

## Validação

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target KotonohaBuildVersionTests KotonohaLauncherTests
ctest --test-dir build --output-on-failure -R 'Kotonoha\.(BuildVersion|Launcher)'
```

Em Linux sem interface gráfica, use
`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy` ao executar manualmente;
o CTest já define o ambiente. A CI Linux executa testes do parser,
launcher, Debug UI e filesystem. Testes em navegador e em GPU/celular real
são verificações separadas.
