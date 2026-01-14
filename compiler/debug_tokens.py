#!/usr/bin/env python3

from tokenizer import Tokenizer

source = """class Person{
    int age = 145
}
Person p = new Person()
p.age = 12
print(p.age)"""

tokenizer = Tokenizer(source)
tokens = tokenizer.tokenize()

print("Tokens:")
for i, token in enumerate(tokens):
    print(f"{i:3d}: {token.type:15s} {token.value}")