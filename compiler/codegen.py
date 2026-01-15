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
        self.variable_classes = {}  # var_name -> class_name
        self.classes = {}  # class_name -> class_info
        self.module = llvmlite.ir.Module(name="mt_lang")
        self.int_type = llvmlite.ir.IntType(32)
        self.float_type = llvmlite.ir.DoubleType()  # 64-bit double as requested
        self.void_type = llvmlite.ir.VoidType()
        self.i8_ptr_type = llvmlite.ir.IntType(8).as_pointer()
        self.bool_type = llvmlite.ir.IntType(8)
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
        elif type_str == "float":
            return self.float_type
        elif type_str == "void":
            return self.void_type
        elif type_str == "bool":
            return self.bool_type
        elif type_str == "string":
            return self.i8_ptr_type
        elif type_str == "array":
            return self.dyn_array_ptr_type
        else:
            # For user-defined types (classes), use pointer to byte
            return self.i8_ptr_type
    
    def get_field_index(self, class_name, field_name):
        """Get the field index for a given class and field name"""
        if class_name not in self.classes:
            raise Exception(f"Unknown class '{class_name}'")
        
        # Safely get fields with fallback
        class_info = self.classes.get(class_name, {})
        if not class_info:
            raise Exception(f"Class '{class_name}' not properly initialized")
        
        ordered_fields = class_info.get('ordered_fields', [])
        for i, field in enumerate(ordered_fields):
            if field['name'] == field_name:
                return i
        
        raise Exception(f"Field '{field_name}' not found in class '{class_name}'")

    def create_class_structs(self):
        """Create LLVM struct types for all classes"""
        for class_name, class_info in self.classes.items():
            fields = class_info.get('fields', [])
            # Sort fields: constructor args first, then regular fields
            arg_fields = [f for f in fields if f['is_constructor_arg']]
            regular_fields = [f for f in fields if not f['is_constructor_arg']]
            ordered_fields = arg_fields + regular_fields
            field_types = []
            for field in ordered_fields:
                llvm_type = self.get_llvm_type(field['type'])
                field_types.append(llvm_type)
            struct_type = llvmlite.ir.LiteralStructType(field_types)
            # Store the struct type and ordered fields
            self.classes[class_name]['llvm_struct'] = struct_type
            self.classes[class_name]['ordered_fields'] = ordered_fields

    def get_common_type(self, left_type, right_type):
        """Get common type for binary operations with promotion rules"""
        if left_type == self.i8_ptr_type or right_type == self.i8_ptr_type:
            raise Exception("Invalid binary operation on strings")
        # Type precedence: float > int > bool
        if left_type == self.float_type or right_type == self.float_type:
            return self.float_type
        elif left_type == self.int_type or right_type == self.int_type:
            return self.int_type
        else:
            return self.bool_type

    def promote_to_common_type(self, value, target_type):
        """Promote a value to target type"""
        if value.type == target_type:
            return value
        elif value.type == self.int_type and target_type == self.float_type:
            return self.builder.sitofp(value, self.float_type, name="int_to_float")
        elif value.type == self.bool_type and target_type == self.int_type:
            return self.builder.zext(value, self.int_type, name="bool_to_int")
        elif value.type == self.bool_type and target_type == self.float_type:
            # bool -> int -> float
            int_val = self.builder.zext(value, self.int_type, name="bool_to_int")
            return self.builder.sitofp(int_val, self.float_type, name="bool_to_float")
        else:
            raise TypeError(f"Cannot promote {value.type} to {target_type}")

    def promote_to_bool(self, value):
        """Promote any numeric value to boolean"""
        if value.type == self.bool_type:
            return value
        elif value.type == self.int_type:
            return self.builder.icmp_signed("!=", value, llvmlite.ir.Constant(self.int_type, 0), name="int_to_bool")
        elif value.type == self.float_type:
            return self.builder.fcmp_ordered("!=", value, llvmlite.ir.Constant(self.float_type, 0.0), name="float_to_bool")
        else:
            raise TypeError(f"Cannot promote {value.type} to bool")

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
        """Load and compile a module file, returning module name"""
        file_path = self.resolve_module_path(module_path)

        if file_path in self.imported_modules:
            # Already imported, return existing module name
            for name, module_data in self.modules.items():
                if module_data.get('_file_path') == file_path:
                    return name
            # Fallback to generating module name
            if isinstance(module_path, MemberExpression):
                return f"{module_path.object.name}.{module_path.property}"
            else:
                return module_path.name
        
        self.imported_modules.add(file_path)

        # Import tokenizer and parser here to avoid circular imports
        from tokenizer import Tokenizer
        from parser import Parser

        with open(file_path, "r") as f:
            source = f.read()

        tokens = Tokenizer(source, file_path).tokenize()
        ast = Parser(tokens, file_path).parse_program()
        
        # Run semantic analysis on the module
        from semantic import SemanticAnalyzer
        module_analyzer = SemanticAnalyzer(file_path)
        module_analyzer.analyze(ast)
        if module_analyzer.errors:
            for error in module_analyzer.errors:
                print(f"Error: {error}")
            # Continue with compilation despite module errors for now

        # Track functions before and after to know what was added
        before_funcs = set(self.functions.keys())
        
        # Generate code for all function declarations and class methods in module
        for statement in ast.statements:
            if isinstance(statement, (FunctionDeclaration, DynamicFunctionDeclaration)):
                self.generate(statement)
            elif isinstance(statement, ClassDeclaration):
                # Generate LLVM functions for class methods
                for method in statement.methods:
                    self.generate_class_method(statement.name, method)
        
        after_funcs = set(self.functions.keys())
        new_funcs = after_funcs - before_funcs
        
        # Register all new functions under the module name
        if isinstance(module_path, MemberExpression):
            module_name = f"{module_path.object.name}.{module_path.property}"
        else:
            module_name = module_path.name
            
        if module_name not in self.modules:
            self.modules[module_name] = {}
        self.modules[module_name]['_file_path'] = file_path  # Track file path
        for func in new_funcs:
            self.modules[module_name][func] = self.functions[func]
        
        return module_name

    def generate_class_method(self, class_name, method):
        """Generate LLVM function for a class method"""
        old_current_class = getattr(self, 'current_class', None)
        self.current_class = class_name
        old_builder = self.builder
        old_variables = self.variables
        old_array_lengths = self.array_lengths
        old_variable_types = self.variable_types
        old_current_return_type = getattr(self, 'current_return_type', None)

        self.array_lengths = dict(old_array_lengths)
        self.variables = dict(old_variables)
        self.variable_types = dict(old_variable_types)

        # Method parameters: object pointer + declared parameters
        param_types = [self.i8_ptr_type]  # First param is object pointer (this)
        for param in method.params:
            if param.param_type:
                param_types.append(self.get_llvm_type(param.param_type))
            else:
                param_types.append(self.int_type)

        return_type = self.get_llvm_type(method.return_type)
        self.current_return_type = return_type

        # Function name: Class_method
        func_name = f"{class_name}_{method.name}"
        func_type = llvmlite.ir.FunctionType(return_type, param_types)
        func = llvmlite.ir.Function(self.module, func_type, name=func_name)
        self.functions[func_name] = func

        block = func.append_basic_block(name="entry")
        self.builder = llvmlite.ir.IRBuilder(block)

        # Set up parameters: first is 'this', rest are method parameters
        this_param = func.args[0]
        self.variables['this'] = this_param  # Store object pointer
        self.variable_types['this'] = self.i8_ptr_type

        for i, param in enumerate(method.params, 1):
            param_name = param.name
            param_type = param_types[i]
            if param.param_type == "array":
                # For array parameters, store pointer directly
                self.variables[param_name] = func.args[i]
                self.variable_types[param_name] = param_type
                self.array_lengths[param_name] = (self.int_type, 4)  # Default assumption
            else:
                # Allocate space and store parameter
                pointer = self.builder.alloca(param_type, name=param_name)
                self.builder.store(func.args[i], pointer)
                self.variables[param_name] = pointer
                self.variable_types[param_name] = param_type

        # Set current object pointer for field access
        self.current_object_ptr = this_param

        # Generate method body
        for statement in method.body.statements:
            self.generate(statement)

        # Add default return if needed
        if not self.builder.block.is_terminated:
            if return_type == self.void_type:
                self.builder.ret_void()
            elif return_type == self.bool_type:
                self.builder.ret(llvmlite.ir.Constant(self.bool_type, 0))
            elif return_type == self.dyn_array_ptr_type:
                self.builder.ret(llvmlite.ir.Constant(self.dyn_array_ptr_type, None))
            else:
                self.builder.ret(llvmlite.ir.Constant(return_type, 0))

        # Restore state
        self.builder = old_builder
        self.variables = old_variables
        self.array_lengths = old_array_lengths
        self.variable_types = old_variable_types
        self.current_return_type = old_current_return_type
        self.current_class = old_current_class

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

        # String functions
        strlen_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type])
        self.strlen = llvmlite.ir.Function(self.module, strlen_type, "strlen")

        strcpy_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.i8_ptr_type])
        self.strcpy = llvmlite.ir.Function(self.module, strcpy_type, "strcpy")

        strcat_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.i8_ptr_type])
        self.strcat = llvmlite.ir.Function(self.module, strcat_type, "strcat")

        strcmp_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type, self.i8_ptr_type])
        self.strcmp = llvmlite.ir.Function(self.module, strcmp_type, "strcmp")

        sprintf_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type, self.i8_ptr_type], var_arg=True)
        self.sprintf = llvmlite.ir.Function(self.module, sprintf_type, "sprintf")

        # File I/O functions
        # fopen(const char* filename, const char* mode) -> FILE*
        fopen_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.i8_ptr_type])
        self.fopen = llvmlite.ir.Function(self.module, fopen_type, "fopen")

        # fclose(FILE* stream) -> int
        fclose_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type])
        self.fclose = llvmlite.ir.Function(self.module, fclose_type, "fclose")

        # fread(void* buffer, size_t size, size_t count, FILE* stream) -> size_t
        fread_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type, self.int_type, self.int_type, self.i8_ptr_type])
        self.fread = llvmlite.ir.Function(self.module, fread_type, "fread")

        # fwrite(const void* ptr, size_t size, size_t count, FILE* stream) -> size_t
        fwrite_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type, self.int_type, self.int_type, self.i8_ptr_type])
        self.fwrite = llvmlite.ir.Function(self.module, fwrite_type, "fwrite")

        # fputs(const char* s, FILE* stream) -> int
        fputs_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type, self.i8_ptr_type])
        self.fputs = llvmlite.ir.Function(self.module, fputs_type, "fputs")

        # fseek(FILE* stream, long offset, int whence) -> int
        fseek_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type, self.int_type, self.int_type])
        self.fseek = llvmlite.ir.Function(self.module, fseek_type, "fseek")

        # ftell(FILE* stream) -> long
        ftell_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type])
        self.ftell = llvmlite.ir.Function(self.module, ftell_type, "ftell")

        # memcpy(void* dest, const void* src, size_t n) -> void*
        memcpy_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.i8_ptr_type, self.int_type])
        self.memcpy = llvmlite.ir.Function(self.module, memcpy_type, "memcpy")

        # fgets(char* buffer, int size, FILE* stream) -> char*
        fgets_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.int_type, self.i8_ptr_type])
        self.fgets = llvmlite.ir.Function(self.module, fgets_type, "fgets")

        # stdin
        self.stdin = llvmlite.ir.GlobalVariable(self.module, self.i8_ptr_type, "stdin")
        self.stdin.linkage = 'external'

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

    def create_int_constant(self, value: int):
        """Create an integer constant"""
        return llvmlite.ir.Constant(self.int_type, value)
    def generate(self, node):
        if isinstance(node, Program):
            # Create LLVM structs for classes
            self.create_class_structs()
            result = None
            for statement in node.statements:
                result = self.generate(statement)
            return result  # Return the last statement result
        if isinstance(node, ClassDeclaration):
            # Classes are just declarations, don't generate code
            return None
        if isinstance(node, FromImportStatement):
            # Load the module for code generation
            module_name = self.load_module(node.module_path)
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
                tokens = Tokenizer(source, file_path).tokenize()
                ast = Parser(tokens, file_path).parse_program()

                # Run semantic analysis on the module
                from semantic import SemanticAnalyzer
                module_analyzer = SemanticAnalyzer(file_path)
                module_analyzer.analyze(ast)
                if module_analyzer.errors:
                    for error in module_analyzer.errors:
                        print(f"Error: {error}")

                # Process all statements (functions and classes)
                for statement in ast.statements:
                    if isinstance(statement, (FunctionDeclaration, DynamicFunctionDeclaration)):
                        self.generate(statement)
                    elif isinstance(statement, ClassDeclaration):
                        # Register class in symbol table for type checking
                        # We need to make the class available in the current scope
                        # For now, we'll store classes in a separate dictionary
                        if not hasattr(self, 'imported_classes'):
                            self.imported_classes = {}
                        self.imported_classes[statement.name] = statement

                # Track functions before and after to know what was added
                before_funcs = set(self.functions.keys())
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
        if isinstance(node, FloatLiteral):
            return llvmlite.ir.Constant(self.float_type, float(node.value))
        if isinstance(node, NewExpression):
            class_name = node.class_name
            class_info = self.classes.get(class_name)
            if not class_info:
                raise Exception(f"Unknown class '{class_name}'")
            struct_type = class_info['llvm_struct']
            ordered_fields = class_info['ordered_fields']

            # Allocate memory for the struct
            obj_ptr = self.builder.alloca(struct_type, name=f"{class_name}_obj")

            # Initialize constructor arguments
            arg_fields = [f for f in ordered_fields if f['is_constructor_arg']]
            if len(node.arguments) != len(arg_fields):
                raise Exception(f"Wrong number of arguments for {class_name} constructor: expected {len(arg_fields)}, got {len(node.arguments)}")

            for i, arg in enumerate(node.arguments):
                value = self.generate(arg)
                field_ptr = self.builder.gep(obj_ptr, [self.create_int_constant(0), self.create_int_constant(i)], name=f"{arg_fields[i]['name']}_ptr")
                self.builder.store(value, field_ptr)

            # Initialize regular fields with defaults if any (for now, assume no defaults)
            # TODO: handle initializers

            return obj_ptr
        if isinstance(node, BinaryExpression):
            left = self.generate(node.left)
            right = self.generate(node.right)

            # Handle string concatenation
            if node.operator.value == "+" and left.type == self.i8_ptr_type and right.type == self.i8_ptr_type:
                len1 = self.builder.call(self.strlen, [left], name="len1")
                len2 = self.builder.call(self.strlen, [right], name="len2")
                total_len = self.builder.add(len1, len2, name="total_len")
                total_len_plus1 = self.builder.add(total_len, self.create_int_constant(1), name="total_len_plus1")
                new_str = self.builder.call(self.malloc, [total_len_plus1], name="new_str")
                _ = self.builder.call(self.strcpy, [new_str, left], name="copy1")
                _ = self.builder.call(self.strcat, [new_str, right], name="concat")
                return new_str

            # Handle string comparisons specially
            if left.type == self.i8_ptr_type and right.type == self.i8_ptr_type:
                # Use strcmp for string content comparison
                cmp_result = self.builder.call(self.strcmp, [left, right], name="strcmp_result")
                zero = llvmlite.ir.Constant(self.int_type, 0)
                if node.operator.value == "==":
                    return self.builder.icmp_signed("==", cmp_result, zero, name="streq")
                elif node.operator.value == "!=":
                    return self.builder.icmp_signed("!=", cmp_result, zero, name="strne")
                elif node.operator.value == ">":
                    return self.builder.icmp_signed(">", cmp_result, zero, name="strgt")
                elif node.operator.value == "<":
                    return self.builder.icmp_signed("<", cmp_result, zero, name="strlt")
                elif node.operator.value == "<=":
                    return self.builder.icmp_signed("<=", cmp_result, zero, name="strle")
                elif node.operator.value == ">=":
                    return self.builder.icmp_signed(">=", cmp_result, zero, name="strge")
            
            # Determine common type and promote operands
            common_type = self.get_common_type(left.type, right.type)
            left_promoted = self.promote_to_common_type(left, common_type)
            right_promoted = self.promote_to_common_type(right, common_type)
            
            # Arithmetic operations with proper type handling
            if node.operator.value == "+":
                if common_type == self.float_type:
                    return self.builder.fadd(left_promoted, right_promoted, name="faddtmp")
                else:
                    return self.builder.add(left_promoted, right_promoted, name="addtmp")
            elif node.operator.value == "-":
                if common_type == self.float_type:
                    return self.builder.fsub(left_promoted, right_promoted, name="fsubtmp")
                else:
                    return self.builder.sub(left_promoted, right_promoted, name="subtmp")
            elif node.operator.value == "*":
                if common_type == self.float_type:
                    return self.builder.fmul(left_promoted, right_promoted, name="fmultmp")
                else:
                    return self.builder.mul(left_promoted, right_promoted, name="multmp")
            elif node.operator.value == "/":
                if common_type == self.float_type:
                    return self.builder.fdiv(left_promoted, right_promoted, name="fdivtmp")
                else:
                    return self.builder.sdiv(left_promoted, right_promoted, name="divtmp")
            
            # Comparison operations
            elif node.operator.value == "==":
                if common_type == self.float_type:
                    return self.builder.fcmp_ordered("==", left_promoted, right_promoted, name="feqtmp")
                else:
                    return self.builder.icmp_signed("==", left_promoted, right_promoted, name="eqtmp")
            elif node.operator.value == "!=":
                if common_type == self.float_type:
                    return self.builder.fcmp_ordered("!=", left_promoted, right_promoted, name="fnetmp")
                else:
                    return self.builder.icmp_signed("!=", left_promoted, right_promoted, name="netmp")
            elif node.operator.value == ">":
                if common_type == self.float_type:
                    return self.builder.fcmp_ordered(">", left_promoted, right_promoted, name="fgttmp")
                else:
                    return self.builder.icmp_signed(">", left_promoted, right_promoted, name="gttmp")
            elif node.operator.value == "<":
                if common_type == self.float_type:
                    return self.builder.fcmp_ordered("<", left_promoted, right_promoted, name="flttmp")
                else:
                    return self.builder.icmp_signed("<", left_promoted, right_promoted, name="lttmp")
            elif node.operator.value == "<=":
                if common_type == self.float_type:
                    return self.builder.fcmp_ordered("<=", left_promoted, right_promoted, name="fletmp")
                else:
                    return self.builder.icmp_signed("<=", left_promoted, right_promoted, name="letmp")
            elif node.operator.value == ">=":
                if common_type == self.float_type:
                    return self.builder.fcmp_ordered(">=", left_promoted, right_promoted, name="fgetmp")
                else:
                    return self.builder.icmp_signed(">=", left_promoted, right_promoted, name="getmp")
            
            # Logical operations (both operands must be boolean)
            elif node.operator.value == "&&":
                # Promote to bool if needed
                bool_left = self.promote_to_bool(left_promoted)
                bool_right = self.promote_to_bool(right_promoted)
                return self.builder.and_(bool_left, bool_right, name="andtmp")
            elif node.operator.value == "||":
                # Promote to bool if needed
                bool_left = self.promote_to_bool(left_promoted)
                bool_right = self.promote_to_bool(right_promoted)
                return self.builder.or_(bool_left, bool_right, name="ortmp")
        if isinstance(node, VariableDeclaration):
            # Determine the LLVM type for this variable
            llvm_type = self.get_llvm_type(node.type)
            
            # Track the variable's type
            self.variable_types[node.name] = llvm_type

            # Track class variables
            if node.type in self.classes:
                self.variable_classes[node.name] = node.type

            if node.type == "array":
                # Arrays are special - we store the pointer directly
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
                    
                    # Create dynamic array - returns pointer to struct
                    array_ptr, _, _ = self.create_dynamic_array(elem_values, elem_type)
                    self.variables[node.name] = array_ptr  # Store pointer directly
                    self.array_lengths[node.name] = (elem_type, elem_size)
                else:
                    # Array assignment from function call or variable
                    array_ptr = self.generate(node.value)
                    
                    # Type check: make sure the value is actually an array pointer
                    if array_ptr.type != self.dyn_array_ptr_type:
                        raise TypeError(f"Cannot assign {array_ptr.type} to array variable '{node.name}': expected {self.dyn_array_ptr_type}")
                    
                    self.variables[node.name] = array_ptr
                    # For function returns, assume int elements for now (could be improved)
                    self.array_lengths[node.name] = (self.int_type, 4)
            else:
                # For non-array types
                if node.type in ('int', 'float', 'string', 'bool', 'void'):
                    # For primitive types, allocate space on stack
                    pointer = self.builder.alloca(llvm_type, name=node.name)
                    self.variables[node.name] = pointer
                    
                    if node.value:
                        value = self.generate(node.value)

                        # Type check before storing
                        if value.type != llvm_type:
                            raise TypeError(f"Cannot assign {value.type} to variable '{node.name}' of type {llvm_type}: type mismatch")

                        self.builder.store(value, pointer)
                        return None
                else:
                    # For class types, store the object pointer directly (like arrays)
                    if node.value:
                        value = self.generate(node.value)
                        self.variables[node.name] = value  # Store object pointer directly
                        self.variable_types[node.name] = llvm_type
                        self.variable_classes[node.name] = node.type  # Store class name
                    return None
        if isinstance(node, IndexExpression):
            # Handle array indexing: arr[index]
            array = self.generate(node.object)
            index = self.generate(node.index)
            
            # Determine the element type based on the array
            if isinstance(node.object, Identifier):
                elem_type, elem_size = self.array_lengths[node.object.name]
            else:
                # For more complex expressions, assume int elements for now
                elem_type = self.int_type
                elem_size = 4
            
            return self.array_get(array, index, elem_type)
        if isinstance(node, Identifier):
            if node.name not in self.variables:
                raise Exception(f"Variable '{node.name}' not declared")
            
            storage = self.variables[node.name]
            var_type = self.variable_types[node.name]

            # Special handling for arrays - return the pointer directly
            if var_type == self.dyn_array_ptr_type:
                return storage  # Return the pointer directly, no load needed
            else:
                # For non-array types, load from the allocated space
                return self.builder.load(storage, name=node.name)
        if isinstance(node, MemberExpression):
            # Handle field access like obj.field or this.field
            obj = self.generate(node.object)

            # Handle this.field access
            if isinstance(node.object, ThisExpression):
                if 'this' not in self.variables:
                    raise Exception("Cannot access 'this' outside of a method")
                if not hasattr(self, 'current_class'):
                    raise Exception("Current class not set")
                class_name = self.current_class
                struct_type = self.classes[class_name]['llvm_struct']
                obj = self.builder.bitcast(self.variables['this'], struct_type.as_pointer(), name="this_obj")
                field_idx = self.get_field_index(class_name, node.property)
                field_ptr = self.builder.gep(obj, [self.create_int_constant(0), self.create_int_constant(field_idx)], name=f"this_{node.property}_field")
                return self.builder.load(field_ptr, name=node.property)
            elif isinstance(node.object, Identifier):
                # Handle field access on class instances: obj.field
                obj_name = node.object.name
                if obj_name in self.variables and obj_name in self.variable_classes:
                    # Get the object pointer
                    obj_ptr = self.variables[obj_name]
                    class_name = self.variable_classes[obj_name]
                    
                    # Get field index
                    field_idx = self.get_field_index(class_name, node.property)
                    
                    # Access field
                    field_ptr = self.builder.gep(obj_ptr, [self.create_int_constant(0), self.create_int_constant(field_idx)], name=f"{node.property}_field")
                    return self.builder.load(field_ptr, name=node.property)
                else:
                    # Try to handle as array method if it's not a class instance
                    var_type = self.variable_types.get(obj_name)
                    if var_type == self.dyn_array_ptr_type:
                        # Handle array methods (length, append)
                        if node.property == "length":
                            array_ptr = self.variables[obj_name]
                            return self.array_length(array_ptr)
                        elif node.property == "append":
                            # This should be called as method, not field access
                            raise Exception(f"Array append should be called as method: {obj_name}.append(value)")
                        else:
                            raise Exception(f"Unknown array property: {node.property}")
                    else:
                        raise Exception(f"Unknown field '{node.property}' on variable '{obj_name}'")
            else:
                # For other member access, assume it's method call (handled elsewhere)
                raise Exception(f"Field access not implemented for {type(node.object)}.{node.property}")
        if isinstance(node, ThisExpression):
            # Generate the current object pointer
            if hasattr(self, 'current_object_ptr'):
                # For class types, current_object_ptr contains the object pointer directly (i8*)
                return self.current_object_ptr
            else:
                # Fallback - should not happen in normal usage
                raise Exception("'this' used but no object context available")
        if isinstance(node, SetStatement):
            # For now, only handle Identifier targets
            if isinstance(node.target, Identifier):
                name = node.target.name
                if name not in self.variables:
                    raise Exception(f"Variable '{name}' not declared")
                
                # Get the variable's storage and expected type
                storage = self.variables[name]
                expected_type = self.variable_types[name]
                
                # Generate the value
                value = self.generate(node.value)
                
                # Special handling for arrays
                if expected_type == self.dyn_array_ptr_type:
                    # For arrays, we store the pointer directly
                    if value.type != self.dyn_array_ptr_type:
                        raise TypeError(f"Cannot assign {value.type} to array variable '{name}': expected {self.dyn_array_ptr_type}")
                    
                    # For arrays, replace the pointer in variables dict
                    self.variables[name] = value
                    return None  # No store operation needed for arrays
                else:
                    # For non-array types, store into the allocated space
                    if value.type != expected_type:
                        raise TypeError(f"Cannot assign {value.type} to variable '{name}' of type {expected_type}: type mismatch")

                self.builder.store(value, storage)
                return None
            elif isinstance(node.target, MemberExpression):
                obj = node.target.object
                property = node.target.property
                value = self.generate(node.value)
                if isinstance(obj, Identifier):
                    obj_name = obj.name
                    if obj_name in self.variable_classes:
                        class_name = self.variable_classes[obj_name]
                        field_idx = self.get_field_index(class_name, property)
                        obj_ptr = self.variables[obj_name]
                        field_ptr = self.builder.gep(obj_ptr, [self.create_int_constant(0), self.create_int_constant(field_idx)], name=f"{property}_ptr")
                        self.builder.store(value, field_ptr)
                        return None
                elif isinstance(obj, ThisExpression):
                    if not hasattr(self, 'current_class'):
                        raise Exception("'this' used outside method")
                    class_name = self.current_class
                    struct_type = self.classes[class_name]['llvm_struct']
                    obj_ptr = self.builder.bitcast(self.variables['this'], struct_type.as_pointer(), name="this_obj")
                    field_idx = self.get_field_index(class_name, property)
                    field_ptr = self.builder.gep(obj_ptr, [self.create_int_constant(0), self.create_int_constant(field_idx)], name=f"this_{property}_ptr")
                    self.builder.store(value, field_ptr)
                    return None
                else:
                    raise Exception(f"Unsupported member assignment: {node.target}")
            else:
                raise Exception(f"Unsupported assignment target: {node.target}")
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

                # Check if it's a method call on a class instance
                if obj_name in self.variable_classes:
                    class_name = self.variable_classes[obj_name]
                    func_name = f"{class_name}_{method_name}"
                    if func_name not in self.functions:
                        raise Exception(f"Method '{method_name}' not found in class '{class_name}'")
                    func = self.functions[func_name]
                    obj_ptr = self.variables[obj_name]
                    # Cast to i8* for the first argument
                    obj_i8_ptr = self.builder.bitcast(obj_ptr, self.i8_ptr_type, name="obj_i8")
                    args = [obj_i8_ptr] + [self.generate(arg) for arg in node.arguments]
                    return self.builder.call(func, args)

                if obj_name in self.variables:
                    # Set the current object context for field access
                    # For class types, self.variables[obj_name] contains the object pointer directly (i8*)
                    self.current_object_ptr = self.variables[obj_name]

                    # Implement method dispatch
                    if method_name == "open" and len(node.arguments) == 2:
                        # File.open(fpath, fmode) - print field values
                        path_expr = MemberExpression(ThisExpression(), "path")
                        print_call = CallExpression(Identifier("print"), [path_expr])
                        self.generate(print_call)
                        mode_expr = MemberExpression(ThisExpression(), "mode")
                        print_call2 = CallExpression(Identifier("print"), [mode_expr])
                        self.generate(print_call2)
                        return None


                    else:
                        # Check if there's a generated function for this class method
                        class_name = self.variable_classes.get(obj_name, obj_name.capitalize())
                        method_func_name = f"{class_name}_{method_name}"
                        if method_func_name in self.functions:
                            # Call the generated method function
                            func = self.functions[method_func_name]
                            # Arguments: object pointer + method arguments
                            args = [self.current_object_ptr]
                            for arg in node.arguments:
                                args.append(self.generate(arg))
                            return self.builder.call(func, args)
                        else:
                            print(f"DEBUG: Method {method_func_name} not found")
                            # Unknown method - just evaluate arguments
                            for arg in node.arguments:
                                self.generate(arg)
                            return None

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

                # Determine the type of the argument to choose format string
                if isinstance(arg, StringLiteral):
                    format_ptr = self.create_string_constant("%s\n")
                elif isinstance(arg, FloatLiteral):
                    format_ptr = self.create_string_constant("%f\n")
                elif isinstance(arg, NumberLiteral):
                    format_ptr = self.create_string_constant("%d\n")
                elif isinstance(arg, Identifier):
                    var_type = self.variable_types.get(arg.name)
                    if var_type == self.i8_ptr_type:
                        format_ptr = self.create_string_constant("%s\n")
                    elif var_type == self.float_type:
                        format_ptr = self.create_string_constant("%f\n")
                    else:
                        format_ptr = self.create_string_constant("%d\n")
                else:
                    # For expressions, generate first and check type
                    value = self.generate(arg)
                    if value.type == self.float_type:
                        format_ptr = self.create_string_constant("%f\n")
                    elif value.type == self.i8_ptr_type:
                        format_ptr = self.create_string_constant("%s\n")
                    else:
                        format_ptr = self.create_string_constant("%d\n")
                    return self.builder.call(self.printf, [format_ptr, value])

                # Generate the value for non-expression arguments
                value = self.generate(arg)
                return self.builder.call(self.printf, [format_ptr, value])
            elif isinstance(node.callee, Identifier) and node.callee.name == "read":
                # read([prompt]) - read user input as string, optionally with prompt
                if len(node.arguments) > 1:
                    raise Exception("read() takes 0 or 1 arguments")

                if len(node.arguments) == 1:
                    # Print the prompt
                    prompt_arg = node.arguments[0]
                    prompt_value = self.generate(prompt_arg)
                    if prompt_value.type == self.i8_ptr_type:
                        format_ptr = self.create_string_constant("%s")
                    elif prompt_value.type == self.int_type:
                        format_ptr = self.create_string_constant("%d")
                    elif prompt_value.type == self.float_type:
                        format_ptr = self.create_string_constant("%f")
                    else:
                        format_ptr = self.create_string_constant("%s")
                    _ = self.builder.call(self.printf, [format_ptr, prompt_value])

                buffer = self.builder.call(self.malloc, [self.create_int_constant(1024)], name="input_buffer")
                stdin_val = self.builder.load(self.stdin, name="stdin_val")
                _ = self.builder.call(self.fgets, [buffer, self.create_int_constant(1024), stdin_val], name="fgets_call")

                # Strip trailing newline
                len_val = self.builder.call(self.strlen, [buffer], name="input_len")
                len_minus_1 = self.builder.sub(len_val, self.create_int_constant(1), name="len_minus_1")
                newline_ptr = self.builder.gep(buffer, [len_minus_1], name="newline_ptr")
                newline_char = self.builder.load(newline_ptr, name="newline_char")
                newline_const = self.create_int_constant(10)  # '\n'
                is_newline = self.builder.icmp_signed("==", newline_char, newline_const, name="is_newline")
                with self.builder.if_then(is_newline):
                    zero = llvmlite.ir.Constant(self.bool_type, 0)
                    self.builder.store(zero, newline_ptr)

                return buffer
            elif isinstance(node.callee, Identifier) and node.callee.name == "str":
                if len(node.arguments) != 1:
                    raise Exception("str() takes 1 argument")
                arg = self.generate(node.arguments[0])
                if arg.type == self.int_type:
                    buffer = self.builder.call(self.malloc, [self.create_int_constant(32)], name="str_buffer")
                    format_str = self.create_string_constant("%d", as_constant=True)
                    _ = self.builder.call(self.sprintf, [buffer, format_str, arg], name="sprintf_call")
                    return buffer
                else:
                    raise Exception("str() only supports int for now")
            elif isinstance(node.callee, Identifier) and node.callee.name == "fopen":
                # fopen(const char* filename, const char* mode) -> FILE*
                if len(node.arguments) != 2:
                    raise Exception("fopen requires 2 arguments")
                filename = self.generate(node.arguments[0])
                mode = self.generate(node.arguments[1])
                return self.builder.call(self.fopen, [filename, mode])
            elif isinstance(node.callee, Identifier) and node.callee.name == "fclose":
                # fclose(FILE* stream) -> int
                if len(node.arguments) != 1:
                    raise Exception("fclose requires 1 argument")
                stream = self.generate(node.arguments[0])
                return self.builder.call(self.fclose, [stream])
            elif isinstance(node.callee, Identifier) and node.callee.name == "fread":
                # fread(void* buffer, size_t size, size_t count, FILE* stream) -> size_t
                if len(node.arguments) != 4:
                    raise Exception("fread requires 4 arguments")
                buffer = self.generate(node.arguments[0])
                size = self.generate(node.arguments[1])
                count = self.generate(node.arguments[2])
                stream = self.generate(node.arguments[3])
                return self.builder.call(self.fread, [buffer, size, count, stream])
            elif isinstance(node.callee, Identifier) and node.callee.name == "fseek":
                # fseek(FILE* stream, long offset, int whence) -> int
                if len(node.arguments) != 3:
                    raise Exception("fseek requires 3 arguments")
                stream = self.generate(node.arguments[0])
                offset = self.generate(node.arguments[1])
                whence = self.generate(node.arguments[2])
                return self.builder.call(self.fseek, [stream, offset, whence])
            elif isinstance(node.callee, Identifier) and node.callee.name == "ftell":
                # ftell(FILE* stream) -> long
                if len(node.arguments) != 1:
                    raise Exception("ftell requires 1 argument")
                stream = self.generate(node.arguments[0])
                return self.builder.call(self.ftell, [stream])
            elif isinstance(node.callee, Identifier) and node.callee.name == "malloc":
                # malloc(size_t size) -> void*
                if len(node.arguments) != 1:
                    raise Exception("malloc requires 1 argument")
                size = self.generate(node.arguments[0])
                return self.builder.call(self.malloc, [size])
            elif isinstance(node.callee, Identifier) and node.callee.name == "free":
                # free(void* ptr)
                if len(node.arguments) != 1:
                    raise Exception("free requires 1 argument")
                ptr = self.generate(node.arguments[0])
                return self.builder.call(self.free, [ptr])
            elif isinstance(node.callee, Identifier) and node.callee.name == "fputs":
                # fputs(const char* s, FILE* stream)
                if len(node.arguments) != 2:
                    raise Exception("fputs requires 2 arguments")
                s = self.generate(node.arguments[0])
                stream = self.generate(node.arguments[1])
                return self.builder.call(self.fputs, [s, stream])
            else:
                # First check if function exists directly
                func = self.functions.get(node.callee.name)
                
                # If not found directly, check imported modules
                if func is None:
                    for module_name, module_funcs in self.modules.items():
                        if node.callee.name in module_funcs:
                            func = module_funcs[node.callee.name]
                            break
                
                if func is None:
                    raise Exception(f"Unknown function: {node.callee.name}")
                
                args = []
                for arg in node.arguments:
                    args.append(self.generate(arg))
                call_result = self.builder.call(func, args)
                
                # Check if this is a dynamic function (returns struct with type tag)
                if hasattr(func.function_type.return_type, 'elements'):
                    # Extract type tag and value from dynamic return
                    type_tag = self.builder.extract_value(call_result, 0)
                    value = self.builder.extract_value(call_result, 1)
                    
                    # For now, return just the value (could be enhanced for runtime type handling)
                    return value
                else:
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
            
            # Use parameter types instead of defaulting to int
            param_types = []
            for param in node.parameters:
                if param.param_type:
                    param_types.append(self.get_llvm_type(param.param_type))
                else:
                    param_types.append(self.int_type)  # Default to int if no type specified
            
            return_type = self.get_llvm_type(node.return_type)
            self.current_return_type = return_type  # Track current function's return type
            
            func_type = llvmlite.ir.FunctionType(return_type, param_types)
            func = llvmlite.ir.Function(self.module, func_type, name=node.name)
            self.functions[node.name] = func
            block = func.append_basic_block(name="entry")
            self.builder = llvmlite.ir.IRBuilder(block)
            for arg, param in zip(func.args, node.parameters):
                # Use the parameter's actual type for allocation
                if param.param_type:
                    llvm_type = self.get_llvm_type(param.param_type)
                else:
                    llvm_type = self.int_type
                
                if param.param_type == "array":
                    # For array parameters, store the pointer directly without allocation
                    self.variables[param.name] = arg
                    self.variable_types[param.name] = llvm_type
                    self.array_lengths[param.name] = (self.int_type, 4)  # Default to int elements
                else:
                    # For non-array types, allocate space and store the value
                    pointer = self.builder.alloca(llvm_type, name=param.name)
                    self.builder.store(arg, pointer)
                    self.variables[param.name] = pointer
                    self.variable_types[param.name] = llvm_type  # Track parameter type
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
        if isinstance(node, DynamicFunctionDeclaration):
            old_builder = self.builder
            old_variables = self.variables
            old_array_lengths = self.array_lengths
            old_variable_types = self.variable_types
            old_current_return_type = getattr(self, 'current_return_type', None)
            
            self.array_lengths = dict(old_array_lengths)
            self.variables = dict(old_variables)
            self.variable_types = dict(old_variable_types)
            
            # Create parameter types from annotations
            param_types = []
            for param in node.parameters:
                if param.param_type:
                    param_types.append(self.get_llvm_type(param.param_type))
                else:
                    param_types.append(self.int_type)  # Default fallback
            
            # Use a tagged union approach for dynamic returns:
            # struct { i8 type_tag; union { i32 int_val; double float_val; } value; }
            return_struct_type = llvmlite.ir.LiteralStructType([
                self.int_type,           # type tag (0=int, 1=float, 2=bool, 3=string, 4=array, 5=void)
                self.int_type            # value (use largest type that can hold others)
            ])
            
            func_type = llvmlite.ir.FunctionType(return_struct_type, param_types)
            func = llvmlite.ir.Function(self.module, func_type, name=node.name)
            self.functions[node.name] = func
            block = func.append_basic_block(name="entry")
            self.builder = llvmlite.ir.IRBuilder(block)
            
            # Track that we're in a dynamic function
            self.current_return_type = return_struct_type
            
            # Allocate space for parameters with correct types
            for arg, param in zip(func.args, node.parameters):
                param_type = self.get_llvm_type(param.param_type) if param.param_type else self.int_type
                pointer = self.builder.alloca(param_type, name=param.name)
                self.builder.store(arg, pointer)
                self.variables[param.name] = pointer
                self.variable_types[param.name] = param_type
            
            # Generate function body
            for statement in node.body.statements:
                self.generate(statement)
            
            # Add default return if block has no terminator
            if not self.builder.block.is_terminated:
                # Return default int 0
                type_tag = llvmlite.ir.Constant(self.int_type, 0)  # int type
                value = llvmlite.ir.Constant(self.int_type, 0)     # int value 0
                return_val = llvmlite.ir.Constant(return_struct_type, [type_tag, value])
                self.builder.ret(return_val)
            
            self.builder = old_builder
            self.variables = old_variables
            self.array_lengths = old_array_lengths
            self.variable_types = old_variable_types
            self.current_return_type = old_current_return_type
            self.variables = old_variables
            self.array_lengths = old_array_lengths
            self.variable_types = old_variable_types
            self.current_return_type = old_current_return_type  # Restore previous return type
        if isinstance(node, ReturnStatement):
            expected_type = getattr(self, 'current_return_type', self.int_type)
            
            # Check if we're in a dynamic function
            if hasattr(self, 'current_return_type') and hasattr(expected_type, 'elements'):
                # Dynamic function with tagged union return
                if node.value is None:
                    # Return void/default value
                    type_tag = llvmlite.ir.Constant(self.int_type, 5)  # void type
                    value = llvmlite.ir.Constant(self.int_type, 0)
                    return_val = llvmlite.ir.Constant(expected_type, [type_tag, value])
                    return self.builder.ret(return_val)
                
                # Generate the value and determine its type
                value = self.generate(node.value)
                
                # Determine type tag based on LLVM type
                if value.type == self.int_type:
                    type_tag = llvmlite.ir.Constant(self.int_type, 0)  # int
                elif value.type == self.float_type:
                    type_tag = llvmlite.ir.Constant(self.int_type, 1)  # float
                    # Convert float to int for storage
                    value = self.builder.fptosi(value, self.int_type)
                elif value.type == self.bool_type:
                    type_tag = llvmlite.ir.Constant(self.int_type, 2)  # bool
                    value = self.builder.zext(value, self.int_type)
                else:
                    type_tag = llvmlite.ir.Constant(self.int_type, 0)  # default to int
                    if value.type != self.int_type:
                        value = self.builder.ptrtoint(value, self.int_type)
                
                # Create return struct by inserting into undef-like value
                # First create a zero-initialized struct, then build it up
                zero_val = llvmlite.ir.Constant(self.int_type, 0)
                temp_struct = llvmlite.ir.Constant(expected_type, [zero_val, zero_val])
                
                # Use insert_value instructions to modify it properly
                return_val = self.builder.insert_value(temp_struct, type_tag, 0)
                return_val = self.builder.insert_value(return_val, value, 1)
                return self.builder.ret(return_val)
            
            # Original static function handling
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
            
            # Position at merge block - only add unreachable if both branches terminated
            self.builder.position_at_end(merge_block)
            then_terminated = not then_block.is_terminated
            else_terminated = not else_block.is_terminated if node.else_body else True
            
            # If both branches are terminated (e.g., both have return), add unreachable
            if then_terminated and else_terminated:
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
            return llvmlite.ir.Constant(llvmlite.ir.IntType(8), 1 if node.value else 0)
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
                # Variable reference - use Identifier generation to get proper value
                array_ptr = self.generate(node.iterable)
                elem_type, elem_size = self.array_lengths[node.iterable.name]
            else:
                # Function call or expression - evaluate to get array
                array_ptr = self.generate(node.iterable)
                # For function returns, assume int elements for now
                elem_type = self.int_type
                elem_size = 4

            # Allocate loop index and loop variable
            index_ptr = self.builder.alloca(self.int_type, name="loop_index")
            self.builder.store(llvmlite.ir.Constant(self.int_type, 0), index_ptr)

            loop_var_ptr = self.builder.alloca(elem_type, name=node.variable)
            self.variables[node.variable] = loop_var_ptr
            self.variable_types[node.variable] = elem_type

            # Create basic blocks for the loop structure
            loop_block = func.append_basic_block(name="for_loop")
            body_block = func.append_basic_block(name="for_body")
            exit_block = func.append_basic_block(name="for_exit")

            # Entry: branch to loop condition check
            self.builder.branch(loop_block)

            # Loop condition block: check index < length
            self.builder.position_at_end(loop_block)
            index = self.builder.load(index_ptr, name="index")
            length = self.array_length(array_ptr)  # Get dynamic length
            condition = self.builder.icmp_signed("<", index, length, name="for_cond")
            self.builder.cbranch(condition, body_block, exit_block)

            # Loop body block: execute loop statements
            self.builder.position_at_end(body_block)
            
            # Get current element and store in loop variable
            elem_value = self.array_get(array_ptr, index, elem_type)
            self.builder.store(elem_value, loop_var_ptr)

            # Save current state for cleanup
            old_variables = dict(self.variables)
            old_variable_types = dict(self.variable_types)
            old_array_lengths = dict(self.array_lengths)

            # Generate loop body statements
            for statement in node.body.statements:
                self.generate(statement)
                
                # If the body contains a return/break, the block is already terminated
                if self.builder.block.is_terminated:
                    break

            # Only increment and branch if block is not terminated
            if not self.builder.block.is_terminated:
                index_val = self.builder.load(index_ptr, name="index_val")
                next_index = self.builder.add(index_val, llvmlite.ir.Constant(self.int_type, 1), name="next_index")
                self.builder.store(next_index, index_ptr)
                self.builder.branch(loop_block)

            # Restore state and continue to exit block
            self.variables = old_variables
            self.variable_types = old_variable_types  
            self.array_lengths = old_array_lengths
            
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
    if result is None:
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