# Animações do DVD Pure Mail

[English (US)](ANIMATIONS.md) | **Português (Brasil)**

A investigação original verificou assets do DVD e o executável `pmail.exe`. Os resultados são específicos dessa versão; **não demonstram fidelidade integral**.

- Arquivos `.anim` sequenciais trazem contagem de quadros e tabela de deslocamentos; quadros usam fluxos gráficos CHAR em tela 800×600, com origem na parte inferior.
- Animações de boca usam cabeçalho próprio, até 16 regiões por ator e quadros que podem ser retângulos BGR ou CHAR.
- O comando **SYS 51** seleciona 19 presets. O player bloqueia a VM durante a animação, preserva o quadro final e registra o estado para save/load.
- A animação da boca acompanha ID do ator e o áudio efetivo. A temporização ainda não foi comparada amostra por amostra com o original.
- O cursor normal percorre quadros na sequência `0,1,2,3,2,1`. O modo alternativo do original tem limitações de implementação registradas no teste.

**Validação:** testes de formato, gráficos, reprodução e fluxo de PRO1, com dados locais. Consulte o [relatório original](ANIMATIONS.md) para offsets, SHA-256, tabela de presets e resultados detalhados.
