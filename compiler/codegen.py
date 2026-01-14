import llvmlite.ir
import os
from ast_nodes import *

class CodeGenerator:
    def __init__(self, source_dir=None):
        self.variables = {}
        self.variable_types = {}
        self.functions = {}
        self.array_lengths = {}
        self.modules = {}  # alias -> {func_name: llvm_func}
        self.module = llvmlite.ir.Module(name="mt_lang")
        self.int_type = llvmlite.ir.IntType(32)
        self.void_type = llvmlite.ir.VoidType()
        self.i8_ptr_type = llvmlite.ir.IntType(8).as_pointer()
        self.bool_type = llvmlite.ir.IntType(1)
        self.string_counter = 0
        self.source_dir = source_dir if source_dir else os.getcwd()
        self.stdlib_path = os.path.join(os.path.dirname(__file__), "stdlib")
        self.imported_modules = set()  # Track which modules we've already imported

        # Dynamic array struct: { i32 length, i32 capacity, i8* data }
        self.dyn_array_type = llvmlite.ir.LiteralStructType([
            self.int_type,      # length
            self.int_type,      # capacity
            self.i8_ptr_type    # data pointer
        ])
        self.dyn_array_ptr_type = self.dyn_array_type.as_pointer()

    def get_llvm_type(self, type_str: str):
        """Map AST type strings to LLVM types"""
        if type_str == "int":
            return self.int_type
        elif type_str == "void":
            return self.void_type
        elif type_str == "bool":
            return self.bool_type
        elif type_str == "string":
            return self.i8_ptr_type
        elif type_str == "array":
            return self.dyn_array_ptr_type
        else:
            # Default to int for unknown types
            return self.int_type

    def resolve_module_path(self, module_path):
        """Resolve module path to actual file path"""
        if isinstance(module_path, MemberExpression):
            # Handle dotted paths like stdlib.math or mypackage.utils
            parts = []
            node = module_path
            while isinstance(node, MemberExpression):
                parts.append(node.property)
                node = node.object
            if isinstance(node, Identifier):
                parts.append(node.name)
            parts.reverse()

            # Check if first part is "stdlib" - use stdlib path
            if parts[0] == "stdlib":
                parts = parts[1:]  # Remove "stdlib" prefix
                rel_path = os.path.join(*parts[:-1], parts[-1] + ".mtc") if len(parts) > 1 else parts[0] + ".mtc"
                file_path = os.path.join(self.stdlib_path, rel_path)
            else:
                # Local module
                rel_path = os.path.join(*parts[:-1], parts[-1] + ".mtc") if len(parts) > 1 else parts[0] + ".mtc"
                file_path = os.path.join(self.source_dir, rel_path)
        elif isinstance(module_path, Identifier):
            # Simple name like "math" - check local only
            rel_path = module_path.name + ".mtc"
            file_path = os.path.join(self.source_dir, rel_path)
        else:
            rel_path = str(module_path) + ".mtc"
            file_path = os.path.join(self.source_dir, rel_path)

        if os.path.exists(file_path):
            return file_path

        raise FileNotFoundError(f"Module not found: {rel_path}")

    def load_module(self, module_path):
        """Load and compile a module file, returning dict of functions"""
        file_path = self.resolve_module_path(module_path)

        if file_path in self.imported_modules:
            return  # Already imported
        self.imported_modules.add(file_path)

        # Import tokenizer and parser here to avoid circular imports
        from tokenizer import Tokenizer
        from parser import Parser

        with open(file_path, "r") as f:
            source = f.read()

        tokens = Tokenizer(source).tokenize()
        ast = Parser(tokens).parse_program()

        # Generate code for all function declarations in the module
        for statement in ast.statements:
            if isinstance(statement, FunctionDeclaration):
                self.generate(statement)
    def create_main_function(self):
        func_type = llvmlite.ir.FunctionType(self.int_type, [])
        func = llvmlite.ir.Function(self.module, func_type, name="main")
        block = func.append_basic_block(name="entry")
        self.builder = llvmlite.ir.IRBuilder(block)
        self.create_printf_function()
        self.create_libc_functions()
    def create_printf_function(self):
        printf_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type], var_arg=True)
        self.printf = llvmlite.ir.Function(self.module, printf_type, "printf")

    def create_libc_functions(self):
        # malloc(size_t size) -> void*
        malloc_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.int_type])
        self.malloc = llvmlite.ir.Function(self.module, malloc_type, "malloc")

        # realloc(void* ptr, size_t size) -> void*
        realloc_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.int_type])
        self.realloc = llvmlite.ir.Function(self.module, realloc_type, "realloc")

        # free(void* ptr)
        free_type = llvmlite.ir.FunctionType(self.void_type, [self.i8_ptr_type])
        self.free = llvmlite.ir.Function(self.module, free_type, "free")

        # memcpy(void* dest, void* src, size_t n) -> void*
        memcpy_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.i8_ptr_type, self.int_type])
        self.memcpy = llvmlite.ir.Function(self.module, memcpy_type, "memcpy")

    def create_dynamic_array(self, elements, elem_type):
        """Create a new dynamic array with initial elements"""
        initial_capacity = max(len(elements), 4)  # At least 4 elements capacity

        # Calculate element size
        if elem_type == self.i8_ptr_type:
            elem_size = 8  # pointer size
        else:
            elem_size = 4  # int size

        # Allocate the array struct on stack
        array_ptr = self.builder.alloca(self.dyn_array_type, name="dyn_array")

        # Set length
        length_ptr = self.builder.gep(array_ptr, [llvmlite.ir.Constant(self.int_type, 0), llvmlite.ir.Constant(self.int_type, 0)], name="length_ptr")
        self.builder.store(llvmlite.ir.Constant(self.int_type, len(elements)), length_ptr)

        # Set capacity
        capacity_ptr = self.builder.gep(array_ptr, [llvmlite.ir.Constant(self.int_type, 0), llvmlite.ir.Constant(self.int_type, 1)], name="capacity_ptr")
        self.builder.store(llvmlite.ir.Constant(self.int_type, initial_capacity), capacity_ptr)

        # Allocate data buffer
        data_size = self.builder.mul(llvmlite.ir.Constant(self.int_type, initial_capacity), llvmlite.ir.Constant(self.int_type, elem_size), name="data_size")
        data_ptr = self.builder.call(self.malloc, [data_size], name="data_ptr")

        # Store data pointer
        data_field_ptr = self.builder.gep(array_ptr, [llvmlite.ir.Constant(self.int_type, 0), llvmlite.ir.Constant(self.int_type, 2)], name="data_field_ptr")
        self.builder.store(data_ptr, data_field_ptr)

        # Store initial elements
        if elem_type == self.i8_ptr_type:
            typed_data_ptr = self.builder.bitcast(data_ptr, self.i8_ptr_type.as_pointer(), name="typed_data")
        else:
            typed_data_ptr = self.builder.bitcast(data_ptr, self.int_type.as_pointer(), name="typed_data")

        for i, elem_val in enumerate(elements):
            elem_ptr = self.builder.gep(typed_data_ptr, [llvmlite.ir.Constant(self.int_type, i)], name=f"elem_{i}")
            self.builder.store(elem_val, elem_ptr)

        return array_ptr, elem_type, elem_size

    def array_append(self, array_ptr, value, elem_type, elem_size):
        """Append an element to a dynamic array"""
        zero = llvmlite.ir.Constant(self.int_type, 0)
        one = llvmlite.ir.Constant(self.int_type, 1)
        two = llvmlite.ir.Constant(self.int_type, 2)

        # Get current length and capacity
        length_ptr = self.builder.gep(array_ptr, [zero, zero], name="len_ptr")
        length = self.builder.load(length_ptr, name="length")

        capacity_ptr = self.builder.gep(array_ptr, [zero, one], name="cap_ptr")
        capacity = self.builder.load(capacity_ptr, name="capacity")

        data_field_ptr = self.builder.gep(array_ptr, [zero, two], name="data_ptr_ptr")
        data_ptr = self.builder.load(data_field_ptr, name="data_ptr")

        # Check if we need to grow
        func = self.builder.block.parent
        grow_block = func.append_basic_block(name="grow")
        store_block = func.append_basic_block(name="store")
        continue_block = func.append_basic_block(name="continue")

        need_grow = self.builder.icmp_signed(">=", length, capacity, name="need_grow")
        self.builder.cbranch(need_grow, grow_block, store_block)

        # Grow block - double capacity and realloc
        self.builder.position_at_end(grow_block)
        new_capacity = self.builder.mul(capacity, llvmlite.ir.Constant(self.int_type, 2), name="new_cap")
        new_size = self.builder.mul(new_capacity, llvmlite.ir.Constant(self.int_type, elem_size), name="new_size")
        new_data = self.builder.call(self.realloc, [data_ptr, new_size], name="new_data")
        self.builder.store(new_data, data_field_ptr)
        self.builder.store(new_capacity, capacity_ptr)
        self.builder.branch(continue_block)

        # Store block - just continue
        self.builder.position_at_end(store_block)
        self.builder.branch(continue_block)

        # Continue - store the new element
        self.builder.position_at_end(continue_block)
        current_data = self.builder.load(data_field_ptr, name="current_data")
        if elem_type == self.i8_ptr_type:
            typed_ptr = self.builder.bitcast(current_data, self.i8_ptr_type.as_pointer())
        else:
            typed_ptr = self.builder.bitcast(current_data, self.int_type.as_pointer())

        elem_ptr = self.builder.gep(typed_ptr, [length], name="new_elem_ptr")
        self.builder.store(value, elem_ptr)

        # Increment length
        new_length = self.builder.add(length, one, name="new_length")
        self.builder.store(new_length, length_ptr)

    def array_length(self, array_ptr):
        """Get the length of a dynamic array"""
        zero = llvmlite.ir.Constant(self.int_type, 0)
        length_ptr = self.builder.gep(array_ptr, [zero, zero], name="len_ptr")
        return self.builder.load(length_ptr, name="arr_length")

    def array_get(self, array_ptr, index, elem_type):
        """Get element at index from dynamic array"""
        zero = llvmlite.ir.Constant(self.int_type, 0)
        two = llvmlite.ir.Constant(self.int_type, 2)

        data_field_ptr = self.builder.gep(array_ptr, [zero, two], name="data_ptr_ptr")
        data_ptr = self.builder.load(data_field_ptr, name="data_ptr")

        if elem_type == self.i8_ptr_type:
            typed_ptr = self.builder.bitcast(data_ptr, self.i8_ptr_type.as_pointer())
        else:
            typed_ptr = self.builder.bitcast(data_ptr, self.int_type.as_pointer())

        elem_ptr = self.builder.gep(typed_ptr, [index], name="elem_ptr")
        return self.builder.load(elem_ptr, name="elem_val")

    def create_string_constant(self, string: str, as_constant=False):
        # Use a simple string cache to avoid duplicate constants
        cache_key = f"str_cache_{string}"
        if hasattr(self, '_string_cache') and cache_key in self._string_cache:
            global_var = self._string_cache[cache_key]
        else:
            if not hasattr(self, '_string_cache'):
                self._string_cache = {}
            
            encoded = string.encode("utf-8") + b'\x00'
            array_type = llvmlite.ir.ArrayType(llvmlite.ir.IntType(8), len(encoded))
            global_var = llvmlite.ir.GlobalVariable(self.module, array_type, name=f"str_{self.string_counter}")
            self.string_counter += 1
            global_var.global_constant = True
            global_var.initializer = llvmlite.ir.Constant(array_type, bytearray(encoded))
            self._string_cache[cache_key] = global_var
        
        zero = llvmlite.ir.Constant(llvmlite.ir.IntType(32), 0)
        if as_constant:
            # Return constant GEP for use in global initializers
            return global_var.gep([zero, zero])
        else:
            # Return instruction GEP for use in function body
            ptr = self.builder.gep(global_var, [zero, zero], name="str_ptr")
            return ptr
    def generate(self, node):
        if isinstance(node, Program):
            result = None
            for statement in node.statements:
                result = self.generate(statement)
            return result
        if isinstance(node, FromImportStatement):
            # Load the module and import the symbols
            self.load_module(node.module_path)
            # For "from math use add, square" - register each function directly
            for symbol in node.symbols:
                if symbol in self.functions:
                    # Make it callable directly by its name
                    self.functions[symbol] = self.functions[symbol]
            return None
        if isinstance(node, SimpleImportStatement):
            # For "use math" - load module and register under module name for math.add() style
            module_name = node.alias if node.alias else node.module_name
            # Resolve module path (checks source dir first, then stdlib)
            from tokenizer import Tokenizer
            from parser import Parser
            file_path = self.resolve_module_path(Identifier(node.module_name))
            if file_path not in self.imported_modules:
                self.imported_modules.add(file_path)
                with open(file_path, "r") as f:
                    source = f.read()
                tokens = Tokenizer(source).tokenize()
                ast = Parser(tokens).parse_program()
                # Track functions before and after to know what was added
                before_funcs = set(self.functions.keys())
                for statement in ast.statements:
                    if isinstance(statement, FunctionDeclaration):
                        self.generate(statement)
                after_funcs = set(self.functions.keys())
                new_funcs = after_funcs - before_funcs
                # Register all new functions under the module name
                if module_name not in self.modules:
                    self.modules[module_name] = {}
                for func in new_funcs:
                    self.modules[module_name][func] = self.functions[func]
            return None
        if isinstance(node, NumberLiteral):
            return llvmlite.ir.Constant(self.int_type, int(node.value))
        if isinstance(node, BinaryExpression):
            left = self.generate(node.left)
            right = self.generate(node.right)
            
            # Handle string comparisons specially
            if left.type == self.i8_ptr_type and right.type == self.i8_ptr_type:
                # String comparison using strcmp (would need to implement)
                # For now, just use pointer equality
                if node.operator.value == "==":
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
            
            # Numeric operations
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
            elif node.operator.value == "&&":
                # Logical AND: both operands must be boolean
                return self.builder.and_(left, right, name="andtmp")
            elif node.operator.value == "||":
                # Logical OR: both operands must be boolean
                return self.builder.or_(left, right, name="ortmp")
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
                    if len(elements) > 0 and isinstance(elements[0], StringLiteral):
                        elem_type = self.i8_ptr_type
                        elem_size = 8
                    else:
                        elem_type = self.int_type
                        elem_size = 4
                    # Generate values for all elements
                    elem_values = []
                    for element in elements:
                        elem_values.append(self.generate(element))
                    # Create dynamic array
                    array_ptr, _, _ = self.create_dynamic_array(elem_values, elem_type)
                    self.variables[node.name] = array_ptr
                    self.array_lengths[node.name] = (elem_type, elem_size)  # Store elem_type and elem_size
                else:
                    # Array assignment from function call or variable
                    array_ptr = self.generate(node.value)
                    self.variables[node.name] = array_ptr
                    # For function returns, assume int elements for now (could be improved)
                    self.array_lengths[node.name] = (self.int_type, 4)
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
            # Handle method calls on objects (like arr.append(value))
            if isinstance(node.callee, MemberExpression):
                obj_name = node.callee.object.name
                method_name = node.callee.property

                # Check if it's an array method call
                if obj_name in self.array_lengths:
                    array_ptr = self.variables[obj_name]
                    elem_type, elem_size = self.array_lengths[obj_name]

                    if method_name == "append":
                        value = self.generate(node.arguments[0])
                        self.array_append(array_ptr, value, elem_type, elem_size)
                        return None
                    elif method_name == "length":
                        return self.array_length(array_ptr)
                    else:
                        raise Exception(f"Unknown array method: {method_name}")

                # Otherwise, it's a module function call like math.add(1, 2)
                module_name = obj_name
                func_name = method_name
                if module_name in self.modules and func_name in self.modules[module_name]:
                    func = self.modules[module_name][func_name]
                elif func_name in self.functions:
                    func = self.functions[func_name]
                else:
                    raise Exception(f"Unknown function: {module_name}.{func_name}")
                args = []
                for arg in node.arguments:
                    args.append(self.generate(arg))
                return self.builder.call(func, args)
            elif isinstance(node.callee, Identifier) and node.callee.name == "print":
                arg = node.arguments[0]
                is_string = False
                if isinstance(arg, StringLiteral):
                    is_string = True
                elif isinstance(arg, Identifier):
                    if arg.name in self.variable_types and self.variable_types[arg.name] == self.i8_ptr_type:
                        is_string = True
                if is_string:
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
                call_result = self.builder.call(func, args)
                # Return None for void functions, LLVM value for others
                if func.function_type.return_type == self.void_type:
                    return None
                return call_result
        if isinstance(node, StringLiteral):
            return self.create_string_constant(node.value)
        if isinstance(node, FunctionDeclaration):
            old_builder = self.builder
            old_variables = self.variables
            old_array_lengths = self.array_lengths
            old_variable_types = self.variable_types
            old_current_return_type = getattr(self, 'current_return_type', None)
            
            self.array_lengths = dict(old_array_lengths)
            self.variables = dict(old_variables)
            self.variable_types = dict(old_variable_types)
            
            param_types = [self.int_type] * len(node.parameters)
            return_type = self.get_llvm_type(node.return_type)
            self.current_return_type = return_type  # Track current function's return type
            
            func_type = llvmlite.ir.FunctionType(return_type, param_types)
            func = llvmlite.ir.Function(self.module, func_type, name=node.name)
            self.functions[node.name] = func
            block = func.append_basic_block(name="entry")
            self.builder = llvmlite.ir.IRBuilder(block)
            for arg, param in zip(func.args, node.parameters):
                pointer = self.builder.alloca(self.int_type, name=param.name)
                self.builder.store(arg, pointer)
                self.variables[param.name] = pointer
                self.variable_types[param.name] = self.int_type  # Track parameter type
            for statement in node.body.statements:
                self.generate(statement)
            # Add default return if block has no terminator
            if not self.builder.block.is_terminated:
                if return_type == self.void_type:
                    self.builder.ret_void()
                elif return_type == self.bool_type:
                    self.builder.ret(llvmlite.ir.Constant(self.bool_type, 0))
                elif return_type == self.dyn_array_ptr_type:
                    # Return null pointer for arrays (should create empty array instead)
                    self.builder.ret(llvmlite.ir.Constant(self.dyn_array_ptr_type, None))
                else:
                    self.builder.ret(llvmlite.ir.Constant(return_type, 0))
            self.builder = old_builder
            self.variables = old_variables
            self.array_lengths = old_array_lengths
            self.variable_types = old_variable_types
            self.current_return_type = old_current_return_type  # Restore previous return type
        if isinstance(node, ReturnStatement):
            expected_type = getattr(self, 'current_return_type', self.int_type)
            if expected_type == self.void_type:
                return self.builder.ret_void()
            
            # Special handling for array returns - need pointer, not loaded value
            if (expected_type == self.dyn_array_ptr_type and 
                isinstance(node.value, Identifier) and 
                node.value.name in self.array_lengths):
                # Return the array pointer directly
                array_ptr = self.variables[node.value.name]
                return self.builder.ret(array_ptr)
            
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
            if not self.builder.block.is_terminated:
                self.builder.branch(merge_block)

            self.builder.position_at_end(else_block)
            if node.else_body:
                for statement in node.else_body.statements:
                    self.generate(statement)
            if not self.builder.block.is_terminated:
                self.builder.branch(merge_block)
            
            # Always position at merge block and add unreachable
            self.builder.position_at_end(merge_block)
            self.builder.unreachable()
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
        if isinstance(node, TypeofExpression):
            # For now, implement typeof as a simple type check
            # In a full implementation, this would return type info at runtime
            # For now, we'll return string constants for basic types
            arg = self.generate(node.argument)
            
            # Check if argument is a variable and look up its type
            if isinstance(node.argument, Identifier):
                var_name = node.argument.name
                if var_name in self.variable_types:
                    var_type = self.variable_types[var_name]
                    if var_type == self.int_type:
                        type_string = "int"
                    elif var_type == self.i8_ptr_type:
                        type_string = "string" 
                    elif var_type == self.bool_type:
                        type_string = "bool"
                    elif var_type == self.dyn_array_ptr_type:
                        type_string = "array"
                    else:
                        type_string = "unknown"
                else:
                    type_string = "unknown"
            else:
                # For expressions, default to int for now
                type_string = "int"
            
            return self.create_string_constant(type_string)
        if isinstance(node, TypeLiteral):
            # Type literals like "int", "string", etc. used in typeof comparisons
            # For now, treat them as string constants
            return self.create_string_constant(node.name)
        if isinstance(node, ForInStatement):
            func = self.builder.block.parent

            # Evaluate the iterable (could be variable or function call)
            if isinstance(node.iterable, Identifier):
                # Variable reference
                array_ptr = self.variables[node.iterable.name]
                elem_type, elem_size = self.array_lengths[node.iterable.name]
            else:
                # Function call or expression - evaluate to get array
                array_ptr = self.generate(node.iterable)
                # For function returns, assume int elements for now
                elem_type = self.int_type
                elem_size = 4

            index_ptr = self.builder.alloca(self.int_type, name="loop_index")
            self.builder.store(llvmlite.ir.Constant(self.int_type, 0), index_ptr)

            loop_var_ptr = self.builder.alloca(elem_type, name=node.variable)
            self.variables[node.variable] = loop_var_ptr
            self.variable_types[node.variable] = elem_type

            loop_block = func.append_basic_block(name="for_loop")
            body_block = func.append_basic_block(name="for_body")
            exit_block = func.append_basic_block(name="for_exit")

            self.builder.branch(loop_block)

            self.builder.position_at_end(loop_block)
            index = self.builder.load(index_ptr, name="index")
            length = self.array_length(array_ptr)  # Get dynamic length
            condition = self.builder.icmp_signed("<", index, length, name="for_cond")
            self.builder.cbranch(condition, body_block, exit_block)

            self.builder.position_at_end(body_block)

            elem_value = self.array_get(array_ptr, index, elem_type)  # Get element from dynamic array
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