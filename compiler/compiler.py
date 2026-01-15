from codegen import CodeGenerator
import llvmlite.ir
from tokenizer import Tokenizer
from parser import Parser
import llvmlite.binding as llvm
import sys
import os

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Provide source code and outfile")
        print("Example usage:\npython compiler.py source.mtc executable")
        exit(1)
    else:
        source_file = sys.argv[1]
        out = sys.argv[2]

    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()

    # Get source directory for module imports
    source_dir = os.path.dirname(os.path.abspath(source_file))

    with open(source_file, "r") as f:
        source_code = f.read()
    
    # Use absolute path for clearer error messages
    abs_source_file = os.path.abspath(source_file)
    tokens = Tokenizer(source_code, abs_source_file).tokenize()
    ast = Parser(tokens, abs_source_file).parse_program()
    from semantic import SemanticAnalyzer
    analyzer = SemanticAnalyzer(abs_source_file)
    analyzer.analyze(ast)
    if analyzer.errors:
        for error in analyzer.errors:
            print(f"Error: {error}")
        exit(1)
    gen = CodeGenerator(source_dir=source_dir)
    
    # Pass semantic analyzer to code generator for class info
    if hasattr(analyzer, 'classes'):
        gen.classes = analyzer.classes
    gen.create_main_function()
    result = gen.generate(ast)
    # Check if result is None or has void type (void calls return void-typed instruction)
    if result is None or (hasattr(result, 'type') and result.type == llvmlite.ir.VoidType()):
        result = llvmlite.ir.Constant(gen.int_type, 0)
    gen.builder.ret(result)


    # Compile to machine code
    llvm_ir = str(gen.module)
    print(llvm_ir)
    mod = llvm.parse_assembly(llvm_ir)
    mod.verify()

    # Create target machine
    target = llvm.Target.from_default_triple()
    target_machine = target.create_target_machine()

    # Write object file
    with open(f"{out}.o", "wb") as f:
        f.write(target_machine.emit_object(mod))

    print(f"\n=== Compiled to {out}.o ===")
    print("\n=== Compiling to binary ===")
    import os
    os.system(f"clang {out}.o -o {out}")
    os.system(f"rm {out}.o")
    print(f"\n=== Compiled to {out} ===")
    print("\n=== Running now ===")
    os.system(f"./{out}")
