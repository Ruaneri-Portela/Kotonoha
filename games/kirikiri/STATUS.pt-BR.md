# Kirikiri2 / KirikiriZ — resumo de suporte

[English (US)](STATUS.md) | **Português (Brasil)**

**Escopo implementado/testado:** Arquivos XP3 v1/v2 padrão (índices encadeados/zlib, arquivos segmentados, precedência de patches), imagens TLG5/TLG6/PIMG, parser binário clean-room PSB v2 + MDF (`PsbFile`), executor de cenários KAGEnv `.ks.scn` (`ScnRunner` com localização PT-BR `VNX+/`, placas de nome `cn_name_*.png`, vozes multicanal, animações keyframe E-mote, persistência de snapshots de savepoint e progressão pelos capítulos `00_01.ks`..`00_15.ks`), telas de interface nativa `VNX+/` (menu inicial `title_cn@*`, Save/Load `saveload_cn@*`, Config `config_cn@*`, Backlog `backlog_cn@*`, Extra/seleção de capítulos `extra_cn@*`, caixa de mensagem/barra de ferramentas `window@help__*` e `qlpopup@*`), scripts KAG3 em texto e avaliação limitada de bootstrap TJS2 estão implementados.

**Limitações:** Pacotes `.xp3` comerciais criptografados por filtros nativos de terceiros (`PackinOne.dll`) são indexados sem contorno de DRM; overlays não criptografados (`VNX+/`) e assets Kirikiri padrão são renderizados diretamente.

[README do port](README.pt-BR.md) · [Todos os ports](../README.pt-BR.md)
