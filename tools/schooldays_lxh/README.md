# School Days HQ L×H — instalação local na L4 (Windows)

Este procedimento **não distribui** o patch L×H. Requer a pasta privada
montada pelo próprio usuário. Não altera EXE comercial, GPKs, saves, DLLs nem
qualquer recurso de terceiros no repositório.

## Perfil usado

- Executável: `build/school-days-dev-control4/games/school-days/Release/SchoolDays.exe`.
- Pasta de overlay padrão: `<Release>/display-options`, reconhecida pela
  `SchoolDaysAssets.cpp::SelectDisplayOptionsOverlay`.
- Conteúdo L×H ON: sete ORS em `Script/English/05`, sete cartões PNG e
  sete pares WMV/OGG em `LxH`. Esse conteúdo fica **somente na pasta build**.
- GPK original inglês preservado: os arquivos das sete cenas são substituídos
  em memória pelo Asset Overlay. Os outros finais seguem com seu ORS original.
- As opções gráficas da L4 em `System/Option` permanecem inalteradas.

## Etapa 1: controle L×H ON/OFF persistente (sem remover arquivos)

A **mesma SchoolDays.exe da L4** aceita `--lxh on`, `--lxh off` ou
`--lxh auto`. Feche o jogo antes de trocar o modo; a seleção é fixada no
início da sessão, para não trocar roteiros durante um final ou save.

```powershell
$release = 'D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release'
& "$release/SchoolDays.exe" --lxh off   # HQ original, sem os epílogos
& "$release/SchoolDays.exe" --lxh on    # HQ + L×H, exige os 28 arquivos
& "$release/SchoolDays.exe" --lxh auto  # detecta o pacote completo
```

A escolha é guardada em `LxH.mode` na raiz do perfil de documentos (por
padrão o próprio `<Release>` no Windows; ao passar `--documents`, fica nesse
diretório isolado). A próxima execução sem `--lxh` lê a escolha anterior.
`--no-config` usa o modo solicitado **somente para aquela execução**.
Se `LxH.mode` não existir, vale `auto`. Com `off`, os arquivos continuam na
pasta privada, mas **os sete ORS voltam a ser lidos do GPK original** e as
mídias opcionais ficam inacessíveis pelo resolver. Com `on`, um pacote
incompleto causa erro explícito em vez de começar um epílogo incompleto.

Esta etapa implementa o controle de runtime e persistência. **O botão
gráfico no menu Options ainda será integrado numa etapa posterior**, junto
com as correções de Display Options e saves L×H.

## Instalar ou retirar fisicamente o overlay privado

Executar na raiz do projeto com Python:

```powershell
$release = 'D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release'
$patch   = 'D:/Dev/_temp_lxh_static_audit_worker8/private-seven-en-ui-on-20261106'
python tools/schooldays_lxh/install_local_l4.py --release $release --enable $patch
python tools/schooldays_lxh/install_local_l4.py --release $release --status
python tools/schooldays_lxh/install_local_l4.py --release $release --disable
```

Quando ON, basta executar o SchoolDays.exe **da L4** normalmente, sem passar
`--asset-overlay`. Se `--asset-overlay` for fornecido explicitamente, esse
caminho terá prioridade e o overlay padrão da L4 não será carregado.

O utilitário verifica SHA-256 de cada recurso e grava um manifesto local para
remover somente seus 28 arquivos no comando `--disable`. **Não é preciso
executar `--disable` para desligar a DLC; prefira `SchoolDays.exe --lxh off`.**
Se algum arquivo de destino já
existir e não pertencer ao manifesto, não o sobrescreve.

Os sete saves originais de QA estão em `<Release>/Save`; executar sem
`--documents` usa esse perfil. Para automação/QA, apontar `--documents`
para cópia isolada do mesmo perfil, para não alterar os saves de teste.

O teste de leitura do patch fornecido localmente pode ser executado via
`SchoolDaysLxhPrivateMountProbe.exe` na pasta
`build/school-days-dev-control4/games/school-days/tests/Release`, com
argumentos `RETAIL_DIR OFF_OVERLAY ON_OVERLAY`. Não registrado no CTest
porque depende dos ativos licenciados do usuário.

**Limites:** a prova de montagem 7/7 não substitui QA de jogador completo,
sincronização de áudio/vídeo, timeouts ou tradução dos 273 textos novos.
Não iniciar trabalho Android antes de fechar a versão Windows.
