from codegen import CodeGenerator
import llvmlite.ir
from tokenizer import Tokenizer
from parser import Parser
import llvmlite.binding as llvm
import sys

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Provide source code and outfile")
        print("Example usage:\npython compiler.py source.mtc executable")
        exit(1)
    else:
        source = sys.argv[1]
        out = sys.argv[2]

    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()

    with open(source, "r") as f:
        source = f.read()
    tokens = Tokenizer(source).tokenize()
    ast = Parser(tokens).parse_program()
    from semantic import SemanticAnalyzer
    analyzer = SemanticAnalyzer()
    analyzer.analyze(ast)
    if analyzer.errors:
        for error in analyzer.errors:
            print(f"Error: {error}")
        exit(1)
    gen = CodeGenerator()
    gen.create_main_function()
    result = gen.generate(ast)
    if not result:
        result = llvmlite.ir.Constant(gen.int_type, 0)
    gen.builder.ret(result)

    # Print the IR
    print("=== LLVM IR ===")
    print(gen.module)

    # Compile to machine code
    llvm_ir = str(gen.module)
    mod = llvm.parse_assembly(llvm_ir)
    mod.verify()

    # Create target machine
    target = llvm.Target.from_default_triple()
    target_machine = target.create_target_machine()

    # Write object file
    with open(f"{out}.o", "wb") as f:
        f.write(target_machine.emit_object(mod))

    print("\n=== Compiled to output.o ===")
    print("\n=== Compiling to binary ===")
    import os
    os.system(f"clang {out}.o -o {out}")
    os.system(f"rm {out}.o")
    print(f"\n=== Compiled to {out} ===")
    print("\n=== Running now ===")
    os.system(f"./{out}")