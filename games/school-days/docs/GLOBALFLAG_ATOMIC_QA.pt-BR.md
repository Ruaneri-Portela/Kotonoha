# Persistência do GlobalFlag e testes sintéticos

[English (US)](GLOBALFLAG_ATOMIC_QA.md)

## Sistema de arquivos gerenciado pelo motor

O School Days agora delega a gravação a
`Kotonoha::Filesystem::writeBinaryFileAtomically`, através de um
adaptador que mantém apenas o limite de tamanho do perfil em
`SchoolDaysAtomicFile.cpp`. O motor usa exclusivamente **SDL3** para
criar, escrever, descarregar, fechar, remover e substituir arquivos,
incluindo caminhos UTF-8 no Windows. O arquivo temporário é criado
exclusivamente (`wxb`), no mesmo diretório do destino.

Não há chamadas Win32, POSIX, `fcntl`, `fsync` ou consulta direta a
processos na implementação do port. O arquivo `GlobalFlag.DAT` novo só
fica visível depois de `SDL_RenamePath`; uma falha não apaga previamente
o save antigo. Cada escritor limpa somente seu próprio temporário em caso
de erro. Em uma interrupção simulada, o temporário permanece isolado, sem
ser tratado como save nem removido arbitrariamente por outro processo.

**Limite de durabilidade:** `SDL_FlushIO` e `SDL_CloseIO` descarregam
os buffers, mas **não** garantem que bytes e diretório tenham alcançado
a mídia física em caso de queda de energia. Os testes antigos do fork
com `fsync`/`F_FULLFSYNC` validavam outra implementação nativa e não
devem ser tratados como prova desta. O formato `FlgH` e o limite de
32 MiB do perfil continuam inalterados.

Os adaptadores compartilhados `SDLFileSource` e `OutputFile` agora
aceitam caminhos UTF-8 corretamente no Windows. As leituras, consultas de
slot e gravações usam as interfaces de I/O do Kotonoha.

## Validação

- `Kotonoha.AtomicFile`: teste CTest compartilhado com arquivos sintéticos,
  substituição atômica, concorrência, nomes Unicode, falhas simuladas,
  isolamento de temporários e rejeição de links/diretórios.
- `SchoolDays.GlobalFlagAtomic`: testa o codec FlgH integrado ao
  armazenamento do port, limites de tamanho e os erros de publicação.
- `SchoolDays.AllClearSynthetic`: verifica limites e persistência dos
  indicadores de conclusão, sem dados comerciais.
- `shared-atomic-storage.yml`: CI para o escritor SDL em Linux, macOS e
  Windows, com checagem para impedir reintrodução de I/O de sistema
  diretamente no port.
- O launcher do School Days agora segue o padrão dos outros jogos:
  abre por padrão, com `--no-launcher` ou `--frames` para execuções
  automatizadas. Seleção de pasta, rota e configurações lembradas
  continuam usando `GameLauncher`.

Compilação completa, inspeção visual e compatibilidade total continuam
a exigir testes integrados. Nenhum recurso comercial ou save pessoal
foi incorporado a estas mudanças.
