# Pesquisa de áudio — Pure Mail

[English (US)](AUDIO_PARITY.md) | **Português (Brasil)**

A análise do `pmail.exe` original e das tabelas ADPCM confirmou saída **PCM estéreo, 44.100 Hz, 16 bits**. Isso difere do valor padrão usado pelo provedor Snow Radish.

- Os recursos SOUND.DAT incluem músicas **ADPS** e efeitos **ADP4**; escolha do codec deve usar a extensão do arquivo.
- Os comandos da VM controlam BGM, efeitos e voz, incluindo espera, pausa, substituição e crossfade.
- Fades usam passos de ganho nativos; a implementação atualiza o ganho em passos de apresentação, portanto ainda não garante alinhamento exato de amostras.
- Save/load deve conservar o estado dos players, incluindo stream de saída em crossfade.
- Os testes de áudio validam cabeçalhos e agendamento, não qualidade auditiva nem fidelidade integral a cada rota.

Veja [AUDIO_PARITY.md](AUDIO_PARITY.md) para SHA-256, offsets, lista de opcodes, medições originais e reprodução dos testes.
