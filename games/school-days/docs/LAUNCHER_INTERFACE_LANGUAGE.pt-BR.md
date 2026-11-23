# School Days HQ — Idioma da interface da launcher

As launchers do **School Days HQ** no Windows e Android oferecem cinco idiomas
de interface: `en`, `pt-BR`, `es`, `ru` e `zh-CN`. O seletor aparece
imediatamente abaixo do controle de música, na tela inicial. Os idiomas são
mostrados por seus nomes nativos, independentemente da língua atual.

**O idioma da launcher não seleciona a tradução do jogo.** A seleção de idioma
e backend GPK/JSON, a preferência/disponibilidade de L×H, os saves, a pasta
externa SAF e a localização das legendas forçadas continuam independentes.

## Persistência e padrão

- **Windows:** `launcher-language.txt` fica ao lado das configurações da
  launcher. `SchoolDaysLauncherLocale::ReadPreference/WritePreference` gerenciam
  o arquivo sem editar `language.txt`, `translation-pack.txt` ou
  `Kotonoha.ini`.
- **Android:** as preferências da Activity `SchoolDaysSetup` armazenam
  apenas a escolha visual em `launcherUiLanguage`. A Activity aplica o idioma
  em um Context localizado e recria a própria interface. O idioma do jogo
  continua na configuração anterior `languageSelection`.
- **Instalações existentes:** sem preferência explícita, preservar a antiga
  interface em inglês.
- **Instalações novas:** usar o idioma do sistema quando estiver entre os cinco,
  ou inglês quando não estiver.

## Fontes das traduções e validação

- Windows/ImGui: `games/school-days/src/SchoolDaysLauncherLocale.cpp`, com
  tabela completa das chaves de localização da interface.
- Android: `buildsystems/android/app/src/schoolDays/res/values*/strings.xml`.
- Executar `SchoolDays.LauncherLocale` via CTest para testar a tabela da
  launcher Windows e a persistência. Executar
  `python tools/android/test_school_days_launcher_strings.py` para conferir
  chaves e parâmetros das traduções Android.

São textos da interface, não traduções dos diálogos do jogo. Qualquer nova
opção precisa de texto nos cinco idiomas. A funcionalidade não altera as
legendas `forced_subtitles` v4 aprovadas.

**Cuidado antes da entrega:** compilação e cobertura de strings não substituem
QA visual. Confirmar fallback de fontes, cortes de texto, persistência e
independência do idioma do jogo em Windows e Android antes de substituir os
artefatos v1.00 já aprovados.
