# Kotonoha v3 / School Days HQ v1.02 — handoff técnico Windows L4

Este documento é para entregar **código e instruções**, sem fazer pull request e
sem copiar material comercial para o Git. O jogo usa o **MCP nativo do motor**
(`lib/src/mcp/MCPServer.cpp`), com transporte stdio e socket local opcional;
o servidor próprio `SchoolDaysDevBridge` e o MCP Python separado foram removidos.

## 1. O que o repositório deve fornecer

- Código `lib/`, `games/school-days/`, `cmake/` e `tools/` versionado;
- `games/school-days/game-assets/school-days-hq.ktnroute` e `styles.ass`;
- shaders em `assets/Anime4K/` e `assets/ArtCNN/`;
- recursos Win32 opcionais `games/school-days/windows/SchoolDays.rc`, com
  `SchoolDays.ico` **fornecido separadamente**;
- scripts `games/school-days/tools/{build_lxh_gpk.py,publish_two_dll_l4.ps1,
  stage_clean_local_l4.ps1}` e manifesto vcpkg local;
- testes de engine e do jogo. O teste padrão usa fixtures sintéticos; os GPKs
  originais **não são necessários só para compilar** o binário.

### SDK e dependências de compilação

- Windows x64, CMake >= 3.18, MSVC com C/C++ e Resource Compiler Windows SDK,
  Python 3 para scripts opcionais e vcpkg;
- submódulos: `lib/submodules/SDL`, `lib/submodules/SDL_ttf`,
  `lib/submodules/imgui` (`git submodule update --init --recursive`);
- manifesto local de duas DLLs:
  `games/school-days/tools/two-dll-vcpkg/vcpkg.json`;
- dependências vcpkg: freetype, harfbuzz, json-c, libass, libiconv,
  plutosvg[freetype], zlib, ffmpeg[zlib] e pkgconf (host);
- configuração L4 auditada: triplet `x64-windows-static-md`, engine e ImGui
  estáticos, SDL3 e SDL3_ttf compartilhados, build `Release`, sem
  `KOTONOHA_DEV_CONTROL` do antigo servidor do jogo;
- Windows fornece Winsock2; seu import/link pertence ao **motor genérico MCP**
  apenas quando habilitado. Não há outra DLL de socket distribuída.

Exemplo de build em um clone limpo (adaptar caminho de vcpkg):

```powershell
git submodule update --init --recursive
cmake -S . -B build/windows-school-days-l4 -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
  -DVCPKG_MANIFEST_DIR="games/school-days/tools/two-dll-vcpkg" `
  -DKOTONOHA_STATIC_ENGINE=ON -DKOTONOHA_STATIC_IMGUI=ON `
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DBUILD_TESTING=ON
cmake --build build/windows-school-days-l4 --config Release --parallel 1
ctest --test-dir build/windows-school-days-l4 -C Release --output-on-failure
```

O CMake, o compilador e o vcpkg precisam ser os da mesma arquitetura.
Não reutilizar caches do desenvolvedor com caminhos absolutos de outro PC.

## 2. Arquivos necessários para executar School Days HQ

Na instalação local Windows L4, a estrutura foi validada assim:

```text
School Days HQ/
  SchoolDays.exe                 # launcher embutido (abre primeiro)
  SDL3.dll                       # dependência local de runtime
  SDL3_ttf.dll                   # dependência local de runtime
  Packs/
    BGM.GPK
    Event00.GPK ... Event05.GPK
    Ini.GPK
    Movie00.GPK ... Movie05.GPK
    Script.GPK
    Se00.GPK ... Se05.GPK
    SysSe.GPK
    System.gpk
    Voice00.GPK ... Voice05.GPK
    LxH.GPK                      # extensão integrada obrigatória desta versão
  game-assets/
    school-days-hq.ktnroute
    styles.ass
  assets/
    SPLASH.PNG
    icon.ico                     # existente no engine
    SchoolDaysHQ-Launcher.png    # arte de fundo anexada pelo usuário
    fonts/NotoSansJP-Regular.ttf
    Anime4K/*                    # shaders + avisos de licença
    ArtCNN/*                     # shaders + avisos de licença
  display-options/
    display_options_controls.json
    System/Option/*              # artes PNG e mapas CMAP
  translations/
    en-lxh.json
    en-lxh-reviewed-identities.json
    es.json
    es-reviewed-identities.json
    es-lxh.json
    es-lxh-reviewed-identities.json
    zh-CN.json
    zh-CN-lxh.json
  third-party-licenses/*        # avisos pertinentes às DLLs e bibliotecas
```

