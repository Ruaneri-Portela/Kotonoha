# School Days HQ / Kotonoha v3 L4 — instalação dos PNG L×H universais (2026-11-07)

## Escopo executado

- Build Windows **somente**: `D:/Dev/Kotonoha-v3/build/school-days-dev-control4/games/school-days/Release`.
- Origem do usuário (somente leitura): `D:/SCHOOL DAYS HQ/Overflow/SCHOOLDAYS HQ/LxH`.
- Destino local: `<Release>/display-options/LxH`.
- Substituídos **apenas** `05-A2-F00-LXH.png`, `05-A2-Z00-LXH.png`,
  `05-B0-F00-LXH.png`, `05-G0-L03-LXH.png`, `05-KC-F00-LXH.png`,
  `05-SB-E01-LXH.png` e `05-SC-L03-LXH.png` pelas versões inglesas,
  sem redimensionar, recomprimir ou modificar a origem.
- Atualizados exclusivamente os sete SHA256 correspondentes no manifesto
  privado `<Release>/display-options/.school-days-lxh-local-manifest.json`
  (`format: 1`, 28 recursos).

## Backup e comprovação

Backup privado **anterior** à cópia:

`D:/Dev/_temp_lxh_static_audit_worker8/l4-universal-english-20261107-003817-5930017`

Contém `spanish-png-original/` (sete PNG espanhóis anteriores),
`manifest-original.json`, `preflight.json` e `result.json` com SHA256.
O manifesto instalado ao final tem SHA256
`846abb3c8c00e082f48c09bb368ef86009d9f42e41e4347a3bd02687b669c1d0`.

Pré/pós-validação automatizada na instalação efetiva:

- **7/7 PNG** instalados idênticos byte-a-byte à origem inglesa; PNG
  íntegro, dimensões `800×452`, nomes preservados.
- **28/28 SHA256** conferidos com o manifesto.
- Os outros **21/21** arquivos no manifesto (sete ORS, sete WMV, sete OGG)
  permaneceram com os mesmos hashes.
- `Packs/Script.GPK` e **sete SaveFile DAT** de QA permaneceram com os
  mesmos hashes; não foram escritos.
- A pasta comercial original, os GPKs originais, o código-fonte do motor e
  os JSON privados de tradução não foram alterados nesta etapa.

## Testes posteriores à cópia

- `SchoolDaysLxhPrivateMountProbe.exe`: **PASS**, montagem OFF → ON → OFF,
  sete roteiros de epílogo, 21 mídias presentes, Display Options preservado.
- `SchoolDaysLxhPolicyTest` + `SchoolDaysLxhFadeTest`: **2/2 PASS**.
- `python tools/schooldays_lxh/install_local_l4.py --release <Release> --status`:
  **PASS**, manifesto gerenciado e 28/28 recursos íntegros.
- Teste rápido real de inicialização do L4 (`SDL_VIDEODRIVER=dummy`,
  `SDL_AUDIODRIVER=dummy`, `--documents` isolado,
  `--renderer software --frames 6 --no-config --no-launcher`):
  **4/4 PASS** (EN/ES × L×H OFF/ON).
  Logs privados:
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-universal-english-smoke-20261107-004132-7843669`.
- Auditoria nativa do ES V4b após a troca:
  OFF **30.968/30.968**, ON **31.241/31.241**, sem lacuna, identidade não
  revisada ou overflow; 485 opções de escolha nos dois modos.
  Logs privados:
  `D:/Dev/_temp_lxh_static_audit_worker8/qa-l4-universal-english-probes-20261107-004317-3122314`.

## Limites / gates posteriores

Os smokes de seis quadros **não certificam** renderização física do cartão,
reprodução completa WMV/OGG, sincronia audiovisual ou os sete encerramentos
em 1×. A inspeção de cada cartão inglês com L×H ON (e sua ausência com OFF)
permanece parte da matriz Etapa 5. O inglês dos **273 novos PRINT_TEXT**
também não foi concluído nesta etapa; pertence à Etapa 2 e exige JSON
específico mais integração por idioma na Etapa 3.

Nunca reexecutar o instalador antigo de forma destrutiva nem utilizar
`--disable` sem intenção explícita de remover a DLC local. Nenhum commit,
push ou build Android foi realizado.
