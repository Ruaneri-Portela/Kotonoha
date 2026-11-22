# Encodings do Kotonoha no macOS e iOS — sem iconv

Os builds Apple usam **lib/src/localization/Encoding.mm** como implementação
única de `Kotonoha::ConvertEncoding`, `ShiftJisToUtf8`,
`Utf8ToUtf16` e `Utf16ToUtf8`. O backend combina CoreFoundation com
conversões explícitas de escalares Unicode para UTF-16/UTF-32 e **não**
chama `iconv`, `SDL_iconv` ou `SDL_iconv_string`.

`Encoding.cpp` continua implementando as outras plataformas, mas seu
código de conversão fica excluído em `__APPLE__`. O projeto Xcode
inclui `Encoding.mm` nos targets da engine para macOS e iOS; CMake
também compila os arquivos `.mm` no Apple e linka CoreFoundation.

## Conversões cobertas

| Encoding / aliases | Aplicação |
| --- | --- |
| UTF-8 | Scripts, traduções, interface e leitura de texto |
| CP932, Windows-31J, Shift_JIS, SJIS | Jogos japoneses, pontuação Windows, extensão NEC/IBM e texto SCF |
| Windows-1252, CP1252 | Texto ocidental RealLive/rlBabel |
| ISO-8859-1, LATIN1 | Assets e textos legados |
| UTF-16LE / UTF-16BE | School Days, LucaSystem, arquivos ISO/Joliet e assets com BOM |
| UTF-32LE / UTF-32BE | Assets com BOM, incluindo caracteres suplementares |
| EUC-JP, ISO-2022-JP | Visualizador de textos da Debug Overlay |
| GBK, GB18030, Big5 | Visualizador de textos chineses |
| EUC-KR, KOI8-R | Visualizador de textos coreanos e cirílicos |

As conversões de saída para os mesmos nomes são testadas quando aplicável.
A API permite consultar outras codificações IANA que estejam disponíveis
no CoreFoundation. Um nome desconhecido gera uma exceção, em vez de
interpretar os bytes silenciosamente como UTF-8.

## Compatibilidade e dados inválidos

- CP932 usa a tabela Windows/DOS japonesa e normaliza seis diferenças de
  pontuação Unicode entre Shift-JIS e Windows-31J. Sequências inválidas
  resultam em `?`, sem eliminar caracteres ASCII adjacentes.
- Caracteres que o encoding de destino não consegue representar viram
  um único `?` por escalar Unicode, incluindo caracteres fora do BMP.
- UTF-16LE/BE detecta pares substitutos, preserva caracteres suplementares
  e substitui sequências isoladas por `?`.
- UTF-32LE/BE preserva o byte order, bytes zero e escalas Unicode válidas;
  escalares inválidos são substituídos por `?`.
- Conversões genéricas tratam entrada binária por tamanho explícito,
  evitando truncamento em NUL. As funções nativas `Utf8ToUtf16` e
  `Utf16ToUtf8` mantêm suas APIs anteriores.
- A detecção de BOM no `AssetProvider::decodeTextAsset` continua selecionando
  UTF-8, UTF-16LE/BE e UTF-32LE/BE antes da conversão.

## Build e validação

O `Common.xcconfig` não liga mais `-liconv`. Os `xcconfig` do
macOS e iOS adicionam `-framework CoreFoundation`, e o CMake linka o
framework diretamente em `KotonohaEngine`. O manifest vcpkg não solicita
`libiconv` nos triplets macOS/iOS; RealLive não exige `Iconv::Iconv`
nessas plataformas.

A suíte `.github/workflows/apple-encoding-no-iconv.yml` executa em
macOS com C++17, compila `Encoding.cpp` e `Encoding.mm` juntos,
verifica os símbolos com `nm`, verifica os frameworks vinculados com
`otool` e executa `lib/tests/EncodingAppleTests.cpp`, sem SDL,
fontes ou assets comerciais.

Isso valida a **camada de conversão de textos da engine**, não garante
automaticamente que bibliotecas terceirizadas ou o conjunto completo
dos arquivos de todos os jogos dispensem `iconv` internamente. Para
afirmar isso sobre um aplicativo distribuído, também é necessário
auditar os binários finais e as dependências efetivamente empacotadas.
