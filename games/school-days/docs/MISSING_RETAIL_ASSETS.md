# Assets citados pelos roteiros e ausentes nos GPK originais

Inventário da instalação em `D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ\Packs`, em 04/11/2026.
A verificação leu os índices dos 29 GPK e os 1.857 arquivos ORS. São 69.936
entradas nos pacotes, sem colisões de nome, e 146 nomes distintos citados
pelos roteiros sem entrada com a grafia exata: 6 introduções BGM (386 citações),
139 vozes (149 citações) e 1 imagem de fundo (1 citação). Além do caso de
espaço indevido descrito abaixo, nenhum nome ausente corresponde a um arquivo
de mesmo nome-base em outro GPK.

Uma das 139 referências de voz é um erro de escrita no próprio ORS: o caminho
contém um espaço, mas a voz correspondente está no pacote. Portanto, depois da
correção descrita abaixo, restam **138 vozes e 1 fundo realmente indisponíveis**
nesta instalação. As seis introduções BGM são atendidas pelos loops presentes.

## BGM com apenas o loop no original

As faixas `sdbgm14`, `sdbgm18`, `sdbgm20`, `sdbgm28`, `sdbgm29` e
`sdbgm33` têm `_loop.ogg` em `BGM.GPK`, sem `_int.ogg`. A build de referência
tentava abrir a introdução e deixava o canal BGM silencioso. School Days agora
começa no loop existente e o repete até o fim do evento. Na cena
`01/01-00-A00`, o canal BGM passou de zero para um stream ativo.

## Recurso ABC presente recuperado

Na cena `00/00-00-A02`, o fundo
`Event00/00-00/00-00-A02/00-00-A02-001B.PNG` tem as três imagens
`001BMAK.A.PNG`, `001BMAK.B.PNG` e `001BMAK.C.PNG` em `Event00.GPK`.
A listagem genérica reduzia as referências GPK a uma entrada, e a versão
recompilada antes da correção informava zero grupos ABC. School Days agora
usa sua própria enumeração de irmãos; a mesma cena informa um grupo ABC
completo, como a build funcional de referência.

## Ausências reais nesta instalação

As 138 vozes restantes não têm arquivo com o mesmo nome-base em outro GPK. A imagem
`Event01/01-00/01-00-T00/01-00-T00-009.PNG` também está ausente e é citada
tanto no `Packs/Script.GPK` quanto no `Backup Packs Original/Script.GPK`.
O jogo ignora a voz indisponível e mantém o fundo anterior quando falta uma
imagem; não há conteúdo original correspondente para reconstruir por código.

O roteiro `Script/ENGLISH/03/03-BK-H00.ENG.ORS` pede
`Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG`, com um espaço entre `B` e `K`.
O pacote `Voice03.GPK` contém
`Voice03/03-BK/03-BK-H00/03-BK-H00-0410.OGG`. A resolução específica de
School Days usa esse arquivo quando a grafia do roteiro não existe, mantendo
prioridade para um eventual arquivo com a grafia exata em uma instalação
extraída ou overlay. Este é um recurso recuperado, não uma voz ausente.

`Event05.GPK` contém `05-KA/05-KA-B00/05-KA-B00-001BTAI.B.PNG` e
`05-KA/05-KA-B00/05-KA-B00-001BTAI.C.PNG`, mas não
`05-KA/05-KA-B00/05-KA-B00-001BTAI.A.PNG`. Esse grupo ABC incompleto
pertence ao fundo `Event05/05-KA/05-KA-B00/05-KA-B00-001B.PNG` na cena
`Script/ENGLISH/05/05-KA-B00.ENG.ORS`.

## Lista completa

