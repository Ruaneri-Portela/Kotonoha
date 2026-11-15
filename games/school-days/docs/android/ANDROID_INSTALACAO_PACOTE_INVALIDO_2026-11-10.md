# School Days HQ Android — correção de "pacote inválido" (10/11/2026)

## Diagnóstico

O APK anteriormente entregue como
`app-schoolDays-dist-unsigned.apk` (ou `...DIST-UNSIGNED...`) é um
artefato **sem assinatura**. A verificação `apksigner verify` falha com
`DOES NOT VERIFY` / `Missing META-INF/MANIFEST.MF`. Esse APK não pode
ser instalado no Android. O erro não prova defeito de GPKs, saves ou
instalador Java.

O APK `schoolDaysDev` anterior **é assinado** (Android Debug APK Signature
Scheme v2); sua instalação ainda depende de Android 9+ e CPU arm64-v8a,
espaço disponível, pacote transferido integralmente e permissões do aparelho.

## Variante de teste instalável, separada do produto final

O Gradle agora oferece:

- `:app:assembleSchoolDaysDev` — assinatura debug, pacote
  `me.hirameki.kotonoha.schooldays.dev`, com oito fixtures L×H apenas
  na DEV.
- `:app:assembleSchoolDaysDist` — pacote de produção
  `me.hirameki.kotonoha.schooldays`, **unsigned** deliberadamente até
  ser assinado com a chave release do proprietário.
- `:app:assembleSchoolDaysDistTest` — **mesmo código/flags de distribuição**,
  minificação e sem arquivos QA, mas assinatura Android Debug local e
  `applicationIdSuffix='.test'`. Pacote final de teste:
  `me.hirameki.kotonoha.schooldays.test`. É instalável **para
  teste**, não constitui release comercial nem usa uma chave comercial.

O Gradle verifica `KOTONOHA_DISTRIBUTION_BUILD=ON` e
`KOTONOHA_DEV_CONTROL=OFF` na variante `distTest`; nenhuma ferramenta
DEV é compilada. Esse uso da chave de teste **não** torna a aplicação
debuggable: o Manifest do APK não contém `android:debuggable=true`.

APK transferido para Downloads:

`C:\Users\Suzu\Downloads\SchoolDaysHQ-Android-ARM64-DIST-TESTE-INSTALAVEL-2026-11-10.apk`

- Tamanho: **24.277.491 bytes**
- SHA-256: `A05615B7765A60FF0A8BC0757DBDB196FF405F33ECCBE1E055C6CF194B670C3C`
- `apksigner verify --verbose`: **Verifies**, esquema v2
- `zipalign -c -P 16 -v 4`: **Verification successful** (conferir novamente após novas builds)
- `aapt dump badging`: minSdk **28**, targetSdk **34**, ABI **arm64-v8a**
- Verificação do ZIP: **14/14 traduções** GPK/JSON iguais à fonte; **0** saves
  de teste; **0** GPKs retail; **0** classe Java de importação DEV;
  **6** bibliotecas nativas ARM64 e a arte da launcher presente.

## Se o aparelho ainda rejeitar o APK assinado

1. Transferir o novo arquivo **.apk completo**, sem ZIP, renomear ou
   extrair. Instalar diretamente do app Arquivos/Downloads do Android.
2. Confirmar se o aparelho é Android 9 ou superior e **arm64-v8a**.
3. Autorizar no Android a instalação de aplicativos de fontes externas
   **para o app usado para abrir o arquivo**, se o sistema solicitar.
4. Não tentar instalar o APK `DIST-UNSIGNED`.
5. Se houver uma instalação prévia do **mesmo** pacote com outra assinatura,
   o Android pode retornar `INSTALL_FAILED_UPDATE_INCOMPATIBLE`.
   **Não desinstale às cegas**: isso apagaria dados/saves desse pacote.
   Faça backup e confirme o ID do pacote antes de decidir como proceder.
6. Caso falhe mesmo assim, registrar a **mensagem completa do instalador**,
   modelo do celular, versão Android, nome exato do APK e, se disponível,
   `adb install ...apk` para revelar o código de erro (por exemplo,
   `NO_MATCHING_ABIS` ou `OLDER_SDK`).

Nenhum celular foi detectado pelo `adb devices` durante este diagnóstico;
não se deve declarar o teste de instalação física como realizado.

**Importante:** `distTest` é descartável; seus saves são isolados do
pacote comercial. O APK comercial verdadeiro só será distribuído depois
da assinatura release definitiva. Não publique nem exponha a chave local
de teste ou um keystore comercial em Git.
