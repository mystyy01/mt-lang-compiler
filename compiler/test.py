from tokenizer import Tokenizer

with open("../showcase.mtc", "r") as f:
    source = f.read()
tokenizer = Tokenizer(source)
tokens = tokenizer.tokenize()
print(tokens)
