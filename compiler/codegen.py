import llvmlite.ir
from ast_nodes import *

class CodeGenerator:
    def __init__(self):
        self.variables = {}
        self.functions = {}
        self.array_lengths = {}
        self.module = llvmlite.ir.Module(name="mt_lang")
        self.int_type = llvmlite.ir.IntType(32)
        self.void_type = llvmlite.ir.VoidType()
        self.i8_ptr_type = llvmlite.ir.IntType(8).as_pointer()
        self.bool_type = llvmlite.ir.IntType(1)
        self.string_counter = 0
    def create_main_function(self):
        func_type = llvmlite.ir.FunctionType(self.int_type, [])
        func = llvmlite.ir.Function(self.module, func_type, name="main")
        block = func.append_basic_block(name="entry")
        self.builder = llvmlite.ir.IRBuilder(block)
        self.create_printf_function()
    def create_printf_function(self):
        printf_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type], var_arg=True)
        self.printf = llvmlite.ir.Function(self.module, printf_type, "printf")
    def create_string_constant(self, string: str):
        encoded = string.encode("utf-8") + b'\x00'
        array_type = llvmlite.ir.ArrayType(llvmlite.ir.IntType(8), len(encoded))
        global_var = llvmlite.ir.GlobalVariable(self.module, array_type, name=f"str_{self.string_counter}")
        self.string_counter += 1
        global_var.global_constant = True
        global_var.initializer = llvmlite.ir.Constant(array_type, bytearray(encoded))
        zero = llvmlite.ir.Constant(llvmlite.ir.IntType(32), 0)
        ptr = self.builder.gep(global_var, [zero, zero], name="str_ptr")
        return ptr
    def generate(self, node):
        if isinstance(node, Program):
            result = None
            for statement in node.statements:
                result = self.generate(statement)
            return result
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
            elif node.operator.value == "==":
                return self.builder.icmp_signed("==", left, right, name="eqtmp")
            elif node.operator.value == "!=":
                return self.builder.icmp_signed("!=", left, right, name="netmp")
            elif node.operator.value == ">":
                return self.builder.icmp_signed(">", left, right, name="gttmp")
            elif node.operator.value == "<":
                return self.builder.icmp_signed("<", left, right, name="lttmp")
            elif node.operator.value == "<=":
                return self.builder.icmp_signed("<=", left, right, name="letmp")
            elif node.operator.value == ">=":
                return self.builder.icmp_signed(">=", left, right, name="getmp")
        if isinstance(node, VariableDeclaration):
            if node.type == "int":
                pointer = self.builder.alloca(self.int_type, name=node.name)
                self.variables[node.name] = pointer
                if node.value:
                    value = self.generate(node.value) # generate code for the int
                    return self.builder.store(value, pointer)
            elif node.type == "bool":
                pointer = self.builder.alloca(self.bool_type, name=node.name)
                self.variables[node.name] = pointer
                if node.value:
                    value = self.generate(node.value)
                    return self.builder.store(value, pointer)
            elif node.type == "array":
                if isinstance(node.value, ArrayLiteral):
                    elements = node.value.elements
                    length = len(elements)
                    array_type = llvmlite.ir.ArrayType(self.int_type, length)
                    pointer = self.builder.alloca(array_type, name=node.name)
                    self.variables[node.name] = pointer
                    self.array_lengths[node.name] = length

                    for i, element in enumerate(elements):
                        value = self.generate(element)
                        zero = llvmlite.ir.Constant(self.int_type, 0)
                        idx = llvmlite.ir.Constant(self.int_type, i)
                        elem_ptr = self.builder.gep(pointer, [zero, idx], name=f"{node.name}_elem_{i}")
                        self.builder.store(value, elem_ptr)
        if isinstance(node, Identifier):
            pointer = self.variables[node.name]
            return self.builder.load(pointer, name=node.name)
        if isinstance(node, SetStatement):
            pointer = self.variables[node.name]
            value = self.generate(node.value)
            return self.builder.store(value, pointer)
        if isinstance(node, ExpressionStatement):
            return self.generate(node.expression)
        if isinstance(node, CallExpression):
            if node.callee.name == "print":
                arg = node.arguments[0]
                if isinstance(arg, StringLiteral):
                    format_ptr = self.create_string_constant("%s\n")
                else:
                    format_ptr = self.create_string_constant("%d\n")
                value = self.generate(arg)
                return self.builder.call(self.printf, [format_ptr, value])
            else:
                func = self.functions[node.callee.name]
                args = []
                for arg in node.arguments:
                    args.append(self.generate(arg))
                return self.builder.call(func, args)
        if isinstance(node, StringLiteral):
            return self.create_string_constant(node.value)
        if isinstance(node, FunctionDeclaration):
            old_builder = self.builder
            old_variables = self.variables
            self.variables = {}
            param_types = [self.int_type] * len(node.parameters)
            func_type = llvmlite.ir.FunctionType(self.int_type, param_types)
            func = llvmlite.ir.Function(self.module, func_type, name=node.name)
            self.functions[node.name] = func
            block = func.append_basic_block(name="entry")
            self.builder = llvmlite.ir.IRBuilder(block)
            for arg, param in zip(func.args, node.parameters):
                pointer = self.builder.alloca(self.int_type, name=param.name)
                self.builder.store(arg, pointer)
                self.variables[param.name] = pointer
            for statement in node.body.statements:
                self.generate(statement)
            self.builder = old_builder
            self.variables = old_variables
        if isinstance(node, ReturnStatement):
            value = self.generate(node.value)
            return self.builder.ret(value)
        if isinstance(node, IfStatement):
            func = self.builder.block.parent
            condition = self.generate(node.condition)
            if condition.type != llvmlite.ir.IntType(1):
                zero = llvmlite.ir.Constant(condition.type, 0)
                condition = self.builder.icmp_signed("!=", condition, zero, name="tobool")
            then_block = func.append_basic_block(name="then")
            else_block = func.append_basic_block(name="else")
            merge_block = func.append_basic_block(name="merge")
            self.builder.cbranch(condition, then_block, else_block)
            self.builder.position_at_end(then_block)
            for statement in node.then_body.statements:
                self.generate(statement)
            self.builder.branch(merge_block)

            self.builder.position_at_end(else_block)
            if node.else_body:
                for statement in node.else_body.statements:
                    self.generate(statement)
            self.builder.branch(merge_block)
            
            self.builder.position_at_end(merge_block)
        if isinstance(node, WhileStatement):
            func = self.builder.block.parent

            loop_block = func.append_basic_block(name="loop")
            body_block = func.append_basic_block(name="body")
            exit_block = func.append_basic_block(name="exit")

            self.builder.branch(loop_block)

            self.builder.position_at_end(loop_block)
            condition = self.generate(node.condition)
            if condition.type != llvmlite.ir.IntType(1):
                zero = llvmlite.ir.Constant(condition.type, 0)
                condition = self.builder.icmp_signed("!=", condition, zero, name="tobool")
            self.builder.cbranch(condition, body_block, exit_block)

            self.builder.position_at_end(body_block)
            for statement in node.then_body.statements:
                self.generate(statement)
            self.builder.branch(loop_block)

            self.builder.position_at_end(exit_block)
        if isinstance(node, BoolLiteral):
            return llvmlite.ir.Constant(llvmlite.ir.IntType(1), 1 if node.value else 0)
        if isinstance(node, ForInStatement):
            func = self.builder.block.parent

            array_name = node.iterable.name
            array_ptr = self.variables[array_name]
            length = self.array_lengths[array_name]

            index_ptr = self.builder.alloca(self.int_type, name="loop_index")
            self.builder.store(llvmlite.ir.Constant(self.int_type, 0), index_ptr)

            loop_var_ptr = self.builder.alloca(self.int_type, name=node.variable)
            self.variables[node.variable] = loop_var_ptr

            loop_block = func.append_basic_block(name="for_loop")
            body_block = func.append_basic_block(name="for_body")
            exit_block = func.append_basic_block(name="for_exit")

            self.builder.branch(loop_block)

            self.builder.position_at_end(loop_block)
            index = self.builder.load(index_ptr, name="index")
            length_const = llvmlite.ir.Constant(self.int_type, length)
            condition = self.builder.icmp_signed("<", index, length_const, name="for_cond")
            self.builder.cbranch(condition, body_block, exit_block)

            self.builder.position_at_end(body_block)

            zero = llvmlite.ir.Constant(self.int_type, 0)
            elem_ptr = self.builder.gep(array_ptr, [zero, index], name="elem_ptr")
            elem_value = self.builder.load(elem_ptr, name="elem")
            self.builder.store(elem_value, loop_var_ptr)

            for statement in node.body.statements:
                self.generate(statement)

            index_val = self.builder.load(index_ptr, name="index_val")
            next_index = self.builder.add(index_val, llvmlite.ir.Constant(self.int_type, 1), name="next_index")
            self.builder.store(next_index, index_ptr)
            self.builder.branch(loop_block)

            self.builder.position_at_end(exit_block)
if __name__ == "__main__":
    from tokenizer import Tokenizer
    from parser import Parser
    import llvmlite.binding as llvm

    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()

    with open("test.mtc", "r") as f:
        source = f.read()
    tokens = Tokenizer(source).tokenize()
    ast = Parser(tokens).parse_program()

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
    with open("output.o", "wb") as f:
        f.write(target_machine.emit_object(mod))

    print("\n=== Compiled to output.o ===")
    print("\n=== Compiling to binary ===")
    import os
    os.system("clang output.o -o program")
    print("\n=== Compiled to program ===")
    print("\n=== Running now ===")
    os.system("./program")