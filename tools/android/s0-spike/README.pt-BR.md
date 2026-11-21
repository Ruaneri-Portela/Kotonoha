# Android S0 — spike isolado de FD/SAF sintético

Este é um **APK de teste independente**, não o APK do jogo School Days.
Não depende do motor Kotonoha, CMake, NDK, bibliotecas nativas, GPKs retail,
saves ou build Android A1. ID do aplicativo:
`org.kotonoha.schooldays.safspike`.

## Fontes / requisitos

- `app/src/main/java/org/kotonoha/schooldays/safspike/S0StorageReadProbeActivity.java`
- `app/src/main/AndroidManifest.xml`
- AGP **8.13.2**, Gradle **8.14.6** (ou compatível), JDK **17**, Android
  compile SDK **36**, API mínima **30**, API alvo **34**.
- Sem permissões amplas de armazenamento nem dependências. Fora do arquivo
  sintético privado, só usa SAF ou um registro MediaStore criado pelo próprio
  aplicativo de teste.

## Build e execução manual — para o prime (não executados nesta tarefa)

Na pasta `tools/android/s0-spike/`, com Gradle e Android SDK disponíveis:

```powershell
gradle :app:assembleDebug --no-daemon
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.kotonoha.schooldays.safspike/.S0StorageReadProbeActivity
adb logcat -s KTN_S0:I '*:S'
```

O APK isolado possui entrada própria no launcher, mas não adiciona nenhuma
Activity nem atalho ao APK School Days. Os dois arquivos provisórios
criados na variante do jogo foram retirados antes desta entrega.

## Procedimento manual

1. **SAF:** toque **Start synthetic SAF probe**, escolha o destino por
   `ACTION_CREATE_DOCUMENT` e deixe criar 1 GiB sintético no provedor e uma
   referência privada idêntica de 1 GiB. Quando abrir `ACTION_OPEN_DOCUMENT`,
   escolha **somente o documento recém-criado**. Outro `content://` document
   ID é rejeitado antes da abertura. O seletor do sistema escolhe **arquivo,
   não pasta/árvore**: não selecione pasta do jogo, GPK nem documento pessoal.
   Não é necessário autorizar acesso a pastas inteiras.
2. O caminho SAF solicita e confirma a concessão de leitura persistente com
   `takePersistableUriPermission`, abre `ContentResolver.openFileDescriptor`,
   mede `Os.lseek` / `Os.pread`, tenta reabrir o FD por
   `/proc/self/fd/<number>` sem resolver ou registrar o destino do link e
   **não libera a concessão ao concluir**. URI e referência privada ficam
   salvos para reabertura após fechar o app ou reiniciar o celular.
3. **MediaStore:** como alternativa, toque **Start MediaStore synthetic probe**.
   Cria um item próprio em `Downloads/KotonohaS0` via `MediaStore.Downloads`,
   gera os mesmos bytes na referência privada e no documento, publica com
   `IS_PENDING=0` e lê diretamente o FD. Essa modalidade **não** verifica
   permissão persistente do SAF.
4. Cada modalidade faz **três comparações de arquivo inteiro**: blocos de
   1 MiB sequenciais, em ordem inversa e em saltos coprimos (todos os 1024
   offsets exatamente uma vez). A **tag única `KTN_S0`** registra uma medição
   do provedor (`mode=saf` ou `mode=mediastore`) e outra do arquivo privado
   (`mode=private`) por repetição. Campos: `test`, `mode`,
   `access=sequential|random`, `pattern=forward|reverse|strided`,
   `repetition=1|2|3`, `block_bytes`, `blocks`, `bytes`, `equal`, `read_ms`,
   `throughput_mib_s`, `lat_p50_ms`, `lat_p95_ms`. Os percentis usam posição
   mais próxima na distribuição das 1024 leituras individuais de blocos de
   1 MiB por modo/repetição. A tag também registra concessão SAF, suporte a
   `/proc/self/fd` e códigos de resultado. Cada medição original recebe um
   `metric_id` aleatório e `historic=false` (sem dados de usuário/aparelho).
