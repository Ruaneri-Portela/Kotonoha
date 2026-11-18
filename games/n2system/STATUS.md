# N2System Port Status

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

## Implementation Coverage

- **NsScript VM & Indexing**: Chapters, labels, subroutines (`Call`, `CallLocal`, `Return`), flow control (`Goto`, `Jump`, `Chapter`).
- **Variable Store & Expressions**: Integer (`$var`), string (`@str`), boolean (`#flag`) variables, expression evaluator with arithmetic, comparison, and logical operators.
- **Scene Graph**: Entity tree (Container, Sprite, Text, Choice) with transforms (x, y, z, scale, angle, opacity, tint) and animations (`fade`, `move`, `zoom`).
- **Audio System**: BGM, Voice, and SE channels with fading, looping, and volume control.
- **Dialogue System**: Typewriter text animation, speaker tracking, and backlog buffer.
- **Save State**: Binary (`N2SV`) and JSON save state serialization.
- **Kotonoha Engine Integration**: `N2Runtime` game port with `VNTextBox`, `VNBacklog`, `InGameMenu`, `ChoiceQuestionScene`.
