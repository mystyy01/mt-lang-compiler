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
    # Only add return if block is not already terminated
    if not gen.builder.block.is_terminated:
        # Check if result is None or has void type (void calls return void-typed instruction)
        if result is None or (hasattr(result, 'type') and result.type == llvmlite.ir.VoidType()):
            result = llvmlite.ir.Constant(gen.int_type, 0)
        gen.builder.ret(result)


    # Compile to machine code
    llvm_ir = str(gen.module)
    print(llvm_ir)
    
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

    # Write object file
    with open(f"{out}.o", "wb") as f:
        f.write(target_machine.emit_object(mod))

    print(f"\n=== Compiled to {out}.o ===")
    print("\n=== Compiling to binary ===")
    import os
    os.system(f"clang {out}.o -o {out} -lm")
    os.system(f"rm {out}.o")
    print(f"\n=== Compiled to {out} ===")
    print("\n=== Running now ===")
    os.system(f"./{out}")
