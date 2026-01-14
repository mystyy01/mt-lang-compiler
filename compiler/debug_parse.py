#!/usr/bin/env python3

from tokenizer import Tokenizer
from parser import Parser
from semantic import SemanticAnalyzer

source = """class TestClass {
    int x
    string name
    void greet() {
        print("Hello from " + this.name)
    }
    int getX() {
        return this.x
    }
}"""

tokenizer = Tokenizer(source)
tokens = tokenizer.tokenize()

print("Tokens:")
for i, token in enumerate(tokens):
    print(f"{i:3d}: {token.type:15s} {token.value}")

parser = Parser(tokens)
try:
    ast = parser.parse_program()
    print('Parsing successful!')
    print('AST:', ast)

    # Now test semantic analysis
    analyzer = SemanticAnalyzer()
    analyzer.analyze(ast)
    if analyzer.errors:
        print('Semantic errors:')
        for error in analyzer.errors:
            print(f'  {error}')
    else:
        print('Semantic analysis successful!')

except Exception as e:
    print(f'Error: {e}')