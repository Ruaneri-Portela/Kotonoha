# Gerador privado de GPKs L×H por idioma

O script **build_lxh_locale_gpks.py** converte os sete ORS estendidos do
LxH.GPK integrado, usando os sidecars L×H com 273 entradas.
Ele preserva falante, cronologia, todos os comandos e seus ticks; substitui
apenas o conteúdo textual de PrintText a partir do evento PlayMovie L×H.
Os ordinais seguem a ordenação do parser nativo: todos os comandos por
startTick, mantendo a ordem original para eventos no mesmo tick.

O LxH.GPK contém também o prefixo HQ. Para EN, esse prefixo vem
**exclusivamente do Packs/Script.GPK original**, identificado por SHA-256
E82DC824449381B9A132AA092740C4169EB3D77A820D969046B74CA1D9DF4ADF.
O gerador rejeita Script - english.GPK de translations.
Para PT-BR, o prefixo precisa vir do Script - Brazilian.GPK com 1859 entradas
e as sete cenas compatíveis. A integração não deve exibir falas HQ inglesas
quando PT-BR está selecionado.

Exemplo de execução a partir da raiz do repositório:

    python games/school-days/tools/build_lxh_locale_gpks.py --lxh-gpk "build/school-days-dev-control4/games/school-days/Release/Packs/LxH.GPK" --sidecars "build/school-days-dev-control4/games/school-days/Release/translations" --base-gpk "en=build/school-days-dev-control4/games/school-days/Release/Packs/Script.GPK" --base-gpk "pt-BR=build/school-days-dev-control4/games/school-days/Release/translations/Script - Brazilian.GPK" --locales en,pt-BR

O conversor apenas lê as fontes (em memória), sem extrair ou alterar nenhum
pack retail. Todos os arquivos novos ficam sob
tools/qa-lab/state/lxh-locales/run-ID/, com nomes en-lxh.GPK,
pt-BR-lxh.GPK e, se houver HQ espanhol compatível, es-lxh.GPK.
Cada GPK tem somente sete ORS, sem PNG, WMV ou OGG. O manifesto privado
registra SHA-256, tamanho, total de falas e status private-not-installed.
O CLI não permite selecionar um diretório de saída fora de QA state nem
sobrescrever um pack existente. Nenhum arquivo da Release ou save é escrito.

ES: o es-lxh.json original contém sete créditos Juan Ochoa; só os campos
de sentinela correspondentes são normalizados para U+200B na saída GPK,
sem qualquer modificação no JSON. Um es-lxh.GPK legítimo exige um Script.GPK
base em espanhol com HQ nas sete cenas e formato ORS compatível, passando
--base-gpk es=CAMINHO ou --base-overlay es=PASTA. Esse GPK base não foi
encontrado na Release examinada; gerar apenas o sufixo espanhol com HQ
em inglês seria incorreto.

ZH-CN: o GPK chinês observado usa GB18030, nomes sem subpasta English e
ORS entre aspas com campos separados por vírgula, diferentes do parser L4
de campos TAB. O gerador bloqueia zh-CN deliberadamente, mesmo com as
273 entradas JSON presentes. Antes de habilitá-lo, é necessária uma
conversão própria comprovada de todas as 1859 cenas, com chaves, timing,
charset, semântica e runtime testados.

Testes com ORS e GPKs sintéticos, sem modificar assets licenciados:

    python -m unittest discover -s games/school-days/tools -p test_build_lxh_locale_gpks.py -v

Esses testes não substituem testes funcionais do jogo. A auditoria read-only
contra o LxH.GPK privado também verificou as 273 chaves de evento exatas.
