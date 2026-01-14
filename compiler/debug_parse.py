#!/usr/bin/env python3

from tokenizer import Tokenizer
from parser import Parser

source = """Person p = new Person()"""

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
except Exception as e:
    print(f'Error: {e}')