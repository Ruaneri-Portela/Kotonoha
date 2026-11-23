# Idioma da interface da launcher

[English (US)](LAUNCHER_INTERFACE_LANGUAGE.md) | **Português (Brasil)**

A launcher compartilhada para desktop agora tem um seletor **Idioma da
interface** para inglês, português brasileiro, espanhol, russo e chinês
simplificado (`en`, `pt-BR`, `es`, `ru`, `zh-CN`). Ele aparece acima do botão
Jogar, e as opções sempre usam os nomes nativos de cada idioma.

A escolha é independente da aba **Tradução**: alterar o idioma da launcher
não modifica `settings.json`, o dicionário de traduções do jogo, os pacotes
de idioma, os saves, as configurações de renderização ou os caminhos do jogo.
O motor e o jogo continuam usando as mesmas configurações de execução.

A preferência fica ao lado das configurações da launcher em
`launcher-language.txt`. Instalações existentes sem esse arquivo conservam
a interface anterior em inglês. Instalações novas usam um idioma suportado
do sistema, ou inglês quando o idioma é desconhecido. `zh-TW` e `zh-Hant`
não selecionam chinês simplificado automaticamente. Valores inválidos também
voltam para inglês.

As etiquetas comuns da launcher e as etiquetas de pasta/rota do School Days
foram traduzidas; dicas específicas de jogos, diagnósticos dinâmicos, nomes
técnicos e alguns painéis ImGui opcionais continuam com textos em inglês.
A fonte padrão da interface (`NotoSansJP-Regular.ttf`) cobre CJK; ao escolher
uma fonte personalizada, utilize uma que cubra o alfabeto selecionado.

Para validar a persistência independente, os códigos de idioma e a tabela
de traduções, execute `ctest -R '^Kotonoha.LauncherLocale$'` após compilar
os testes. Não é necessário nenhum arquivo comercial.

No Android, a launcher **unificada** do Kotonoha (não a tela específica de
um jogo) oferece os mesmos cinco idiomas por um seletor. A preferência da
interface é salva separadamente em `kotonoha_launcher_ui`, chave
`interface_language`, sem modificar o idioma do jogo, as permissões SAF ou
os saves. A troca recria a Activity sem apagar opções de jogos. O padrão
segue um idioma suportado do aparelho; chinês tradicional volta para inglês.
Valide os recursos Android com
`python buildsystems/android/tools/test_launcher_locales.py -v`.
