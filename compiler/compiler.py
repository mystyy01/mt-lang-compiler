from codegen import CodeGenerator
import llvmlite.ir
from tokenizer import Tokenizer
from parser import Parser
import llvmlite.binding as llvm
import sys
import os

if __name__ == "__main__":
    # Silence all normal output; errors should go to stderr.
    sys.stdout = open(os.devnull, "w")

    # Parse arguments
    object_only = False
    lib_file = False
    args = sys.argv[1:]

    if "-o" in args:
        object_only = True
        args.remove("-o")
    if "--lib" in args:
        lib_file = True
        args.remove("--lib")

    if len(args) != 2:
        print("Provide source code and outfile")
        print("Example usage:")
        print("  mtc source.mtc executable     # compile and link")
        print("  mtc -o source.mtc output.o    # object file only")
        exit(1)

    source_file = args[0]
    out = args[1]

    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()

    # Get source directory for module imports
    source_dir = os.getcwd()

    with open(source_file, "r") as f:
        source_code = f.read()
    
    # Use absolute path for clearer error messages
    abs_source_file = os.path.abspath(source_file)
    tokens = Tokenizer(source_code, abs_source_file).tokenize()
    ast = Parser(tokens, abs_source_file).parse_program()
    
    # Prepend exception imports to enable try/catch/throw
    from tokenizer import Tokenizer as ExTokenizer
    from parser import Parser as ExParser
    stdlib_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stdlib")
    exceptions_path = os.path.join(stdlib_dir, "exceptions.mtc")
    if os.path.exists(exceptions_path):
        with open(exceptions_path, "r") as f:
            exceptions_code = f.read()
        exc_tokens = ExTokenizer(exceptions_code, exceptions_path).tokenize()
        exc_ast = ExParser(exc_tokens, exceptions_path).parse_program()
        
        # Prepend exception classes to the program
        ast.statements = exc_ast.statements + ast.statements
    
    from semantic import SemanticAnalyzer
    analyzer = SemanticAnalyzer(abs_source_file)
    analyzer.analyze(ast)
    if analyzer.errors:
        for error in analyzer.errors:
            print(f"Error: {error}")
        exit(1)
    gen = CodeGenerator(source_dir=source_dir, lib_file=lib_file)
    
    # Pass semantic analyzer to code generator for class info
    if hasattr(analyzer, 'classes'):
        gen.classes = analyzer.classes
    if not lib_file:
        gen.create_main_function()
    result = gen.generate(ast)
    if not lib_file:
        # Only add return if block is not already terminated
        if not gen.builder.block.is_terminated:
            # Check if result is None or has void type (void calls return void-typed instruction)
            if result is None or (hasattr(result, 'type') and result.type == llvmlite.ir.VoidType()):
                result = llvmlite.ir.Constant(gen.int_type, 0)
            gen.builder.ret(result)


    # Compile to machine code
    llvm_ir = str(gen.module)
    # print(llvm_ir)  # Debug: uncomment to see generated IR
    
    # Wrap LLVM parsing with detailed error information
    try:
        mod = llvm.parse_assembly(llvm_ir)
        mod.verify()
    except RuntimeError as e:
        error_msg = str(e)
        print(f"\n=== LLVM IR Parsing Error ===")
        print(f"Error: {error_msg}")
        
        # Try to extract line number from error message
        import re
        line_match = re.search(r'<string>:(\d+):(\d+)', error_msg)
        if line_match:
            line_num = int(line_match.group(1))
            print(f"\n=== Problem around line {line_num} of generated IR ===")
            lines = llvm_ir.split('\n')
            start = max(0, line_num - 5)
            end = min(len(lines), line_num + 3)
            print("Context:")
            for i in range(start, end):
                prefix = ">>> " if i == line_num - 1 else "    "
                print(f"{prefix}{i+1}: {lines[i]}")
        else:
            # Try to find line with '}' that's not properly indented
            print("\n=== Searching for potential issues ===")
            lines = llvm_ir.split('\n')
            for i, line in enumerate(lines):
                if line.strip() == '}' and i > 0 and lines[i-1].strip() and not lines[i-1].strip().endswith(':'):
                    print(f"Potential issue at line {i+1}:")
                    for j in range(max(0, i-2), min(len(lines), i+3)):
                        prefix = ">>> " if j == i else "    "
                        print(f"{prefix}{j+1}: {lines[j]}")
                    break
        
        # Show the last source position tracked during code generation
        last_pos = getattr(gen, '_last_source_position', None)
        if last_pos:
            src_line, src_col, src_file = last_pos
            print(f"\n=== Last source code position during code generation ===")
            # Use the original source file if file_path not tracked
            display_file = src_file if src_file else abs_source_file
            if display_file:
                print(f"File: {display_file}")
            if src_line:
                print(f"Line: {src_line}, Column: {src_col}")
                # Show the actual source line if possible
                if os.path.exists(display_file):
                    try:
                        with open(display_file, 'r') as sf:
                            lines = sf.readlines()
                            if 0 < src_line <= len(lines):
                                source_line = lines[src_line-1].rstrip()
                                print(f"Source: {source_line}")
                                # Show pointer to column if available
                                if src_col and src_col > 0 and len(source_line) >= src_col:
                                    pointer = " " * (src_col - 1) + "^"
                                    print(f"         {pointer}")
                    except Exception as ex:
                        print(f"  (could not read source: {ex})")
            else:
                print(f"Column: {src_col}" if src_col else "Position unknown")
        
        print("\n=== This is a compiler code generation bug ===")
        print("Please report this issue with the source file and this error message.")
        exit(1)

    # Create target machine
    target = llvm.Target.from_default_triple()
    target_machine = target.create_target_machine()

    # Write object 
    obj_file = out if object_only else f"{out}.o"
    with open(obj_file, "wb") as f:
        f.write(target_machine.emit_object(mod))

    # print(f"\n=== Compiled to {obj_file} ===")

    if object_only:
        exit(0)

    # print("\n=== Compiling to binary ===")
    os.system(f"clang {out}.o -o {out} -lm")
    os.system(f"rm {out}.o")
    # print(f"\n=== Compiled to {out} ===")
    # print("\n=== Running now ===")

    out_path = os.path.abspath(out)
    out_dir = os.path.dirname(out_path)
    out_name = os.path.basename(out_path)

    # If executable is in current directory, run with ./
    if out_dir == os.getcwd() or out_dir == "":
        os.system(f"./{out_name}")
    else:
        os.system(out_path)
