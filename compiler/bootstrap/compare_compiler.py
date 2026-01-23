import subprocess
import re
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from tokenizer import Tokenizer
from parser import Parser

SOURCE_FILE = "/mnt/ssd/Coding/mt-lang/compiler/bootstrap/tokenizer.mtc"

def get_mtc_token_count():
    proc = subprocess.run(
        ["./compiler"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    out = proc.stdout

    match = re.search(r"Total tokens:\s*(\d+)", out)

    if match:
        token_count = int(match.group(1))
        return token_count
    else:
        print("Could not find token count from mt-lang tokenizer")
        return None
def get_mtc_ast_count():
    proc = subprocess.run(
        ["./compiler"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    out = proc.stdout

    match = re.search(r"Total AST:\s*(\d+)", out)

    if match:
        token_count = int(match.group(1))
        return token_count
    else:
        print("Could not find AST count from mt-lang tokenizer")
        return None
def get_python_token_count():
    with open(SOURCE_FILE, "r") as f:
        source = f.read()

    tokenizer = Tokenizer(source)
    tokens = tokenizer.tokenize()
    return tokens
def get_python_ast_count(tokens):
    parser = Parser(tokens)
    ast =  parser.parse_program()
    return len(ast.statements)

def main():
    print("Comparing tokenizers...")
    print(f"Source file: {SOURCE_FILE}")
    print()

    mtc_count = get_mtc_token_count()
    tokens = get_python_token_count()
    python_count = len(tokens)
    python_ast_count = get_python_ast_count(tokens)

    mtc_ast_count = get_mtc_ast_count()
    print(f"mt-lang tokenizer token count: {mtc_count}")
    print(f"Python tokenizer token count: {python_count}")
    print(f"mt-lang AST count: {mtc_ast_count}")
    print(f"Python AST count: {python_ast_count}")


    if mtc_count is None or mtc_ast_count is None:
        print("ERROR: Could not get token count from mt-lang tokenizer")
        sys.exit(1)

    if mtc_count == python_count and mtc_ast_count == python_ast_count:
        print(f"SUCCESS: Token counts match ({mtc_count} tokens)")
        print(f"SUCCESS: AST counts match ({mtc_ast_count})")
        sys.exit(0)

    else:
        diff = abs(mtc_count - python_count)
        print(f"FAILURE: Token counts don't match (difference: {diff})")
        if mtc_count > python_count:
            print(f"  mt-lang has {diff} more tokens than Python")
        else:
            print(f"  Python has {diff} more tokens than mt-lang")
        sys.exit(1)

if __name__ == "__main__":
    main()
