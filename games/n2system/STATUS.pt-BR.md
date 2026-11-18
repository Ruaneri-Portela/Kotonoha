# Estado do Port N2System

[English (US)](STATUS.md) | **Português (Brasil)**

## Cobertura de Implementação

- **VM & Indexação NsScript**: Capítulos, rótulos, sub-rotinas (`Call`, `CallLocal`, `Return`), controle de fluxo (`Goto`, `Jump`, `Chapter`).
- **Armazenamento de Variáveis e Expressões**: Variáveis inteiras (`$var`), texto (`@str`), booleanas (`#flag`), e avaliador de expressões com operadores aritméticos, lógicos e de comparação.
- **Grafo de Cena**: Árvore de entidades (Contêiner, Sprite, Texto, Escolha) com transformações (x, y, z, escala, ângulo, opacidade, cor) e animações (`fade`, `move`, `zoom`).
- **Sistema de Áudio**: Canais de BGM, Voz e SE com fading, loops e controle de volume.
- **Sistema de Diálogo**: Animação de texto estilo máquina de escrever, oradores e histórico (*backlog*).
- **Save State**: Serialização de save state em formato binário (`N2SV`) e JSON.
- **Integração com Kotonoha Engine**: Runtime `N2Runtime` integrado com `VNTextBox`, `VNBacklog`, `InGameMenu` e `ChoiceQuestionScene`.
