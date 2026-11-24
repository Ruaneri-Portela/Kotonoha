# Grupos de testes do Kotonoha

As suítes CTest usam exatamente um dos dois rótulos:

- `synthetic`: não precisa dos dados comerciais/de varejo do usuário. Pode criar mídia temporária ou usar os fixtures controlados do repositório.
- `real`: precisa de assets de jogos, imagens de disco ou dados de rota/instalação específicos do jogo, incluindo testes cuja configuração de fixture depende de `game-assets`.

## Executando os testes

Configure e compile seu build nativo com `-DBUILD_TESTING=ON` e execute:

```sh
ctest --test-dir build-linux -L '^synthetic$' --output-on-failure
ctest --test-dir build-linux -L '^real$' --output-on-failure
ctest --test-dir build-linux -N -L '^real$'
```

Para builds multi-config do Windows, adicione `-C Release` (ou sua configuração).

Os builds do GitHub Actions para Linux, Windows e macOS executam **apenas testes CTest sintéticos**.
Os testes de navegador WASM usam fixtures gerados/self-test, não instalações de varejo.
A CI do Android valida a criação dos APKs example/SchoolDays; não executa testes com dados de varejo.
Os testes de varejo são configurados apenas quando o caminho de dados do jogo ou a imagem de disco apropriada é fornecido.
Consulte os arquivos `tests/CMakeLists.txt` de cada jogo para as opções `*_DATA_DIR`, `*_DISC_IMAGE` e outras.

Uma regressão sintética que falha emite um aviso do GitHub Actions e permanece visível nos logs;
ela não suprime erros de compilação, pacotes malformados ou falhas de upload de artefatos.