5. Depois das três rodadas, **não toque Delete**. Feche e reabra o app e use
   **Retest saved synthetic document (after app/reboot)**: repete três rodadas
   nos mesmos arquivos, sem regeneração/cópia. Reinicie o celular, reabra o
   app e repita **Retest**. No SAF, o botão verifica se a permissão persistiu
   após o reboot; se não persistiu, registra
   `persisted_read_grant_on_retest=false` na tag `KTN_S0` e exige escolher *somente* o
   documento sintético original. Execute SAF e MediaStore separadamente: o
   projeto mantém uma sessão sintética por vez.
6. **Reiniciar o POCO apaga logcat, mas não o histórico S0.** Ao abrir o app,
   as medições salvas reaparecem na tag `KTN_S0` com
   `historic=true replay=on_open` e o **mesmo `metric_id`** da linha original
   (`historic=false replay=none`). Se o logcat não estava sendo coletado ao
   abrir o app, toque **Replay saved metrics to KTN_S0 logcat** para emitir
   `historic=true replay=manual`. **Ao consolidar os três ciclos, mantenha
   apenas um registro por `metric_id`**, não conte original + replay como
   medições diferentes. O replay não executa benchmark nem acessa GPKs.

Logs contêm apenas métricas e códigos fixos de falha, nunca URI, caminhos,
nomes de documentos, bytes ou dados pessoais. Nenhum GPK/save é aberto,
copiado ou modificado. **Não selecione arquivos reais.**

## Espaço e limpeza (inclusive celulares de 32 GB)

- A criação só acontece após o toque. Antes de escrever, exige pelo menos
  **2,5 GiB livres no armazenamento interno**. A referência privada e o
  documento do provedor podem ocupar 1 GiB cada no mesmo volume. O provedor
  também pode exigir cache temporário. Ter 32 GB de capacidade nominal não
  garante espaço livre suficiente.
- Geração em blocos de 1 MiB, comparação com dois buffers de 1 MiB, sem
  carregar 1 GiB inteiro em RAM. Espaço insuficiente interrompe o teste sem
  reduzir silenciosamente seu tamanho.
- Histórico de **somente métricas** em SharedPreferences privado, gravado
  sincronamente antes de cada linha ao vivo. Limites: **96 registros,
  32.768 caracteres totais e 384 caracteres por linha**. Descarta primeiro
  os registros mais antigos. Validação de formato fixo rejeita URI, caminhos,
  nomes, conteúdo de arquivo e outros logs. Três ciclos completos de um modo
  normalmente produzem 18 linhas (2 origens × 3 padrões × 3 ciclos).
- **O sucesso não apaga arquivos nem libera a permissão SAF persistente.**
  Somente o botão **Delete MY synthetic test files** tenta apagar o documento
  criado pelo teste com `DocumentsContract.deleteDocument` (SAF) ou
  `ContentResolver.delete` (MediaStore), depois a referência privada e a
  permissão guardada. **Delete também apaga o histórico de métricas**, mesmo
  se o provedor recusar a remoção. Colete os três ciclos e replay antes de
  usar Delete. Alguns provedores SAF negam a exclusão após reboot,
  porque a permissão persistida é só de leitura; nesse caso apague o arquivo
  sintético pelo gerenciador Arquivos e tente novamente ou limpe os dados do
  aplicativo de teste. Um documento selecionado separadamente nunca é apagado.

## Interpretação / limites

- `openFileDescriptor` + `pread/lseek` confirmados demonstram leitura aleatória
  **naquele provedor/aparelho**. Outros provedores podem entregar pipes/FDs
  proxy (`ESPIPE`), arquivos na nuvem ou cópias temporárias: não é prova de
  zero-copy físico nem integração com os providers C++ de GPK. Nenhuma injeção
  de FD em código nativo foi implementada.
- Reabrir `/proc/self/fd` é teste adicional separado: pode falhar apesar de
  `pread` direto funcionar. Não presumir que FD Java vira caminho nativo.
- A permissão persistente é confirmada na execução e no reteste após fechar
  o app/reboot; a persistência real precisa ser verificada no aparelho.
  Nuvem, offsets 32-bit de GPK nativo, térmica e gameplay exigem QA separado.
- Este worker não compilou, instalou nem executou testes em emulador/celular.