`Packs/` deve conter **29 GPKs originais + LxH.GPK** (30 arquivos).
Original `Script.GPK` provê os diálogos ingleses; `en-lxh.json` acrescenta
273 falas aos sete epílogos. JSONs espanhóis e chineses são traduções
alternativas, validadas sem fallback silencioso para inglês. Os arquivos
`*-reviewed-identities.json` documentam entradas iguais à fonte que foram
explicitamente revisadas. A identificação `Juan Ochoa (JNC)- LxH .2021^`
é exibida somente nas traduções espanholas, não em inglês nem chinês.

Os recursos de tema `display-options/` e o PNG do launcher são arquivos
externos ao GPK e **não devem ser fundidos aos packs comerciais**.
`SchoolDays.ico` é incorporado ao `.exe` via `SchoolDays.rc` quando presente
na pasta `games/school-days/windows/`. Seu desenho preto e branco e o PNG
do launcher são artes fornecidas pelo usuário; encaminhar separadamente,
com permissão de uso, porque não são parte do código-fonte.

### Tipografia e dependências do Windows

`NotoSansJP-Regular.ttf` deve estar disponível em `assets/fonts/` e com
licença apropriada. A fonte não cobre todos os caracteres da tradução
chinesa; no Windows testado, o fallback `Microsoft YaHei` (`msyh.ttc`)
ofereceu cobertura, mas **não pode ser copiado/redistribuído automaticamente**.
Outra instalação precisa fornecer uma fonte CJK compatível por meios lícitos.
Além das duas DLLs locais, o Windows e o Microsoft Visual C++ Runtime x64
fornecem bibliotecas de sistema — duas DLLs não significa zero dependências.

## 3. O que fornecer ao Ruaneri separadamente

1. **Para compilar:** clone, submódulos e vcpkg; não enviar `build/`, caches,
   `.pdb`, `.lib` nem binários privados. A compilação não requer os 30 GPKs.
2. **Para testar o jogo completo:** os **29 packs comerciais de sua própria
   licença** e `LxH.GPK` somente com autorização de acesso e de distribuição
   ao destinatário. Não colocar packs em commit, fork público ou PR.
3. **Para testar idiomas:** JSONs EN-LxH, ES e zh-CN e manifestos de revisão,
   compartilhados por canal privado apropriado; traduções PT-BR pessoais
   estão expressamente fora deste handoff.
4. **Para reproduzir a aparência atual:** fornecer o PNG HD
   `homebg_waifu2x_art_scan_noise2.png` como
   `assets/SchoolDaysHQ-Launcher.png`, o `.ico` derivado do desenho PB em
   `games/school-days/windows/SchoolDays.ico`, os recursos `display-options/`
   e fonte legalmente redistribuível. Usar só as artes autorizadas.
5. **Para validar a distribuição:** usar
   `games/school-days/tools/stage_clean_local_l4.ps1` com `-QuickDryRun` ou
   `-Create -ApprovedExeSha256 <hash>` em pasta NOVA, sem saves/config reais;
   os scripts falham se faltarem recursos obrigatórios ou ocorrer desvio hash.

## 4. Comportamento do launcher e da depuração

Um clique normal em `SchoolDays.exe` abre **PLAY / OPTIONS / QUIT**, sem
segundo executável. Options usa abas Display, Graphics, Audio, General,
Language e Advanced. PLAY inicia o jogo com as configurações salvas;
`--no-launcher` continua disponível para testes locais e headless.

O motor fornece MCP com `--mcp` para stdio e um socket de loopback habilitado
explicitamente. O mesmo conjunto de ferramentas e o mesmo escopo de arquivos
são utilizados pelos transportes; o antigo protocolo `SchoolDaysDevBridge`,
porta fixa 47321 e servidores Python próprios do School Days foram removidos.
Se não for necessário controle remoto, **não ativar** MCP no runtime.

## 5. Regras de integridade

- Nenhum programa deve alterar a instalação comercial, executável retail,
  `Script.GPK` ou outros 29 arquivos originais.
- Fazer QA de saves sempre com `--documents` isolado.
- Não incluir PT-BR privado, capturas de tela de QA, dados pessoais, arquivos
  de preferências `settings.json`/`Config.DAT`/`Save/` no Git.
- Sem pull request nesta entrega. O commit deste código-fonte é local; enviar
  ao fork requer `git push` separado e autorização específica.
