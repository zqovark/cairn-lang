# Cairn

Uma linguagem pequena com compilador e VM feitos em C.
Tem sintaxe simples, tipos estaticos e bytecode para uma VM de pilha.

## Estado

- [x] lexer e comentarios
- [x] parser e arvore de sintaxe
- [x] inteiros, booleanos e checagem de tipos
- [x] funcoes e recursao
- [x] if, while, break e continue
- [x] vetores locais e limites de acesso
- [x] bytecode e vm
- [ ] codigo intermediario
- [ ] blocos e analise de fluxo
- [ ] otimizacoes basicas
- [x] testes automaticos
- [ ] textos (strings)
- [ ] numeros com ponto flutuante
- [ ] importar outros arquivos
- [ ] gerar codigo nativo

## Exemplo

```text
fn main() -> int {
    let resultado: int = 6 * 7;
    print(resultado);
    return 0;
}
```

## Rodar

```sh
make
make test
build/cairn examples/arithmetic.crn
```

## Notas

Tipos: `int`, `bool` e `void` no retorno de funcoes.
O compilador gera bytecode. A execucao fica por conta da VM.
Variaveis usam `let`. Funcoes usam `fn`. Blocos usam chaves.
