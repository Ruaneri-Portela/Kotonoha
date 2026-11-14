# Fixtures sintéticas de School Days

[English (US)](README.md) | **Português (Brasil)**

São mídias artificiais geradas exclusivamente para testes, **não** recursos comerciais do School Days: PNG azul 64×64, vídeo vermelho 64×64 a 24 Hz (WMV2/WMA2) com tom de 440 Hz e áudio OGG/Vorbis de 220 Hz. Duração: **2 segundos**.

Foram produzidos com os filtros `color`/`sine` do FFmpeg e codecs de teste. Os decodificadores já presentes no motor leem esses arquivos; não é necessário executar FFmpeg nos próprios testes. O script A03 adicional testa uma escolha temporizada sintética com duas opções, sem diálogo ou arte comerciais.