| GPK esperado | Tipo | Caminho lógico citado | Citações | Roteiros de exemplo |
| --- | --- | --- | ---: | --- |
| `BGM.GPK` | `PlayBgm` | `BGM/SD_BGM/sdbgm14_int.ogg` | 12 | `Script/ENGLISH/01/01-1K-L00.ENG.ORS`<br>`Script/ENGLISH/03/03-B1-B00.ENG.ORS`<br>`Script/ENGLISH/03/03-B1-C00.ENG.ORS` |
| `BGM.GPK` | `PlayBgm` | `BGM/SD_BGM/sdbgm18_int.ogg` | 20 | `Script/ENGLISH/02/02-2K-I02.ENG.ORS`<br>`Script/ENGLISH/03/03-A1-G00.ENG.ORS`<br>`Script/ENGLISH/03/03-BK-K01.ENG.ORS` |
| `BGM.GPK` | `PlayBgm` | `BGM/SD_BGM/sdbgm20_int.ogg` | 218 | `Script/ENGLISH/01/01-00-A00.ENG.ORS`<br>`Script/ENGLISH/01/01-00-G00.ENG.ORS`<br>`Script/ENGLISH/01/01-00-G01.ENG.ORS` |
| `BGM.GPK` | `PlayBgm` | `BGM/SD_BGM/sdbgm28_int.ogg` | 41 | `Script/ENGLISH/01/01-1S-H00.ENG.ORS`<br>`Script/ENGLISH/03/03-A1-D09.ENG.ORS`<br>`Script/ENGLISH/03/03-A1-D10.ENG.ORS` |
| `BGM.GPK` | `PlayBgm` | `BGM/SD_BGM/sdbgm29_int.ogg` | 69 | `Script/ENGLISH/01/01-1K-E04.ENG.ORS`<br>`Script/ENGLISH/02/02-2S-M00.ENG.ORS`<br>`Script/ENGLISH/02/02-2S-M01.ENG.ORS` |
| `BGM.GPK` | `PlayBgm` | `BGM/SD_BGM/sdbgm33_int.ogg` | 26 | `Script/ENGLISH/02/02-2K-Q03.ENG.ORS`<br>`Script/ENGLISH/02/02-2K-Q04.ENG.ORS`<br>`Script/ENGLISH/02/02-2K-Q05.ENG.ORS` |
| `Event01.GPK` | `CreateBG` | `Event01/01-00/01-00-T00/01-00-T00-009.PNG` | 1 | `Script/ENGLISH/01/01-00-T00.ENG.ORS` |
| `Voice00.GPK` | `PlayVoice` | `Voice00/00-00/00-00-L00/00-00-L00-0570.OGG` | 1 | `Script/ENGLISH/00/00-00-L00.ENG.ORS` |
| `Voice00.GPK` | `PlayVoice` | `Voice00/00-00/00-00-L00/00-00-L00-0640.OGG` | 1 | `Script/ENGLISH/00/00-00-L00.ENG.ORS` |
| `Voice01.GPK` | `PlayVoice` | `Voice01/01-00/01-00-M03/01-00-M03-0210.OGG` | 1 | `Script/ENGLISH/01/01-00-M03.ENG.ORS` |
| `Voice01.GPK` | `PlayVoice` | `Voice01/01-00/01-00-M05/01-00-M05-0150.OGG` | 1 | `Script/ENGLISH/01/01-00-M05.ENG.ORS` |
| `Voice01.GPK` | `PlayVoice` | `Voice01/01-00/01-00-N10/01-00-N10-0070.OGG` | 1 | `Script/ENGLISH/01/01-00-N10.ENG.ORS` |
| `Voice01.GPK` | `PlayVoice` | `Voice01/01-1K/01-1K-E03/01-1K-E03-0580.OGG` | 1 | `Script/ENGLISH/01/01-1K-E03.ENG.ORS` |
| `Voice01.GPK` | `PlayVoice` | `Voice01/01-1K/01-1K-E04/01-1K-E04-0270.OGG` | 1 | `Script/ENGLISH/01/01-1K-E04.ENG.ORS` |
| `Voice01.GPK` | `PlayVoice` | `Voice01/01-1S/01-1S-O00/01-1S-O00-0200.OGG` | 1 | `Script/ENGLISH/01/01-1S-O00.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2K/02-2K-C00/02-2K-C00-0150.OGG` | 1 | `Script/ENGLISH/02/02-2K-C00.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2K/02-2K-C00/02-2K-C00-0210.OGG` | 1 | `Script/ENGLISH/02/02-2K-C00.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2K/02-2K-J03/02-2K-J03-0150.OGG` | 1 | `Script/ENGLISH/02/02-2K-J03.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2K/02-2K-Q03/02-2K-Q03-1000.OGG` | 1 | `Script/ENGLISH/02/02-2K-Q03.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2K/02-2K-Q04/02-2K-Q04-0130.OGG` | 1 | `Script/ENGLISH/02/02-2K-Q04.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2K/02-2K-Q04/02-2K-Q04-0530.OGG` | 1 | `Script/ENGLISH/02/02-2K-Q04.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2S/02-2S-J01/02-2S-J01-0040.OGG` | 1 | `Script/ENGLISH/02/02-2S-J01.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2S/02-2S-J01/02-2S-J01-0170.OGG` | 1 | `Script/ENGLISH/02/02-2S-J01.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2S/02-2S-M02/02-2S-M02-0050.OGG` | 1 | `Script/ENGLISH/02/02-2S-M02.ENG.ORS` |
| `Voice02.GPK` | `PlayVoice` | `Voice02/02-2S/02-2S-M03/02-2S-M03-0100.OGG` | 1 | `Script/ENGLISH/02/02-2S-M03.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-A0/03-A0-A00/03-A0-A00-0160.OGG` | 1 | `Script/ENGLISH/03/03-A0-A00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-A1/03-A1-E04/03-A1-E04-0080.OGG` | 1 | `Script/ENGLISH/03/03-A1-E04.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-A1/03-A1-E08/03-A1-E08-0840.OGG` | 1 | `Script/ENGLISH/03/03-A1-E08.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-A1/03-A1-E08/03-A1-E08-1350.OGG` | 1 | `Script/ENGLISH/03/03-A1-E08.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-A1/03-A1-Z01/03-A1-Z01-0020.OGG` | 1 | `Script/ENGLISH/03/03-A1-Z01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-B2/03-B2-A02/03-B2-A02-0160.OGG` | 1 | `Script/ENGLISH/03/03-B2-A02.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-B01/03-BK-B01-0060.OGG` | 1 | `Script/ENGLISH/03/03-BK-B01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-B01/03-BK-B01-0150.OGG` | 1 | `Script/ENGLISH/03/03-BK-B01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-B01/03-BK-B01-0170.OGG` | 1 | `Script/ENGLISH/03/03-BK-B01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-B03/03-BK-B03-0200.OGG` | 1 | `Script/ENGLISH/03/03-BK-B03.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-B03/03-BK-B03-0250.OGG` | 1 | `Script/ENGLISH/03/03-BK-B03.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-B04/03-BK-B04-0070.OGG` | 1 | `Script/ENGLISH/03/03-BK-B04.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-C01/03-BK-C01-0030.OGG` | 1 | `Script/ENGLISH/03/03-BK-C01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-D05/03-BK-D05-0130.OGG` | 1 | `Script/ENGLISH/03/03-BK-D05.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-H00/03-B K-H00-0410.OGG` | 1 | `Script/ENGLISH/03/03-BK-H00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-I00/03-BK-I00-0190.OGG` | 1 | `Script/ENGLISH/03/03-BK-I00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-I01/03-BK-I01-0090.OGG` | 1 | `Script/ENGLISH/03/03-BK-I01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-I06/03-BK-I06-0220.OGG` | 1 | `Script/ENGLISH/03/03-BK-I06.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-K00/03-BK-K00-0430.OGG` | 1 | `Script/ENGLISH/03/03-BK-K00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-K02/03-BK-K02-0050.OGG` | 1 | `Script/ENGLISH/03/03-BK-K02.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-K03/03-BK-K03-0160.OGG` | 1 | `Script/ENGLISH/03/03-BK-K03.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-K03/03-BK-K03-0170.OGG` | 1 | `Script/ENGLISH/03/03-BK-K03.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-BK/03-BK-K05/03-BK-K05-0260.OGG` | 1 | `Script/ENGLISH/03/03-BK-K05.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KA/03-KA-C00/03-KA-C00-0230.OGG` | 1 | `Script/ENGLISH/03/03-KA-C00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KA/03-KA-K01/03-KA-K01-0110.OGG` | 1 | `Script/ENGLISH/03/03-KA-K01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KA/03-KA-N00/03-KA-N00-0060.OGG` | 1 | `Script/ENGLISH/03/03-KA-N00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-A05/03-KB-A05-0090.OGG` | 1 | `Script/ENGLISH/03/03-KB-A05.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-B00/03-KB-B00-0960.OGG` | 1 | `Script/ENGLISH/03/03-KB-B00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-B01/03-KB-B01-0080.OGG` | 1 | `Script/ENGLISH/03/03-KB-B01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-C05/03-KB-C05-0140.OGG` | 1 | `Script/ENGLISH/03/03-KB-C05.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-K07/03-KB-K07-0160.OGG` | 1 | `Script/ENGLISH/03/03-KB-K07.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-K10/03-KB-K10-0150.OGG` | 1 | `Script/ENGLISH/03/03-KB-K10.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-L00/03-KB-L00-0500.OGG` | 3 | `Script/ENGLISH/03/03-AK-C00.ENG.ORS`<br>`Script/ENGLISH/03/03-B1-A00.ENG.ORS`<br>`Script/ENGLISH/03/03-KB-L00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-M05/03-KB-M05-0040.OGG` | 1 | `Script/ENGLISH/03/03-KB-M05.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-O09/03-KB-O09-0180.OGG` | 1 | `Script/ENGLISH/03/03-KB-O09.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-O09/03-KB-O09-0280.OGG` | 1 | `Script/ENGLISH/03/03-KB-O09.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-KB/03-KB-O09/03-KB-O09-0390.OGG` | 1 | `Script/ENGLISH/03/03-KB-O09.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-S0/03-S0-A00/03-S0-A00-0550.OGG` | 1 | `Script/ENGLISH/03/03-S0-A00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-A00/03-SA-A00-0080.OGG` | 1 | `Script/ENGLISH/03/03-SA-A00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-B01/03-SA-B01-0050.OGG` | 1 | `Script/ENGLISH/03/03-SA-B01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-B07/03-SA-B07-0870.OGG` | 1 | `Script/ENGLISH/03/03-SA-B07.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-B07/03-SA-B07-0970.OGG` | 1 | `Script/ENGLISH/03/03-SA-B07.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-B10/03-SA-B10-0310.OGG` | 1 | `Script/ENGLISH/03/03-SA-B10.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-B10/03-SA-B10-0780.OGG` | 1 | `Script/ENGLISH/03/03-SA-B10.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-C03/03-SA-C03-0380.OGG` | 1 | `Script/ENGLISH/03/03-SA-C03.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-F01/03-SA-F01-0340.OGG` | 1 | `Script/ENGLISH/03/03-SA-F01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-G08/03-SA-G08-0030.OGG` | 1 | `Script/ENGLISH/03/03-SA-G08.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SA/03-SA-H01/03-SA-H01-0110.OGG` | 1 | `Script/ENGLISH/03/03-SA-H01.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SB/03-SB-A09/03-SB-A09-0220.OGG` | 1 | `Script/ENGLISH/03/03-SB-A09.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SB/03-SB-I03/03-SB-I03-1040.OGG` | 1 | `Script/ENGLISH/03/03-SB-I03.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SB/03-SB-J00/03-SB-J00-0140.OGG` | 1 | `Script/ENGLISH/03/03-SB-J00.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SB/03-SB-M02/03-SB-M02-0080.OGG` | 1 | `Script/ENGLISH/03/03-SB-M02.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SB/03-SB-M04/03-SB-M04-0060.OGG` | 1 | `Script/ENGLISH/03/03-SB-M04.ENG.ORS` |
| `Voice03.GPK` | `PlayVoice` | `Voice03/03-SB/03-SB-M05/03-SB-M05-0050.OGG` | 1 | `Script/ENGLISH/03/03-SB-M05.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-C0/04-C0-I04/04-C0-I04-0580.OGG` | 1 | `Script/ENGLISH/04/04-C0-I04.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-C1/04-C1-E00/04-C1-E00-0460.OGG` | 1 | `Script/ENGLISH/04/04-C1-E00.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-E0/04-E0-F00/04-E0-F00-0510.OGG` | 1 | `Script/ENGLISH/04/04-E0-F00.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KA/04-KA-B04/04-KA-B04-0070.OGG` | 1 | `Script/ENGLISH/04/04-KA-B04.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KA/04-KA-D05/04-KA-D05-0370.OGG` | 1 | `Script/ENGLISH/04/04-KA-D05.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KA/04-KA-I03/04-KA-I03-0240.OGG` | 1 | `Script/ENGLISH/04/04-KA-I03.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KB/04-KB-B08/04-KB-B08-0030.OGG` | 1 | `Script/ENGLISH/04/04-KB-B08.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KC/04-KC-B04/04-KC-B04-0240.OGG` | 1 | `Script/ENGLISH/04/04-KC-B04.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KC/04-KC-B08/04-KC-B08-0040.OGG` | 1 | `Script/ENGLISH/04/04-KC-B08.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KC/04-KC-E04/04-KC-E04-0210.OGG` | 1 | `Script/ENGLISH/04/04-KC-E04.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KC/04-KC-H02/04-KC-H02-0110.OGG` | 1 | `Script/ENGLISH/04/04-KC-H02.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KE/04-KE-B00/04-KE-B00-0450.OGG` | 1 | `Script/ENGLISH/04/04-KE-B00.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-KE/04-KE-C01/04-KE-C01-1180.OGG` | 1 | `Script/ENGLISH/04/04-KE-C01.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SA/04-SA-L03/04-SA-L03-0430.OGG` | 1 | `Script/ENGLISH/04/04-SA-L03.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SA/04-SA-L10/04-SA-L10-0150.OGG` | 1 | `Script/ENGLISH/04/04-SA-L10.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SA/04-SA-Q03/04-SA-Q03-0480.OGG` | 1 | `Script/ENGLISH/04/04-SA-Q03.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SA/04-SA-U00/04-SA-U00-0530.OGG` | 1 | `Script/ENGLISH/04/04-SA-U00.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SB/04-SB-C02/04-SB-C02-0040.OGG` | 1 | `Script/ENGLISH/04/04-SB-C02.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SC/04-SC-D00/04-SC-D00-0260.OGG` | 1 | `Script/ENGLISH/04/04-SC-D00.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SC/04-SC-D00/04-SC-D00-0670.OGG` | 1 | `Script/ENGLISH/04/04-SC-D00.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SC/04-SC-D01/04-SC-D01-0150.OGG` | 1 | `Script/ENGLISH/04/04-SC-D01.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SE/04-SE-D01/04-SE-D01-0450.OGG` | 1 | `Script/ENGLISH/04/04-SE-D01.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SE/04-SE-D02/04-SE-D02-0080.OGG` | 1 | `Script/ENGLISH/04/04-SE-D02.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SE/04-SE-D03/04-SE-D03-0150.OGG` | 1 | `Script/ENGLISH/04/04-SE-D03.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SE/04-SE-D04/04-SE-D04-0260.OGG` | 1 | `Script/ENGLISH/04/04-SE-D04.ENG.ORS` |
| `Voice04.GPK` | `PlayVoice` | `Voice04/04-SE/04-SE-E04/04-SE-E04-0490.OGG` | 1 | `Script/ENGLISH/04/04-SE-E04.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-5O/05-5O-A09/05-5O-A09-0720.OGG` | 1 | `Script/ENGLISH/05/05-5O-A09.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-5O/05-5O-G04/05-5O-G04-0130.OGG` | 1 | `Script/ENGLISH/05/05-5O-G04.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-5O/05-5O-I10/05-5O-I10-0050.OGG` | 1 | `Script/ENGLISH/05/05-5O-I10.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-5O/05-5O-I10/05-5O-I10-0070.OGG` | 1 | `Script/ENGLISH/05/05-5O-I10.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-5O/05-5O-J02/05-5O-J02-0050.OGG` | 1 | `Script/ENGLISH/05/05-5O-J02.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-5O/05-5O-L04/05-5O-L04-0110.OGG` | 1 | `Script/ENGLISH/05/05-5O-L04.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-5O/05-5O-N00/05-5O-N00-0260.OGG` | 1 | `Script/ENGLISH/05/05-5O-N00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-A2/05-A2-B00/05-A2-B00-0220.OGG` | 1 | `Script/ENGLISH/05/05-A2-B00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-A2/05-A2-Z00/05-A2-Z00-0220.OGG` | 1 | `Script/ENGLISH/05/05-A2-Z00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-B0/05-B0-D00/05-B0-D00-0340.OGG` | 1 | `Script/ENGLISH/05/05-B0-D00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-G0/05-G0-A00/05-G0-A00-0080.OGG` | 1 | `Script/ENGLISH/05/05-G0-A00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-G0/05-G0-E01/05-G0-E01-0100.OGG` | 1 | `Script/ENGLISH/05/05-G0-E01.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KA/05-KA-A05/05-KA-A05-0140.OGG` | 1 | `Script/ENGLISH/05/05-KA-A05.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KB/05-KB-B02/05-KB-B02-0220.OGG` | 4 | `Script/ENGLISH/05/05-G0-B01.ENG.ORS`<br>`Script/ENGLISH/05/05-KB-B02.ENG.ORS`<br>`Script/ENGLISH/05/05-KI-B02.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KB/05-KB-F10/05-KB-F10-0110.OGG` | 4 | `Script/ENGLISH/05/05-G0-F10.ENG.ORS`<br>`Script/ENGLISH/05/05-KB-F10.ENG.ORS`<br>`Script/ENGLISH/05/05-KI-F10.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KC/05-KC-A00/05-KC-A00-0370.OGG` | 1 | `Script/ENGLISH/05/05-KC-A00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KC/05-KC-D03/05-KC-D03-0090.OGG` | 1 | `Script/ENGLISH/05/05-KC-D03.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KC/05-KC-D03/05-KC-D03-0130.OGG` | 1 | `Script/ENGLISH/05/05-KC-D03.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KC/05-KC-F00/05-KC-F00-1220.OGG` | 1 | `Script/ENGLISH/05/05-KC-F00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KC/05-KC-H00/05-KC-H00-0260.OGG` | 1 | `Script/ENGLISH/05/05-KC-H00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KC/05-KC-H00/05-KC-H00-0270.OGG` | 1 | `Script/ENGLISH/05/05-KC-H00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KC/05-KC-H00/05-KC-H00-0860.OGG` | 1 | `Script/ENGLISH/05/05-KC-H00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KF/05-KF-A00/05-KF-A00-0490.OGG` | 1 | `Script/ENGLISH/05/05-KF-A00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KF/05-KF-C05/05-KF-C05-0140.OGG` | 2 | `Script/ENGLISH/05/05-KF-C05.ENG.ORS`<br>`Script/ENGLISH/05/05-SE-C05.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KF/05-KF-H06/05-KF-H06-0060.OGG` | 1 | `Script/ENGLISH/05/05-KF-H06.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-KG/05-KG-G00/05-KG-G00-0240.OGG` | 1 | `Script/ENGLISH/05/05-KG-G00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-B00/05-SA-B00-0070.OGG` | 1 | `Script/ENGLISH/05/05-SA-B00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-B01/05-SA-B01-0130.OGG` | 1 | `Script/ENGLISH/05/05-SA-B01.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-H00/05-SA-H00-0030.OGG` | 1 | `Script/ENGLISH/05/05-SA-H00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-H01/05-SA-H01-0040.OGG` | 2 | `Script/ENGLISH/04/04-C0-A01.ENG.ORS`<br>`Script/ENGLISH/05/05-SA-H01.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-I00/05-SA-I00-0480.OGG` | 1 | `Script/ENGLISH/05/05-SA-I00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-I00/05-SA-I00-0800.OGG` | 1 | `Script/ENGLISH/05/05-SA-I00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-I00/05-SA-I00-0970.OGG` | 1 | `Script/ENGLISH/05/05-SA-I00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SA/05-SA-T00/05-SA-T00-0750.OGG` | 1 | `Script/ENGLISH/05/05-SA-T00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SB/05-SB-D06/05-SB-D06-0480.OGG` | 1 | `Script/ENGLISH/05/05-SB-D06.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SB/05-SB-E01/05-SB-E01-0400.OGG` | 1 | `Script/ENGLISH/05/05-SB-E01.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SB/05-SB-E02/05-SB-E02-0060.OGG` | 1 | `Script/ENGLISH/05/05-SB-E02.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SD/05-SD-B03/05-SD-B03-0610.OGG` | 1 | `Script/ENGLISH/05/05-SD-B03.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SD/05-SD-C00/05-SD-C00-0650.OGG` | 1 | `Script/ENGLISH/05/05-SD-C00.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SE/05-SE-C08/05-SE-C08-0200.OGG` | 1 | `Script/ENGLISH/05/05-SE-C08.ENG.ORS` |
| `Voice05.GPK` | `PlayVoice` | `Voice05/05-SE/05-SE-H00/05-SE-H00-0360.OGG` | 1 | `Script/ENGLISH/05/05-SE-H00.ENG.ORS` |

Reprodução: `python tools/schooldays_parity/audit_retail_assets.py "D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ\Packs"`.
