# Cairn

Uma linguagem pequena com compilador e VM feitos em C.
Tem sintaxe simples, tipos estaticos e bytecode para uma VM de pilha.

## Estado

- [ ] lexer e comentarios
- [ ] parser e arvore de sintaxe
- [ ] inteiros, booleanos e checagem de tipos
- [ ] funcoes e recursao
- [ ] if, while, break e continue
- [ ] vetores locais e limites de acesso
- [ ] bytecode e vm
- [ ] codigo intermediario
- [ ] blocos e analise de fluxo
- [ ] otimizacoes basicas
- [ ] testes automaticos
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
