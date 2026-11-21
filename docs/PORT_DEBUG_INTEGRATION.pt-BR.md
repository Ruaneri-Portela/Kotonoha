# Padrão de debug, tradução e dublagem dos ports

Todos os jogos devem utilizar o **mesmo inspetor de assets, editor de tradução
e applet de dublagem** do Kotonoha. O código específico dos motores deve
ficar apenas nos adaptadores `Runtime` e `AssetProvider`.

## Regras de integração

- **Assets:** implementar `listAsset`, `containsAsset` e `readAsset`
  com IDs lógicos estáveis. O Kotonoha cuida dos overlays, da exportação
  dos bytes originais/efetivos e dos previews. Provedores de arquivos
  compactados devem listar seus índices sem carregar todos os dados.
- **Nodos:** publicar cenários ou scripts em `debugNodes`, com referências
  aos assets usados. A navegação em `debugJumpToNode` deve validar destinos.
  A chave `debugNodeCatalogKey` informa quando é necessário recarregar.
- **Tradução:** publicar textos originais por rota em
  `translationSources`; nomes, falas e escolhas correntes em
  `activeTranslationKeys`. A apresentação usa
  `GetTranslation().text(rota, original)` com a mesma rota do editor.
- **Dublagem:** `debugSceneAudio` fornece ID lógico, personagem e fala
  original. Música/efeitos não devem receber um personagem inventado. O
  applet usa `AssetProvider` e notificações de alteração de overlay.
- **Interface:** F10 exibe scripts e assets, inclusive jogos sem nodos
  próprios. F7 apresenta as rotas e falas. A dublagem inclui uma busca
  opcional por arquivos de áudio do provider.

## Cobertura dos 15 jogos

Amagami; DDLC; Example (VNManager); Katawa Shoujo (runtime Ren'Py
compartilhado); Kirikiri; LucaSystem; MEO; N2System (nativo ou
Ponscripter); Pure Mail; Ponscripter; RealLive; School Days;
Snow Radish Vacation; Summer Radish Vacation; Zero no Tsukaima.

O mapeamento exato de cada aplicação e motor está no documento em inglês
`docs/PORT_DEBUG_INTEGRATION.md`.

## Auditoria automatizada

```sh
python3 tools/check_debug_bridge.py
python3 tools/check_port_debug_contracts.py
```

O teste `check_port_debug_contracts.py` exige um mapeamento para cada
jogo registrado em `ProjectManifest.json`. O CI também compila e executa
os testes C++17 isolados do catálogo de áudio e de nodos do LucaSystem.

**Limitação:** esses testes verificam contratos de código e pequenas
funcionalidades isoladas. Não substituem a compilação integral do projeto,
a execução de cada motor, nem os testes com dados originais. Um asset pode
ser listado corretamente mesmo quando seu formato ainda não é decodificado
pelo jogo. Novos ports devem preservar essas distinções.
