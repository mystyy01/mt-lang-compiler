import llvmlite.ir
from ast_nodes import *

class CodeGenerator:
    def __init__(self):
        self.variables = {}
        self.module = llvmlite.ir.Module(name="mt_lang")
        self.int_type = llvmlite.ir.IntType(32)
        self.void_type = llvmlite.ir.VoidType()
    def create_main_function(self):
        func_type = llvmlite.ir.FunctionType(self.int_type, [])
        func = llvmlite.ir.Function(self.module, func_type, name="main")
        block = func.append_basic_block(name="entry")
        self.builder = llvmlite.ir.IRBuilder(block)
    def generate(self, node):
        if isinstance(node, NumberLiteral):
            return llvmlite.ir.Constant(self.int_type, int(node.value))
        if isinstance(node, BinaryExpression):
            left = self.generate(node.left)
            right = self.generate(node.right)
            if node.operator.value == "+":
                return self.builder.add(left, right, name="addtmp")
            elif node.operator.value == "-":
                return self.builder.sub(left, right, name="subtmp")
            elif node.operator.value == "*":
                return self.builder.mul(left, right, name="multmp")
            elif node.operator.value == "/":
                return self.builder.sdiv(left, right, name="divtmp")
        if isinstance(node, VariableDeclaration):
            if node.type == "int":
                self.builder.alloca(self.int_type, name=node.name)
                self.variables.update(node.name, node.value)
                if node.value:
                    pass # generate code for the int
                    # return pointer
if __name__ == "__main__":
    from tokenizer import Tokenizer
    from parser import Parser
    import llvmlite.binding as llvm

    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()

    source = "5 + 3 * 2"
    tokens = Tokenizer(source).tokenize()
    ast = Parser(tokens).parse_expression()

    gen = CodeGenerator()
    gen.create_main_function()
    result = gen.generate(ast)
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
    with open("output.o", "wb") as f:
        f.write(target_machine.emit_object(mod))

    print("\n=== Compiled to output.o ===")
    print("Run: clang output.o -o program && ./program && echo $?")