# Android School Days — Etapa 4, GPK/JSON embutidos

Fonte externa **não versionada** usada nesta verificação:

`C:\Users\Suzu\Downloads\SchoolDaysHQ-Android-Complementos-2026-11-09\SchoolDaysHQ\translations`

Construção (exemplo PowerShell), sem copiar traduções para o repositório:

```powershell
Set-Location D:\Dev\Kotonoha-v3\Android
$env:VCPKG_ROOT = 'D:\vcpkg-kotonoha'
& 'D:\Gradle\gradle-8.14.5\bin\gradle.bat' :app:assembleSchoolDaysDev -Pabis=arm64-v8a -PbundleGameAssets=false '-PschoolDaysTranslationsDir=C:\Users\Suzu\Downloads\SchoolDaysHQ-Android-Complementos-2026-11-09\SchoolDaysHQ\translations' --no-daemon --console=plain
```

Sem `-PschoolDaysTranslationsDir`, ou se faltarem os GPK/JSON mínimos EN/PT-BR/ES, a task de empacotamento lança erro em vez de produzir um APK com idioma silenciosamente ausente.

O Gradle inclui **somente** `translations/jsons/*.json` e os nomes explicitamente autorizados de GPKs traduzidos (nunca `Packs/*.GPK`, `Script.GPK` retail, saves ou configs), gerando `assets/translations-manifest.sha256` com um hash por arquivo. A inspeção do APK DEV confirmou 14 traduções: 5 GPK e 9 JSON, inclusive `-lxh` e `-reviewed-identities`; um manifesto de 15 linhas (cabeçalho + 14 entradas). O ZIP continha zero `Packs/Script.GPK`.

Na inicialização, `SchoolDaysEmbeddedTranslations` valida o manifesto e todos os SHA256, copiando os idiomas para `<getExternalFilesDir(null)>/Kotonoha/<applicationId>/translations/{scripts,jsons}` (com fallback privado). Assim, os mesmos diretórios são lidos pelo runtime C++ e pelo seletor Android.

Atualização conservadora:

1. Arquivo ausente: copia asset verificado usando staging na pasta de destino e sincronização.
2. Arquivo idêntico: mantém bytes originais.
3. Arquivo antigo anteriormente instalado pelo **mesmo gerenciador** e ainda idêntico ao hash gerenciado: pode atualizar para o asset novo, **exceto PT-BR**.
4. Qualquer PT-BR já existente, qualquer importação própria anterior ou arquivo editado: preserva bytes sem sobreposição; o seletor e runtime permanecem sujeitos à validação estrita, **sem fallback para inglês**.
5. O manifesto privado armazena apenas hashes de arquivos realmente gerenciados, não reivindica propriedade de cópias do usuário. Resumo de instalados/preservados aparece na launcher.

**Limitações explícitas**: não foi executado em telefone/emulador; não é prova de validação semântica do conteúdo de todos os GPK/JSON. O runtime C++ ainda tem de rejeitar ORS GPK incompatíveis e cobertura incompleta dos JSONs. As traduções PT-BR originais e o pacote de complementos em Downloads permaneceram intocados.
