from tokenizer import Tokenizer
from parser import Parser
from semantic import SemanticAnalyzer

with open("test_error.mtc", "r") as f:
    source = f.read()
tokenizer = Tokenizer(source)
tokens = tokenizer.tokenize()
print(tokens)
parsed = Parser(tokens).parse_program()
print(parsed)
print("=== AST ===")
print(parsed)
print("\n=== Semantic Analysis ===")
analyzer = SemanticAnalyzer()
analyzer.analyze(parsed)
if analyzer.errors:
    print("Errors found:")
    for error in analyzer.errors:
        print(f"  - {error}")
else:
    print("No errors found!")