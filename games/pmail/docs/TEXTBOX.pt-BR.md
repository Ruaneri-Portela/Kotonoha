# Caixa de diálogo e avanço automático — Pure Mail

[English (US)](TEXTBOX.md) | **Português (Brasil)**

A pesquisa do executável original mostra que a janela de mensagem desenha, nesta ordem, **WAKUH**, **WAKU** e **BOT**, na resolução lógica **800×600**. WAKUH é o fundo; WAKU define a moldura. O ajuste de cor é aplicado à textura do fundo, preservando o canal alfa.

No avanço automático, o jogo original pode aguardar o fim da voz antes do atraso de mensagem (valor padrão **2000 ms**). O cursor normal usa quadros `0,1,2,3,2,1` com **170 ms** por quadro; o automático alterna para outro recurso. Menus e pausa impedem avanço indevido.

O teste `PmailTextBoxCheck` verifica composição, tint, atraso, voz e bloqueio durante menus. Veja o [relatório original](TEXTBOX.md) para offsets do executável, cálculos exatos de cor e detalhes do teste.
