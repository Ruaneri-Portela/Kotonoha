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

ZH-CN: o Script - chinese.GPK original tem GB18030, eventos em ordem
diferente e ORS incompatível; NUNCA o monte diretamente. A ferramenta
build_chinese_compatible_gpk.py reconstrói um Script.GPK compatível usando
somente comandos, mídia e ticks do retail autenticado (Packs/Script.GPK),
diálogos completos de zh-CN.json e a linha adicional conferida no patch
chinês original. Falha se faltar qualquer texto, opção ou validação estrutural.
O resultado Script - chinese-compatible.GPK fica apenas em
tools/qa-lab/state/chinese-compatible/run-ID/, sem instalação automática:

    python -B games/school-days/tools/build_chinese_compatible_gpk.py --retail-gpk "Packs/Script.GPK" --chinese-gpk "Script - chinese.GPK" --zh-json "zh-CN.json" --ktrf "school-days-hq.ktnroute" --build

O comando acima é ilustrativo e exige caminhos privados existentes; não é
executado nos testes automatizados. Para os sete ORS LxH chineses, use
build_lxh_locale_gpks.py --locales zh-CN --base-gpk
zh-CN="Script - chinese-compatible.GPK" com sidecars completos. A montagem
nativa requer AMBOS os GPKs certificados e bloqueia qualquer mistura com
a tradução bruta ou fallback inglês.

RUSSO: o par Script - Russian.GPK + ru-lxh.GPK é obrigatório para
a montagem LxH. A única exceção estrutural é uma legenda Taisuke extra
sobre PlayVoice em 03-KB-D10 com ticks 403-577; todo comando e tempo retail
restante continua validado. Sem LxH, apenas o perfil explicitamente
RETAIL_ONLY aceita o Script.GPK russo isolado.

Testes com ORS e GPKs sintéticos, sem modificar assets licenciados:

    python -m unittest discover -s games/school-days/tools -p test_build_lxh_locale_gpks.py -v

Esses testes não substituem testes funcionais do jogo. A auditoria read-only
contra o LxH.GPK privado também verificou as 273 chaves de evento exatas.
