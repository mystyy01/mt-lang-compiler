from tokenizer import Tokenizer
from parser import Parser
from semantic import SemanticAnalyzer

test_file = "test_error.mtc"
with open(test_file, "r") as f:
    source = f.read()
tokenizer = Tokenizer(source, test_file)
tokens = tokenizer.tokenize()
print(tokens)
parsed = Parser(tokens, test_file).parse_program()
print(parsed)
print("=== AST ===")
print(parsed)
print("\n=== Semantic Analysis ===")
analyzer = SemanticAnalyzer(test_file)
analyzer.analyze(parsed)
if analyzer.errors:
    print("Errors found:")
    for error in analyzer.errors:
        print(f"  - {error}")
else:
    print("No errors found!")