import llvmlite.ir
import os
from ast_nodes import *

# Registry of libc functions available for import
# Format: "name": {"ret": return_type, "args": [arg_types], "var_arg": bool}
# Types: "int", "ptr", "void", "float"
LIBC_FUNCTIONS = {
    # Memory management
    "malloc": {"ret": "ptr", "args": ["int"]},
    "calloc": {"ret": "ptr", "args": ["int", "int"]},
    "realloc": {"ret": "ptr", "args": ["ptr", "int"]},
    "free": {"ret": "void", "args": ["ptr"]},
    "memcpy": {"ret": "ptr", "args": ["ptr", "ptr", "int"]},
    "memset": {"ret": "ptr", "args": ["ptr", "int", "int"]},

    # String functions
    "strlen": {"ret": "int", "args": ["ptr"]},
    "strcpy": {"ret": "ptr", "args": ["ptr", "ptr"]},
    "strncpy": {"ret": "ptr", "args": ["ptr", "ptr", "int"]},
    "strcat": {"ret": "ptr", "args": ["ptr", "ptr"]},
    "strcmp": {"ret": "int", "args": ["ptr", "ptr"]},
    "strncmp": {"ret": "int", "args": ["ptr", "ptr", "int"]},
    "strstr": {"ret": "ptr", "args": ["ptr", "ptr"]},
    "strchr": {"ret": "ptr", "args": ["ptr", "int"]},
    "sprintf": {"ret": "int", "args": ["ptr", "ptr"], "var_arg": True},
    "snprintf": {"ret": "int", "args": ["ptr", "int", "ptr"], "var_arg": True},
    "tolower": {"ret": "int", "args": ["int"]},
    "toupper": {"ret": "int", "args": ["int"]},

    # File I/O
    "fopen": {"ret": "ptr", "args": ["ptr", "ptr"]},
    "fclose": {"ret": "int", "args": ["ptr"]},
    "fread": {"ret": "int", "args": ["ptr", "int", "int", "ptr"]},
    "fwrite": {"ret": "int", "args": ["ptr", "int", "int", "ptr"]},
    "fgets": {"ret": "ptr", "args": ["ptr", "int", "ptr"]},
    "fputs": {"ret": "int", "args": ["ptr", "ptr"]},
    "fseek": {"ret": "int", "args": ["ptr", "int", "int"]},
    "ftell": {"ret": "int", "args": ["ptr"]},
    "fflush": {"ret": "int", "args": ["ptr"]},
    "feof": {"ret": "int", "args": ["ptr"]},
    "ferror": {"ret": "int", "args": ["ptr"]},

    # File system
    "access": {"ret": "int", "args": ["ptr", "int"]},
    "remove": {"ret": "int", "args": ["ptr"]},
    "rename": {"ret": "int", "args": ["ptr", "ptr"]},
    "stat": {"ret": "int", "args": ["ptr", "ptr"]},
    "mkdir": {"ret": "int", "args": ["ptr", "int"]},
    "rmdir": {"ret": "int", "args": ["ptr"]},
    "getcwd": {"ret": "ptr", "args": ["ptr", "int"]},
    "chdir": {"ret": "int", "args": ["ptr"]},
    "unlink": {"ret": "int", "args": ["ptr"]},

    # Standard I/O
    "printf": {"ret": "int", "args": ["ptr"], "var_arg": True},
    "scanf": {"ret": "int", "args": ["ptr"], "var_arg": True},
    "puts": {"ret": "int", "args": ["ptr"]},
    "getchar": {"ret": "int", "args": []},
    "putchar": {"ret": "int", "args": ["int"]},

    # Process/system
    "exit": {"ret": "void", "args": ["int"]},
    "abort": {"ret": "void", "args": []},
    "system": {"ret": "int", "args": ["ptr"]},
    "getenv": {"ret": "ptr", "args": ["ptr"]},

    # Math (from math.h, but commonly linked)
    "abs": {"ret": "int", "args": ["int"]},
    "atoi": {"ret": "int", "args": ["ptr"]},
    "atof": {"ret": "float", "args": ["ptr"]},
    "floor": {"ret": "float", "args": ["float"]},
    "ceil": {"ret": "float", "args": ["float"]},
    "round": {"ret": "float", "args": ["float"]},
    "sqrt": {"ret": "float", "args": ["float"]},
    "pow": {"ret": "float", "args": ["float", "float"]},
    "fabs": {"ret": "float", "args": ["float"]},
    "fmod": {"ret": "float", "args": ["float", "float"]},
    "log": {"ret": "float", "args": ["float"]},
    "log10": {"ret": "float", "args": ["float"]},
    "exp": {"ret": "float", "args": ["float"]},
    "sin": {"ret": "float", "args": ["float"]},
    "cos": {"ret": "float", "args": ["float"]},
    "tan": {"ret": "float", "args": ["float"]},
    "asin": {"ret": "float", "args": ["float"]},
    "acos": {"ret": "float", "args": ["float"]},
    "atan": {"ret": "float", "args": ["float"]},

    # Random
    "rand": {"ret": "int", "args": []},
    "srand": {"ret": "void", "args": ["int"]},

    # Time
    "time": {"ret": "int", "args": ["ptr"]},
    "sleep": {"ret": "int", "args": ["int"]},
}

# Exception type tags for runtime type checking
EXCEPTION_TYPE_TAGS = {
    "Exception": 0,
    "ValueError": 1,
    "TypeError": 2,
    "NullPointerError": 3,
    "RuntimeError": 4,
    "IndexError": 5,
}

class CodeGenerator:
    def __init__(self, source_dir=None):
        self.variables = {}
        self.variable_types = {}
        self.functions = {}
        self.array_lengths = {}
        self.modules = {}  # alias -> {func_name: llvm_func}
        self.variable_classes = {}  # var_name -> class_name
        self.array_element_classes = {}  # array_var_name -> element_class_name
        self.classes = {}  # class_name -> class_info
        self.module = llvmlite.ir.Module(name="mt_lang")
        self.int_type = llvmlite.ir.IntType(32)
        self.float_type = llvmlite.ir.DoubleType()  # 64-bit double as requested
        self.void_type = llvmlite.ir.VoidType()
        self.i8_type = llvmlite.ir.IntType(8)
        self.i8_ptr_type = self.i8_type.as_pointer()
        self.i1_type = llvmlite.ir.IntType(1)
        self.bool_type = self.i1_type  # Use i1 for bool (proper 1-bit type)
        self.string_counter = 0
        self.bounds_check_counter = 0
        self.source_dir = source_dir if source_dir else os.getcwd()
        self.stdlib_path = os.path.join(os.path.dirname(__file__), "stdlib")
        self.imported_modules = set()  # Track which modules we've already imported
        self.libc_functions = {}  # Track imported libc functions: name -> llvm_function

        # Dynamic array struct: { i32 length, i32 capacity, i8* data }
        self.dyn_array_type = llvmlite.ir.LiteralStructType([
            self.int_type,      # length
            self.int_type,      # capacity
            self.i8_ptr_type    # data pointer
        ])
        self.dyn_array_ptr_type = self.dyn_array_type.as_pointer()
        
        # Exception type: { i32 type_tag, i8* message }
        self.exc_type = llvmlite.ir.LiteralStructType([self.int_type, self.i8_ptr_type])

        # Track global arrays: name -> LLVM GlobalVariable (pointer to dyn_array_ptr)
        self.global_arrays = {}
        self.in_main_function = False  # Track whether we're generating main function code

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

    def get_element_llvm_type(self, element_type_str):
        """Get LLVM type and size for array element types. Returns (llvm_type, size, class_name)"""
        if element_type_str == "int":
            return self.int_type, 4, None
        elif element_type_str == "float":
            return self.float_type, 8, None
        elif element_type_str == "string":
            return self.i8_ptr_type, 8, None
        elif element_type_str == "bool":
            return self.bool_type, 1, None
        else:
            # For user-defined class types (or unknown types), use pointer
            # Classes are stored as i8* pointers
            return self.i8_ptr_type, 8, element_type_str
    
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

    def get_type_size(self, llvm_type):
        """Get the size of an LLVM type in bytes (returns LLVM IR value)"""
        if isinstance(llvm_type, llvmlite.ir.IntType):
            return llvmlite.ir.Constant(self.int_type, llvm_type.width // 8)
        elif isinstance(llvm_type, llvmlite.ir.PointerType):
            return llvmlite.ir.Constant(self.int_type, 8)  # 64-bit pointers
        elif isinstance(llvm_type, llvmlite.ir.FloatType):
            return llvmlite.ir.Constant(self.int_type, 4)
        elif isinstance(llvm_type, llvmlite.ir.DoubleType):
            return llvmlite.ir.Constant(self.int_type, 8)
        elif isinstance(llvm_type, llvmlite.ir.ArrayType):
            element_size = self.get_type_size(llvm_type.element)
            return llvmlite.ir.Constant(self.int_type, llvm_type.count * 8)  # Simplification: assume 8-byte elements
        elif isinstance(llvm_type, llvmlite.ir.LiteralStructType):
            # For structs, calculate total size assuming 8 bytes per field (pointer size)
            return llvmlite.ir.Constant(self.int_type, len(llvm_type.elements) * 8)
        else:
            # Default fallback
            return llvmlite.ir.Constant(self.int_type, 8)

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
        print(f"DEBUG: resolve_module_path called with: {type(module_path).__name__} = {module_path}")
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

            print(f"DEBUG: parts = {parts}")
            
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
            # Simple name like "math" - check local first, then stdlib
            print(f"DEBUG: Simple identifier: {module_path.name}")
            rel_path = module_path.name + ".mtc"
            # Check local source dir first
            file_path = os.path.join(self.source_dir, rel_path)
            if not os.path.exists(file_path):
                # Check stdlib
                stdlib_file_path = os.path.join(self.stdlib_path, rel_path)
                if os.path.exists(stdlib_file_path):
                    file_path = stdlib_file_path
                else:
                    file_path = os.path.join(self.source_dir, rel_path)  # Keep original for error
        else:
            rel_path = str(module_path) + ".mtc"
            file_path = os.path.join(self.source_dir, rel_path)

        print(f"DEBUG: file_path = {file_path}")
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
        before_classes = set(self.classes.keys())
        before_vars = set(self.variables.keys())
        
        # Save and set in_main_function to True so module-level variables are global
        old_in_main_function = self.in_main_function
        self.in_main_function = True
        
        # Generate code for all statements in module
        for statement in ast.statements:
            if isinstance(statement, LibcImportStatement):
                # Process libc imports first to declare functions
                self.generate(statement)
            elif isinstance(statement, FromImportStatement):
                # Process imports to declare functions like range, array_contains, etc.
                self.generate(statement)
            elif isinstance(statement, (FunctionDeclaration, DynamicFunctionDeclaration)):
                self.generate(statement)
            elif isinstance(statement, ClassDeclaration):
                # Generate LLVM functions for class methods
                for method in statement.methods:
                    self.generate_class_method(statement.name, method)
            elif isinstance(statement, VariableDeclaration):
                # Generate code for module-level variable declarations
                self.generate(statement)
            elif isinstance(statement, SimpleImportStatement):
                # Load and generate code for the entire module
                self.generate(statement)
        
        # Restore in_main_function
        self.in_main_function = old_in_main_function
        
        after_funcs = set(self.functions.keys())
        after_classes = set(self.classes.keys())
        after_vars = set(self.variables.keys())
        
        new_funcs = after_funcs - before_funcs
        new_classes = after_classes - before_classes
        new_vars = after_vars - before_vars
        
        # Register all new items under the module name
        if isinstance(module_path, MemberExpression):
            module_name = f"{module_path.object.name}.{module_path.property}"
        else:
            module_name = module_path.name
            
        if module_name not in self.modules:
            self.modules[module_name] = {}
        self.modules[module_name]['_file_path'] = file_path  # Track file path
        for func in new_funcs:
            self.modules[module_name][func] = self.functions[func]
        for cls in new_classes:
            self.modules[module_name][cls] = self.classes[cls]
        for var in new_vars:
            self.modules[module_name][var] = self.variables[var]
        
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
        old_in_main_function = self.in_main_function

        self.in_main_function = False
        self.array_lengths = dict(old_array_lengths)
        self.variables = dict(old_variables)
        self.variable_types = dict(old_variable_types)

        # Remove global arrays from local variables - they'll be accessed via global_arrays
        for name in list(self.global_arrays.keys()):
            if name in self.variables:
                del self.variables[name]

        # Get the struct type for this class - use struct pointer as first param to match method calls
        struct_type = self.classes[class_name]['llvm_struct']
        param_types = [struct_type.as_pointer()]  # First param is struct pointer (this)
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
        self.variables['this'] = this_param  # Store struct pointer
        self.variable_types['this'] = struct_type.as_pointer()

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
            elif isinstance(return_type, llvmlite.ir.PointerType):
                # For pointer types, return null
                self.builder.ret(llvmlite.ir.Constant(return_type, None))
            else:
                self.builder.ret(llvmlite.ir.Constant(return_type, 0))

        # Restore state
        self.builder = old_builder
        self.variables = old_variables
        self.array_lengths = old_array_lengths
        self.variable_types = old_variable_types
        self.current_return_type = old_current_return_type
        self.current_class = old_current_class
        self.in_main_function = old_in_main_function

    def create_main_function(self):
        func_type = llvmlite.ir.FunctionType(self.int_type, [])
        func = llvmlite.ir.Function(self.module, func_type, name="main")
        block = func.append_basic_block(name="entry")
        self.builder = llvmlite.ir.IRBuilder(block)
        self.in_main_function = True
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

        # fgets(char* buffer, int size, FILE* stream) -> char*
        fgets_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.int_type, self.i8_ptr_type])
        self.fgets = llvmlite.ir.Function(self.module, fgets_type, "fgets")

        # stdin
        self.stdin = llvmlite.ir.GlobalVariable(self.module, self.i8_ptr_type, "stdin")
        self.stdin.linkage = 'external'

        # strstr(const char* haystack, const char* needle) -> char*
        strstr_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.i8_ptr_type])
        self.strstr = llvmlite.ir.Function(self.module, strstr_type, "strstr")
        
        # setjmp/longjmp for exception handling
        # jmp_buf is typically an array of ints (size varies by platform, use 16 for x86-64)
        # On x86-64 Linux, jmp_buf is typically [8 x i64] but we'll use i8* for compatibility
        # The actual structure is platform-specific and handled by libc
        # jmp_buf needs to be large enough for the platform
        # On x86_64 Linux, jmp_buf is about 200 bytes, so use 64 x i64 (512 bytes) to be safe
        self.jmp_buf_type = llvmlite.ir.ArrayType(llvmlite.ir.IntType(64), 64)
        self.jmp_buf_ptr_type = llvmlite.ir.PointerType(self.jmp_buf_type)
        
        # Use i8* (void*) for setjmp/longjmp since jmp_buf is opaque to us
        # The actual layout is platform-specific and handled by libc
        setjmp_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type], var_arg=False)
        self.setjmp = llvmlite.ir.Function(self.module, setjmp_type, "setjmp")
        
        longjmp_type = llvmlite.ir.FunctionType(self.void_type, [self.i8_ptr_type, self.int_type], var_arg=False)
        self.longjmp = llvmlite.ir.Function(self.module, longjmp_type, "longjmp")
        
        # memcpy for clearing jump buffer
        memcpy_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.i8_ptr_type, self.int_type], var_arg=False)
        self.memcpy = llvmlite.ir.Function(self.module, memcpy_type, "memcpy")
        
        # memset for initializing jump buffer
        memset_type = llvmlite.ir.FunctionType(self.i8_ptr_type, [self.i8_ptr_type, self.int_type, self.int_type], var_arg=False)
        self.memset = llvmlite.ir.Function(self.module, memset_type, "memset")
        
        # memcmp for checking if jump buffer is set
        memcmp_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type, self.i8_ptr_type, self.int_type], var_arg=False)
        self.memcmp = llvmlite.ir.Function(self.module, memcmp_type, "memcmp")
        
        # atoi and atof for string-to-number conversion with exception throwing
        atoi_type = llvmlite.ir.FunctionType(self.int_type, [self.i8_ptr_type], var_arg=False)
        self.atoi = llvmlite.ir.Function(self.module, atoi_type, "atoi")
        
        atof_type = llvmlite.ir.FunctionType(self.float_type, [self.i8_ptr_type], var_arg=False)
        self.atof = llvmlite.ir.Function(self.module, atof_type, "atof")
        
        # exit for uncaught exceptions
        exit_type = llvmlite.ir.FunctionType(self.void_type, [self.int_type], var_arg=False)
        self.exit = llvmlite.ir.Function(self.module, exit_type, "exit")
        
        # Global jump buffer for exception handling
        # This is a single global buffer that gets reused for each try/catch
        self.global_jmp_buf = llvmlite.ir.GlobalVariable(self.module, self.jmp_buf_type, "__mt_exception_jmp_buf")
        zero_i64 = llvmlite.ir.Constant(llvmlite.ir.IntType(64), 0)
        self.global_jmp_buf.initializer = llvmlite.ir.Constant(self.jmp_buf_type, [zero_i64] * 64)
        
        # Exception depth counter to track nested try blocks
        self.global_exception_depth = llvmlite.ir.GlobalVariable(self.module, self.int_type, "__mt_exception_depth")
        self.global_exception_depth.initializer = llvmlite.ir.Constant(self.int_type, 0)

        # Global storage for exception info (used by throw and catch)
        self.global_exception_type = llvmlite.ir.GlobalVariable(self.module, self.int_type, "__mt_exception_type")
        self.global_exception_type.initializer = llvmlite.ir.Constant(self.int_type, 0)

        self.global_exception_message = llvmlite.ir.GlobalVariable(self.module, self.i8_ptr_type, "__mt_exception_message")
        self.global_exception_message.initializer = llvmlite.ir.Constant(self.i8_ptr_type, None)

        # Create exception handling runtime
        self.create_exception_runtime()
    
    def create_exception_runtime(self):
        # Exception type: { i32 type_tag, i8* message }
        self.exc_type = llvmlite.ir.LiteralStructType([self.int_type, self.i8_ptr_type])
        
        # Exception type tag constants
        self.exc_type_tags = {}
        for exc_name, tag in EXCEPTION_TYPE_TAGS.items():
            const = llvmlite.ir.GlobalVariable(self.module, self.int_type, f"__mt_exc_tag_{exc_name}")
            const.initializer = llvmlite.ir.Constant(self.int_type, tag)
            const.global_constant = True
            self.exc_type_tags[exc_name] = const
    
    def throw_value_error(self, message):
        """Throw a ValueError exception with the given message"""
        # Create error message string
        error_msg = self.create_string_constant(message)
        
        # Create ValueError exception object {i32 type_tag, i8* message}
        exc_ptr = self.builder.alloca(self.exc_type, name="exc_obj")
        
        # Store type tag (ValueError = 1)
        tag_ptr = self.builder.gep(exc_ptr, [self.create_int_constant(0), self.create_int_constant(0)], name="exc_tag_ptr")
        tag_value = self.builder.load(self.get_exception_type_tag("ValueError"), name="exc_tag_value")
        self.builder.store(tag_value, tag_ptr)
        
        # Store message pointer
        msg_ptr = self.builder.gep(exc_ptr, [self.create_int_constant(0), self.create_int_constant(1)], name="exc_msg_ptr")
        self.builder.store(error_msg, msg_ptr)
        
        # Resume the exception (this is how we "throw" in LLVM)
        self.builder.resume(exc_ptr)
        
        # Add unreachable after resume
        unreachable_block = self.builder.append_basic_block(name="unreachable_after_throw")
        self.builder.position_at_end(unreachable_block)
        self.builder.unreachable()
    
    def get_exception_type_tag(self, exc_type_name):
        """Get the LLVM constant for an exception type tag"""
        if exc_type_name in self.exc_type_tags:
            return self.exc_type_tags[exc_type_name]
        else:
            # Default to Exception (0) for unknown types
            return self.exc_type_tags.get("Exception", llvmlite.ir.Constant(self.int_type, 0))

    def get_libc_llvm_type(self, type_str):
        """Convert libc registry type string to LLVM type"""
        if type_str == "int":
            return self.int_type
        elif type_str == "ptr":
            return self.i8_ptr_type
        elif type_str == "void":
            return self.void_type
        elif type_str == "float":
            return self.float_type
        else:
            raise Exception(f"Unknown libc type: {type_str}")

    def declare_libc_function(self, func_name):
        """Declare a single libc function from the registry"""
        if func_name in self.libc_functions:
            return self.libc_functions[func_name]  # Already declared

        # Check if already declared in module globals (e.g., malloc, free, etc.)
        if func_name in self.module.globals:
            self.libc_functions[func_name] = self.module.globals[func_name]
            return self.libc_functions[func_name]

        if func_name not in LIBC_FUNCTIONS:
            raise Exception(f"Unknown libc function: {func_name}. Available functions: {', '.join(sorted(LIBC_FUNCTIONS.keys()))}")

        func_info = LIBC_FUNCTIONS[func_name]
        ret_type = self.get_libc_llvm_type(func_info["ret"])
        arg_types = [self.get_libc_llvm_type(t) for t in func_info["args"]]
        var_arg = func_info.get("var_arg", False)

        func_type = llvmlite.ir.FunctionType(ret_type, arg_types, var_arg=var_arg)
        llvm_func = llvmlite.ir.Function(self.module, func_type, func_name)

        self.libc_functions[func_name] = llvm_func
        return llvm_func

    def generate_array_to_string(self, array_ptr, elem_type=None):
        """Convert an array to string format: [elem1, elem2, ...]

        Args:
            array_ptr: Pointer to the dynamic array
            elem_type: LLVM type of elements (defaults to int)

        Safety guarantees:
        - Never reads past array.length
        - Properly handles empty arrays
        """
        if elem_type is None:
            elem_type = self.int_type

        is_string_array = (elem_type == self.i8_ptr_type)

        length_ptr = self.builder.gep(array_ptr,
            [llvmlite.ir.Constant(self.int_type, 0), llvmlite.ir.Constant(self.int_type, 0)],
            name="arr_len_ptr")
        length = self.builder.load(length_ptr, name="arr_length")

        data_ptr_ptr = self.builder.gep(array_ptr,
            [llvmlite.ir.Constant(self.int_type, 0), llvmlite.ir.Constant(self.int_type, 2)],
            name="arr_data_ptr_ptr")
        data_ptr = self.builder.load(data_ptr_ptr, name="arr_data_ptr")

        func = self.builder.block.parent

        if is_string_array:
            alloc_size = self.builder.mul(length, self.create_int_constant(128), name="est_size")
            alloc_size = self.builder.add(alloc_size, self.create_int_constant(16), name="alloc_size")
        else:
            alloc_size = self.builder.mul(length, self.create_int_constant(64), name="est_size")
            alloc_size = self.builder.add(alloc_size, self.create_int_constant(16), name="alloc_size")
        result_buffer = self.builder.call(self.malloc, [alloc_size], name="str_result")

        self.builder.store(self.i8_type(ord('[')), self.builder.gep(result_buffer, [self.create_int_constant(0)], name="p0"))

        result_len = self.builder.alloca(self.int_type, name="result_len")
        self.builder.store(self.create_int_constant(1), result_len)

        loop_block = func.append_basic_block(name="arr_str_loop")
        body_block = func.append_basic_block(name="arr_str_body")
        done_block = func.append_basic_block(name="arr_str_done")

        counter_ptr = self.builder.alloca(self.int_type, name="arr_str_i")
        self.builder.store(self.create_int_constant(0), counter_ptr)
        self.builder.branch(loop_block)

        self.builder.position_at_end(loop_block)
        counter = self.builder.load(counter_ptr, name="i")
        cond = self.builder.icmp_signed("<", counter, length, name="arr_str_cond")
        self.builder.cbranch(cond, body_block, done_block)

        self.builder.position_at_end(body_block)
        current_i = self.builder.load(counter_ptr, name="curr_i")

        is_not_first = self.builder.icmp_signed(">", current_i, self.create_int_constant(0), name="is_not_first")
        with self.builder.if_then(is_not_first):
            len1 = self.builder.load(result_len, name="len1")
            p1 = self.builder.gep(result_buffer, [len1], name="p_comma")
            self.builder.store(self.i8_type(ord(',')), p1)
            len2 = self.builder.add(len1, self.create_int_constant(1), name="len2")
            p2 = self.builder.gep(result_buffer, [len2], name="p_space")
            self.builder.store(self.i8_type(ord(' ')), p2)
            self.builder.store(self.builder.add(len2, self.create_int_constant(1), name="len3"), result_len)

        current_len = self.builder.load(result_len, name="cur_len")

        if is_string_array:
            self.builder.store(self.i8_type(ord('"')), self.builder.gep(result_buffer, [current_len], name="p_quote_open"))
            new_len = self.builder.add(current_len, self.create_int_constant(1), name="new_len_quote")
            self.builder.store(new_len, result_len)

            str_data_ptr = self.builder.bitcast(data_ptr, self.i8_ptr_type.as_pointer(), name="str_data")
            elem_ptr = self.builder.gep(str_data_ptr, [current_i], name="elem_ptr")
            elem_val = self.builder.load(elem_ptr, name="elem")

            null_ptr = llvmlite.ir.Constant(self.i8_ptr_type, None)
            with self.builder.if_then(self.builder.icmp_signed("!=", elem_val, null_ptr)):
                str_buf_ptr = self.builder.gep(result_buffer, [new_len], name="str_buf")
                str_buf_ptr_ptr = self.builder.bitcast(str_buf_ptr, self.i8_type.as_pointer(), name="str_buf_ptr")
                self.builder.call(self.strcpy, [str_buf_ptr_ptr, elem_val])

                elem_str_len = self.builder.call(self.strlen, [str_buf_ptr], name="elem_str_len")
                final_elem_len = self.builder.add(new_len, elem_str_len, name="final_elem_len")
                self.builder.store(final_elem_len, result_len)

            self.builder.store(self.i8_type(ord('"')), self.builder.gep(result_buffer, [self.builder.load(result_len, name="after_str")], name="p_quote_close"))
            self.builder.store(self.builder.add(self.builder.load(result_len, name="qclose_len"), self.create_int_constant(1), name="final_len"), result_len)
        else:
            int_data_ptr = self.builder.bitcast(data_ptr, self.int_type.as_pointer(), name="int_data")
            elem_ptr = self.builder.gep(int_data_ptr, [current_i], name="elem_ptr")
            elem_val = self.builder.load(elem_ptr, name="elem")

            int_buf_ptr = self.builder.gep(result_buffer, [current_len], name="int_buf")
            int_buf_ptr_ptr = self.builder.bitcast(int_buf_ptr, self.i8_type.as_pointer(), name="int_buf_ptr")
            format_ptr = self.create_string_constant("%d", as_constant=True)
            self.builder.call(self.sprintf, [int_buf_ptr_ptr, format_ptr, elem_val])

            new_str_len = self.builder.call(self.strlen, [int_buf_ptr], name="new_str_len")
            self.builder.store(self.builder.add(current_len, new_str_len, name="updated_len"), result_len)

        next_i = self.builder.add(counter, self.create_int_constant(1), name="next_i")
        self.builder.store(next_i, counter_ptr)
        self.builder.branch(loop_block)

        self.builder.position_at_end(done_block)
        final_len = self.builder.load(result_len, name="final_len")
        close_ptr = self.builder.gep(result_buffer, [final_len], name="close_ptr")
        self.builder.store(self.i8_type(ord(']')), close_ptr)
        null_ptr = self.builder.gep(result_buffer, [self.builder.add(final_len, self.create_int_constant(1))], name="null_ptr")
        self.builder.store(self.i8_type(0), null_ptr)

        return result_buffer

    def create_dynamic_array(self, elements, elem_type, on_heap=False):
        """Create a new dynamic array with initial elements.

        Args:
            elements: Initial elements to store
            elem_type: LLVM type of elements
            on_heap: If True, allocate struct on heap (needed when returning from functions)
        """
        initial_capacity = max(len(elements), 4)  # At least 4 elements capacity

        # Calculate element size
        if elem_type == self.i8_ptr_type:
            elem_size = 8  # pointer size
        else:
            elem_size = 4  # int size

        # Allocate the array struct (on heap if returning, stack otherwise)
        if on_heap:
            # Allocate struct on heap - sizeof({i32, i32, i8*}) = 16 bytes on 64-bit
            struct_size = self.create_int_constant(24)  # 4 + 4 + 8 + padding
            array_mem = self.builder.call(self.malloc, [struct_size], name="dyn_array_mem")
            array_ptr = self.builder.bitcast(array_mem, self.dyn_array_ptr_type, name="dyn_array")
        else:
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
        
        # For class types (stored as i8*), bitcast the value to i8* before storing
        # Class instances are stored as struct pointers, so check if value is a pointer to struct
        value_ptr_type = getattr(value.type, 'pointee', None)
        if value_ptr_type and hasattr(value_ptr_type, 'elements'):
            value = self.builder.bitcast(value, self.i8_ptr_type, name="value_as_void_ptr")
        
        self.builder.store(value, elem_ptr)

        # Increment length
        new_length = self.builder.add(length, one, name="new_length")
        self.builder.store(new_length, length_ptr)

    def array_length(self, array_ptr):
        """Get the length of a dynamic array"""
        zero = llvmlite.ir.Constant(self.int_type, 0)
        length_ptr = self.builder.gep(array_ptr, [zero, zero], name="len_ptr")
        return self.builder.load(length_ptr, name="arr_length")

    def string_index(self, string_ptr, index):
        """Get character at index from string, return as single-character string"""
        func = self.builder.block.parent

        # Get string length for bounds checking
        str_len = self.builder.call(self.strlen, [string_ptr], name="str_len")

        # Check if index < 0
        zero = llvmlite.ir.Constant(self.int_type, 0)
        is_negative = self.builder.icmp_signed("<", index, zero, name="idx_negative")

        # Check if index >= length
        is_too_large = self.builder.icmp_signed(">=", index, str_len, name="idx_too_large")

        # Combine: out of bounds if negative OR too large
        is_out_of_bounds = self.builder.or_(is_negative, is_too_large, name="idx_out_of_bounds")

        # Create blocks for bounds check with unique names
        bc_id = self.bounds_check_counter
        self.bounds_check_counter += 1
        error_block = func.append_basic_block(name=f"str_index_error_{bc_id}")
        ok_block = func.append_basic_block(name=f"str_index_ok_{bc_id}")

        self.builder.cbranch(is_out_of_bounds, error_block, ok_block)

        # Error block - throw IndexError (use longjmp if in try block)
        self.builder.position_at_end(error_block)

        # Store exception type for catch block
        index_error_tag = self.builder.load(self.get_exception_type_tag("IndexError"), name="index_error_tag")
        self.builder.store(index_error_tag, self.global_exception_type)

        # Check at RUNTIME if we're in a try block by checking exception depth > 0
        exc_depth = self.builder.load(self.global_exception_depth, name="exc_depth_check")
        is_in_try = self.builder.icmp_signed(">", exc_depth, self.create_int_constant(0), name="is_in_try")

        # Create blocks for try/no-try paths
        throw_block = func.append_basic_block(name=f"str_idx_throw_{bc_id}")
        exit_block = func.append_basic_block(name=f"str_idx_exit_{bc_id}")

        self.builder.cbranch(is_in_try, throw_block, exit_block)

        # Throw block - use longjmp to jump to catch handler
        self.builder.position_at_end(throw_block)
        jmp_buf_ptr = self.builder.gep(self.global_jmp_buf, [self.create_int_constant(0), self.create_int_constant(0)], name="jmp_buf_for_index_error")
        jmp_buf_void_ptr = self.builder.bitcast(jmp_buf_ptr, self.i8_ptr_type, name="jmp_buf_void_ptr")
        self.builder.call(self.longjmp, [jmp_buf_void_ptr, self.create_int_constant(1)], name="longjmp_index_error")
        self.builder.unreachable()

        # Exit block - print error and exit (no try block)
        self.builder.position_at_end(exit_block)
        error_msg = self.create_string_constant("IndexError: string index out of bounds\n")
        self.builder.call(self.printf, [error_msg], name="print_index_error")
        self.builder.call(self.exit, [self.create_int_constant(1)], name="exit_on_index_error")
        self.builder.unreachable()

        # OK block - continue with indexing
        self.builder.position_at_end(ok_block)

        # Get the character at string_ptr + index
        char_ptr = self.builder.gep(string_ptr, [index], name="char_ptr")
        char = self.builder.load(char_ptr, name="char")
        
        # Allocate memory for a 2-byte string (char + null)
        two = llvmlite.ir.Constant(self.int_type, 2)
        new_str = self.builder.call(self.malloc, [two], name="new_str")
        
        # Store the character
        zero = llvmlite.ir.Constant(self.int_type, 0)
        char_store_ptr = self.builder.gep(new_str, [zero], name="char_store_ptr")
        self.builder.store(char, char_store_ptr)
        
        # Store null terminator
        one = llvmlite.ir.Constant(self.int_type, 1)
        null_ptr = self.builder.gep(new_str, [one], name="null_ptr")
        null_char = llvmlite.ir.Constant(self.i8_type, 0)
        self.builder.store(null_char, null_ptr)
        
        return new_str

    def array_get(self, array_ptr, index, elem_type):
        """Get element at index from dynamic array"""
        func = self.builder.block.parent
        zero = llvmlite.ir.Constant(self.int_type, 0)
        two = llvmlite.ir.Constant(self.int_type, 2)

        # Get array length for bounds checking
        length_ptr = self.builder.gep(array_ptr, [zero, zero], name="arr_len_ptr")
        arr_len = self.builder.load(length_ptr, name="arr_len")

        # Check if index < 0
        is_negative = self.builder.icmp_signed("<", index, zero, name="arr_idx_negative")

        # Check if index >= length
        is_too_large = self.builder.icmp_signed(">=", index, arr_len, name="arr_idx_too_large")

        # Combine: out of bounds if negative OR too large
        is_out_of_bounds = self.builder.or_(is_negative, is_too_large, name="arr_idx_out_of_bounds")

        # Create blocks for bounds check with unique names
        bc_id = self.bounds_check_counter
        self.bounds_check_counter += 1
        error_block = func.append_basic_block(name=f"arr_index_error_{bc_id}")
        ok_block = func.append_basic_block(name=f"arr_index_ok_{bc_id}")

        self.builder.cbranch(is_out_of_bounds, error_block, ok_block)

        # Error block - throw IndexError (use longjmp if in try block)
        self.builder.position_at_end(error_block)

        # Store exception type for catch block
        index_error_tag = self.builder.load(self.get_exception_type_tag("IndexError"), name="arr_index_error_tag")
        self.builder.store(index_error_tag, self.global_exception_type)

        # Check at RUNTIME if we're in a try block by checking exception depth > 0
        exc_depth = self.builder.load(self.global_exception_depth, name="arr_exc_depth_check")
        is_in_try = self.builder.icmp_signed(">", exc_depth, self.create_int_constant(0), name="arr_is_in_try")

        # Create blocks for try/no-try paths
        throw_block = func.append_basic_block(name=f"arr_idx_throw_{bc_id}")
        exit_block = func.append_basic_block(name=f"arr_idx_exit_{bc_id}")

        self.builder.cbranch(is_in_try, throw_block, exit_block)

        # Throw block - use longjmp to jump to catch handler
        self.builder.position_at_end(throw_block)
        jmp_buf_ptr = self.builder.gep(self.global_jmp_buf, [self.create_int_constant(0), self.create_int_constant(0)], name="jmp_buf_for_arr_index_error")
        jmp_buf_void_ptr = self.builder.bitcast(jmp_buf_ptr, self.i8_ptr_type, name="jmp_buf_void_ptr")
        self.builder.call(self.longjmp, [jmp_buf_void_ptr, self.create_int_constant(1)], name="longjmp_arr_index_error")
        self.builder.unreachable()

        # Exit block - print error and exit (no try block)
        self.builder.position_at_end(exit_block)
        error_msg = self.create_string_constant("IndexError: array index out of bounds\n")
        self.builder.call(self.printf, [error_msg], name="print_arr_index_error")
        self.builder.call(self.exit, [self.create_int_constant(1)], name="exit_on_arr_index_error")
        self.builder.unreachable()

        # OK block - continue with indexing
        self.builder.position_at_end(ok_block)

        data_field_ptr = self.builder.gep(array_ptr, [zero, two], name="data_ptr_ptr")
        data_ptr = self.builder.load(data_field_ptr, name="data_ptr")

        if elem_type == self.i8_ptr_type:
            typed_ptr = self.builder.bitcast(data_ptr, self.i8_ptr_type.as_pointer())
        else:
            typed_ptr = self.builder.bitcast(data_ptr, self.int_type.as_pointer())

        elem_ptr = self.builder.gep(typed_ptr, [index], name="elem_ptr")
        return self.builder.load(elem_ptr, name="elem_val")

    def string_split(self, string_ptr, delimiter_ptr):
        """
        Split a string by delimiter, returning a dynamic array of strings.
        """
        func = self.builder.block.parent

        # Get delimiter length (needed to skip past delimiters)
        delim_len = self.builder.call(self.strlen, [delimiter_ptr], name="delim_len")

        # Create result array on heap (since it may be returned from a function)
        result_array, _, _ = self.create_dynamic_array([], self.i8_ptr_type, on_heap=True)

        # Allocate loop variables on stack
        current_pos = self.builder.alloca(self.i8_ptr_type, name="current_pos")
        self.builder.store(string_ptr, current_pos)

        segment_start = self.builder.alloca(self.i8_ptr_type, name="segment_start")
        self.builder.store(string_ptr, segment_start)

        # Create basic blocks for loop structure
        loop_block = func.append_basic_block(name="split_loop")
        found_block = func.append_basic_block(name="split_found")
        not_found_block = func.append_basic_block(name="split_not_found")
        append_final_block = func.append_basic_block(name="split_append_final")
        exit_block = func.append_basic_block(name="split_exit")

        # Enter loop
        self.builder.branch(loop_block)

        # === Loop Block: search for next delimiter ===
        self.builder.position_at_end(loop_block)
        cur = self.builder.load(current_pos, name="cur")
        found_ptr = self.builder.call(self.strstr, [cur, delimiter_ptr], name="found")

        # Check if delimiter was found (strstr returns NULL if not found)
        null_ptr = llvmlite.ir.Constant(self.i8_ptr_type, None)
        is_null = self.builder.icmp_unsigned("==", found_ptr, null_ptr, name="is_null")
        self.builder.cbranch(is_null, not_found_block, found_block)

        # === Found Block: extract segment and continue loop ===
        self.builder.position_at_end(found_block)
        start = self.builder.load(segment_start, name="seg_start")

        # Calculate segment length: found_ptr - segment_start
        start_int = self.builder.ptrtoint(start, self.int_type, name="start_int")
        found_int = self.builder.ptrtoint(found_ptr, self.int_type, name="found_int")
        seg_len = self.builder.sub(found_int, start_int, name="seg_len")

        # Allocate memory for segment (length + 1 for null terminator)
        seg_len_plus1 = self.builder.add(seg_len, llvmlite.ir.Constant(self.int_type, 1), name="seg_len_plus1")
        new_seg = self.builder.call(self.malloc, [seg_len_plus1], name="new_segment")

        # Copy segment bytes
        self.builder.call(self.memcpy, [new_seg, start, seg_len])

        # Null-terminate the segment
        null_pos = self.builder.gep(new_seg, [seg_len], name="null_pos")
        self.builder.store(llvmlite.ir.Constant(llvmlite.ir.IntType(8), 0), null_pos)

        # Append segment to result array
        self.array_append(result_array, new_seg, self.i8_ptr_type, 8)

        # Move current position past the delimiter
        next_pos = self.builder.gep(found_ptr, [delim_len], name="next_pos")
        self.builder.store(next_pos, current_pos)
        self.builder.store(next_pos, segment_start)

        # Continue loop
        self.builder.branch(loop_block)

        # === Not Found Block: check if there's a final segment ===
        self.builder.position_at_end(not_found_block)
        final_start = self.builder.load(segment_start, name="final_start")
        final_len = self.builder.call(self.strlen, [final_start], name="final_len")

        # Check if there's content in the final segment (or if we should add empty string)
        # We always add the final segment to handle cases like "a,b," correctly
        self.builder.branch(append_final_block)

        # === Append Final Block: add the last segment ===
        self.builder.position_at_end(append_final_block)
        final_start2 = self.builder.load(segment_start, name="final_start2")
        final_len2 = self.builder.call(self.strlen, [final_start2], name="final_len2")
        final_len_plus1 = self.builder.add(final_len2, llvmlite.ir.Constant(self.int_type, 1), name="final_len_plus1")
        final_seg = self.builder.call(self.malloc, [final_len_plus1], name="final_segment")
        self.builder.call(self.strcpy, [final_seg, final_start2])
        self.array_append(result_array, final_seg, self.i8_ptr_type, 8)
        self.builder.branch(exit_block)

        # === Exit Block ===
        self.builder.position_at_end(exit_block)

        return result_array

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
    
    def _generate_with_location(self, node, context="expression"):
        """Helper to generate code with better error messages including source location"""
        # Track the last source position for error reporting
        if hasattr(node, 'line') and node.line is not None:
            self._last_source_position = (node.line, node.column, getattr(node, 'file_path', None))
        elif hasattr(node, 'file_path'):
            self._last_source_position = (None, None, node.file_path)
        
        try:
            result = self.generate(node)
            if result is None and node is not None:
                line, column, fp = getattr(self, '_last_source_position', (None, None, None))
                pos_info = f" at line {line}, column {column}" if line is not None else ""
                fp_info = f" in {fp}" if fp else ""
                node_desc = f"{node.__class__.__name__}"
                if hasattr(node, 'value') and isinstance(node.value, str):
                    node_desc += f"('{node.value}')"
                elif hasattr(node, 'name'):
                    node_desc += f"('{node.name}')"
                raise Exception(f"{context} {node_desc} generated None{pos_info}{fp_info}. This usually means semantic analysis failed to resolve its type.")
            return result
        except Exception as e:
            line, column, fp = getattr(self, '_last_source_position', (None, None, None))
            pos_info = f" at line {line}, column {column}" if line is not None else ""
            fp_info = f" in {fp}" if fp else ""
            node_desc = f"{node.__class__.__name__}"
            if hasattr(node, 'value') and isinstance(node.value, str):
                node_desc += f"('{node.value}')"
            elif hasattr(node, 'name'):
                node_desc += f"('{node.name}')"
            raise Exception(f"Error generating {context} {node_desc}: {str(e)}{pos_info}{fp_info}") from e
    
    def generate(self, node):
        if isinstance(node, Program):
            # Create LLVM structs for classes
            self.create_class_structs()
            result = None
            for statement in node.statements:
                result = self.generate(statement)
            return result  # Return the last statement result
        if isinstance(node, ClassDeclaration):
            # Generate LLVM functions for class methods
            for method in node.methods:
                self.generate_class_method(node.name, method)
            return None
        if isinstance(node, FromImportStatement):
            # Load the module for code generation
            module_name = self.load_module(node.module_path)
            return None
        if isinstance(node, LibcImportStatement):
            # Declare requested libc functions
            for func_name in node.symbols:
                self.declare_libc_function(func_name)
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

            # Calculate struct size for heap allocation
            # Each field: i8* (pointer/string) = 8 bytes, i32 (int) = 4 bytes, f32 (float) = 4 bytes
            struct_size = 0
            for field in ordered_fields:
                field_type = field['type']
                if field_type == self.i8_ptr_type or (hasattr(field_type, 'pointee')):
                    struct_size += 8  # pointer
                elif field_type == self.int_type:
                    struct_size += 4
                elif field_type == self.float_type:
                    struct_size += 4
                else:
                    struct_size += 8  # default to pointer size for unknown types
            # Add padding to align to 8 bytes
            struct_size = ((struct_size + 7) // 8) * 8

            # Allocate memory for the struct ON THE HEAP so it persists beyond function scope
            size_val = self.create_int_constant(struct_size)
            obj_mem = self.builder.call(self.malloc, [size_val], name=f"{class_name}_mem")
            obj_ptr = self.builder.bitcast(obj_mem, struct_type.as_pointer(), name=f"{class_name}_obj")

            # Initialize constructor arguments
            arg_fields = [f for f in ordered_fields if f['is_constructor_arg']]

            # Check argument count - allow fewer args if remaining have defaults
            if len(node.arguments) > len(arg_fields):
                raise Exception(f"Too many arguments for {class_name} constructor: expected at most {len(arg_fields)}, got {len(node.arguments)}")

            # Find required args (those without initializers)
            required_count = 0
            for f in arg_fields:
                if f.get('initializer') is None:
                    required_count += 1
                else:
                    break  # Once we hit a default, all following should have defaults

            if len(node.arguments) < required_count:
                raise Exception(f"Not enough arguments for {class_name} constructor: expected at least {required_count}, got {len(node.arguments)}")

            # Initialize provided arguments
            for i, arg in enumerate(node.arguments):
                value = self.generate(arg)
                field_ptr = self.builder.gep(obj_ptr, [self.create_int_constant(0), self.create_int_constant(i)], name=f"{arg_fields[i]['name']}_ptr")
                self.builder.store(value, field_ptr)

            # Initialize remaining constructor args with defaults
            for i in range(len(node.arguments), len(arg_fields)):
                field = arg_fields[i]
                if field.get('initializer') is not None:
                    value = self.generate(field['initializer'])
                    field_ptr = self.builder.gep(obj_ptr, [self.create_int_constant(0), self.create_int_constant(i)], name=f"{field['name']}_ptr")
                    self.builder.store(value, field_ptr)
                else:
                    raise Exception(f"Missing argument for {class_name} constructor: '{field['name']}' has no default value")

            # Initialize regular fields with defaults if any
            regular_fields = [f for f in ordered_fields if not f['is_constructor_arg']]
            for i, field in enumerate(regular_fields):
                field_idx = len(arg_fields) + i
                if field.get('initializer') is not None:
                    value = self.generate(field['initializer'])
                    field_ptr = self.builder.gep(obj_ptr, [self.create_int_constant(0), self.create_int_constant(field_idx)], name=f"{field['name']}_ptr")
                    self.builder.store(value, field_ptr)

            return obj_ptr
        if isinstance(node, BinaryExpression):
            # Handle short-circuit evaluation for && and || BEFORE generating right operand
            if node.operator.value == "&&":
                func = self.builder.block.parent
                # Create blocks for short-circuit evaluation
                eval_right_block = func.append_basic_block(name="and_eval_right")
                merge_block = func.append_basic_block(name="and_merge")

                # Evaluate left operand
                left = self.generate(node.left)
                bool_left = self.promote_to_bool(left)
                left_block = self.builder.block

                # If left is false, short-circuit to merge with false
                self.builder.cbranch(bool_left, eval_right_block, merge_block)

                # Evaluate right operand only if left was true
                self.builder.position_at_end(eval_right_block)
                right = self.generate(node.right)
                bool_right = self.promote_to_bool(right)
                right_block = self.builder.block
                self.builder.branch(merge_block)

                # Merge block - phi node selects result
                self.builder.position_at_end(merge_block)
                phi = self.builder.phi(self.bool_type, name="and_result")
                phi.add_incoming(llvmlite.ir.Constant(self.bool_type, 0), left_block)  # false from short-circuit
                phi.add_incoming(bool_right, right_block)  # result from right eval
                return phi

            elif node.operator.value == "||":
                func = self.builder.block.parent
                # Create blocks for short-circuit evaluation
                eval_right_block = func.append_basic_block(name="or_eval_right")
                merge_block = func.append_basic_block(name="or_merge")

                # Evaluate left operand
                left = self.generate(node.left)
                bool_left = self.promote_to_bool(left)
                left_block = self.builder.block

                # If left is true, short-circuit to merge with true
                self.builder.cbranch(bool_left, merge_block, eval_right_block)

                # Evaluate right operand only if left was false
                self.builder.position_at_end(eval_right_block)
                right = self.generate(node.right)
                bool_right = self.promote_to_bool(right)
                right_block = self.builder.block
                self.builder.branch(merge_block)

                # Merge block - phi node selects result
                self.builder.position_at_end(merge_block)
                phi = self.builder.phi(self.bool_type, name="or_result")
                phi.add_incoming(llvmlite.ir.Constant(self.bool_type, 1), left_block)  # true from short-circuit
                phi.add_incoming(bool_right, right_block)  # result from right eval
                return phi

            left = self.generate(node.left)
            right = self.generate(node.right)

            # Handle string concatenation
            if node.operator.value == "+" and left.type == self.i8_ptr_type and right.type == self.i8_ptr_type:
                # Replace null with empty string using select
                null_ptr = llvmlite.ir.Constant(self.i8_ptr_type, None)
                empty_str = self.create_string_constant("")
                
                left_safe = self.builder.select(
                    self.builder.icmp_signed("==", left, null_ptr, name="left_is_null"),
                    empty_str,
                    left,
                    name="left_safe"
                )
                right_safe = self.builder.select(
                    self.builder.icmp_signed("==", right, null_ptr, name="right_is_null"),
                    empty_str,
                    right,
                    name="right_safe"
                )
                
                len1 = self.builder.call(self.strlen, [left_safe], name="len1")
                len2 = self.builder.call(self.strlen, [right_safe], name="len2")
                total_len = self.builder.add(len1, len2, name="total_len")
                total_len_plus1 = self.builder.add(total_len, self.create_int_constant(1), name="total_len_plus1")
                new_str = self.builder.call(self.malloc, [total_len_plus1], name="new_str")
                _ = self.builder.call(self.strcpy, [new_str, left_safe], name="copy1")
                _ = self.builder.call(self.strcat, [new_str, right_safe], name="concat")
                return new_str

            # Handle string comparisons specially (before null comparison)
            # Handle pointer vs null comparison (e.g., strstr result != null)
            # Only check for null comparison if one operand is actually a null literal
            # Check if right is a null constant and left is i8*
            if right.type == self.i8_ptr_type and isinstance(node.right, NullLiteral):
                null_ptr = llvmlite.ir.Constant(self.i8_ptr_type, None)
                if node.operator.value == "==":
                    return self.builder.icmp_signed("==", left, null_ptr, name="is_null")
                elif node.operator.value == "!=":
                    return self.builder.icmp_signed("!=", left, null_ptr, name="is_not_null")
            # Check if left is a null constant and right is i8*
            if left.type == self.i8_ptr_type and isinstance(node.left, NullLiteral):
                null_ptr = llvmlite.ir.Constant(self.i8_ptr_type, None)
                if node.operator.value == "==":
                    return self.builder.icmp_signed("==", right, null_ptr, name="is_null")
                elif node.operator.value == "!=":
                    return self.builder.icmp_signed("!=", right, null_ptr, name="is_not_null")

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
            
            # Note: && and || are handled above with short-circuit evaluation
        if isinstance(node, InExpression):
            return self.generate_in_expression(node)
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
                # Check for explicit element type from array<Type> syntax
                explicit_element_type = getattr(node, 'element_type', None)

                if isinstance(node.value, ArrayLiteral):
                    elements = node.value.elements
                    # Determine element type: explicit type > inferred from elements > default int
                    if explicit_element_type:
                        elem_type, elem_size, elem_class = self.get_element_llvm_type(explicit_element_type)
                        if elem_class:
                            self.array_element_classes[node.name] = elem_class
                    elif len(elements) > 0 and isinstance(elements[0], StringLiteral):
                        elem_type = self.i8_ptr_type
                        elem_size = 8
                    else:
                        elem_type = self.int_type
                        elem_size = 4

                    # Generate values for all elements
                    elem_values = []
                    for i, element in enumerate(elements):
                        try:
                            elem_value = self._generate_with_location(element, f"array element {i}")
                            if elem_value is None:
                                raise Exception(f"Array element {i} evaluated to None")
                            elem_values.append(elem_value)
                        except Exception as e:
                            line = getattr(node.value, 'line', None)
                            column = getattr(node.value, 'column', None)
                            pos_info = f" at line {line}, column {column}" if line is not None else ""
                            raise Exception(f"Failed to generate array element at index {i}{pos_info}: {e}")

                    # Create dynamic array - returns pointer to struct
                    # Always use on_heap=True to ensure array survives function returns
                    array_ptr, _, _ = self.create_dynamic_array(elem_values, elem_type, on_heap=True)

                    # If in main function, create a global variable to store the pointer
                    # so it can be accessed from methods
                    if self.in_main_function:
                        global_var = llvmlite.ir.GlobalVariable(self.module, self.dyn_array_ptr_type, name=f"global_{node.name}")
                        global_var.initializer = llvmlite.ir.Constant(self.dyn_array_ptr_type, None)
                        self.builder.store(array_ptr, global_var)
                        self.global_arrays[node.name] = global_var

                    self.variables[node.name] = array_ptr  # Store pointer directly
                    self.array_lengths[node.name] = (elem_type, elem_size)
                elif node.value is None:
                    # Initialize empty array - use explicit element type or default to int
                    # Always use on_heap=True to ensure array survives function returns
                    if explicit_element_type:
                        elem_type, elem_size, elem_class = self.get_element_llvm_type(explicit_element_type)
                        if elem_class:
                            self.array_element_classes[node.name] = elem_class
                    else:
                        elem_type = self.int_type
                        elem_size = 4
                    array_ptr, _, _ = self.create_dynamic_array([], elem_type, on_heap=True)

                    # If in main function, create a global variable
                    if self.in_main_function:
                        global_var = llvmlite.ir.GlobalVariable(self.module, self.dyn_array_ptr_type, name=f"global_{node.name}")
                        global_var.initializer = llvmlite.ir.Constant(self.dyn_array_ptr_type, None)
                        self.builder.store(array_ptr, global_var)
                        self.global_arrays[node.name] = global_var

                    self.variables[node.name] = array_ptr
                    self.array_lengths[node.name] = (elem_type, elem_size)
                else:
                    # Array assignment from function call or variable
                    array_ptr = self.generate(node.value)

                    # Type check: make sure the value is actually an array pointer
                    if array_ptr.type != self.dyn_array_ptr_type:
                        raise TypeError(f"Cannot assign {array_ptr.type} to array variable '{node.name}': expected {self.dyn_array_ptr_type}")

                    # If in main function, create a global variable
                    if self.in_main_function:
                        global_var = llvmlite.ir.GlobalVariable(self.module, self.dyn_array_ptr_type, name=f"global_{node.name}")
                        global_var.initializer = llvmlite.ir.Constant(self.dyn_array_ptr_type, None)
                        self.builder.store(array_ptr, global_var)
                        self.global_arrays[node.name] = global_var

                    self.variables[node.name] = array_ptr

                    # Use explicit element type if provided
                    if explicit_element_type:
                        elem_type, elem_size, elem_class = self.get_element_llvm_type(explicit_element_type)
                        self.array_lengths[node.name] = (elem_type, elem_size)
                        if elem_class:
                            self.array_element_classes[node.name] = elem_class
                        return None

                    # Determine element type from context
                    if isinstance(node.value, CallExpression):
                        # Handle split() function call (from stdlib.strings)
                        if isinstance(node.value.callee, Identifier) and node.value.callee.name == "split":
                            self.array_lengths[node.name] = (self.i8_ptr_type, 8)
                            return None
                        if isinstance(node.value.callee, MemberExpression):
                            method_name = node.value.callee.property
                            # For class methods returning array, check the class definition
                            if isinstance(node.value.callee.object, Identifier):
                                obj_name = node.value.callee.object.name
                                if obj_name in self.variable_classes:
                                    class_name = self.variable_classes[obj_name]
                                    if class_name in self.classes:
                                        methods = self.classes[class_name].get('methods', {})
                                        if method_name in methods:
                                            # Method returns array - default to string elements
                                            # since most array-returning methods deal with strings
                                            self.array_lengths[node.name] = (self.i8_ptr_type, 8)
                                            return None

                    # Default: assume int elements
                    self.array_lengths[node.name] = (self.int_type, 4)
            else:
                # For non-array types
                if node.type in ('int', 'float', 'string', 'bool'):
                    # For primitive types, allocate space on stack
                    pointer = self.builder.alloca(llvm_type, name=node.name)
                    self.variables[node.name] = pointer

                    if node.value:
                        value = self.generate(node.value)
                    else:
                        # Initialize with default values
                        if node.type == 'int':
                            value = llvmlite.ir.Constant(self.int_type, 0)
                        elif node.type == 'float':
                            value = llvmlite.ir.Constant(self.float_type, 0.0)
                        elif node.type == 'string':
                            value = self.create_string_constant("", as_constant=True)
                        elif node.type == 'bool':
                            value = llvmlite.ir.Constant(self.bool_type, 0)  # false

                    # Type check before storing
                    if value.type != llvm_type:
                        raise TypeError(f"Cannot assign {value.type} to variable '{node.name}' of type {llvm_type}: type mismatch")

                    self.builder.store(value, pointer)
                    return None
                elif node.type == 'void':
                    # Void variables are not allowed
                    raise TypeError(f"Cannot declare variable of type 'void'")
                else:
                    # For class types, store the object pointer directly (like arrays)
                    if node.value:
                        value = self.generate(node.value)
                        self.variables[node.name] = value  # Store object pointer directly
                        self.variable_types[node.name] = llvm_type
                        self.variable_classes[node.name] = node.type  # Store class name
                    return None
        if isinstance(node, IndexExpression):
            # Handle array/string indexing
            obj = self.generate(node.object)
            index = self.generate(node.index)

            # Check if the object is a string (i8*)
            if obj.type == self.i8_ptr_type:
                # String indexing: return single character as string
                return self.string_index(obj, index)

            # Handle array indexing: arr[index]
            # Determine the element type based on the array
            if isinstance(node.object, Identifier):
                if node.object.name in self.array_lengths:
                    elem_type, elem_size = self.array_lengths[node.object.name]
                else:
                    # Not an array, error
                    raise Exception(f"Cannot index into non-array/string type for {node.object.name}")
            else:
                # For more complex expressions, assume int elements for now
                elem_type = self.int_type
                elem_size = 4

            return self.array_get(obj, index, elem_type)
        if isinstance(node, Identifier):
            # Check if it's a global array (accessed from a method)
            if node.name in self.global_arrays and node.name not in self.variables:
                # Load the array pointer from the global variable
                global_var = self.global_arrays[node.name]
                return self.builder.load(global_var, name=f"global_{node.name}")

            if node.name not in self.variables:
                raise Exception(f"Variable '{node.name}' not declared")

            storage = self.variables[node.name]
            var_type = self.variable_types[node.name]

            # Special handling for arrays - return the pointer directly
            if var_type == self.dyn_array_ptr_type:
                return storage  # Return the pointer directly, no load needed
            # Special handling for class instances - return the pointer directly
            elif node.name in self.variable_classes:
                return storage  # Return the class instance pointer directly
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
                    
                    # Cast obj_ptr to struct pointer type for GEP
                    struct_type = self.classes[class_name]['llvm_struct']
                    obj_struct_ptr = self.builder.bitcast(obj_ptr, struct_type.as_pointer(), name=f"{obj_name}_struct")
                    
                    # Access field
                    field_ptr = self.builder.gep(obj_struct_ptr, [self.create_int_constant(0), self.create_int_constant(field_idx)], name=f"{node.property}_field")
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
            # Handle split() function call (from stdlib.strings)
            if isinstance(node.callee, Identifier) and node.callee.name == "split":
                if len(node.arguments) != 2:
                    raise Exception("split() requires exactly 2 arguments (string, delimiter)")
                string_ptr = self.generate(node.arguments[0])
                delimiter = self.generate(node.arguments[1])
                return self.string_split(string_ptr, delimiter)

            # Handle method calls on objects (like arr.append(value))
            if isinstance(node.callee, MemberExpression):
                # Handle string literal method calls: "text".length()
                if isinstance(node.callee.object, StringLiteral):
                    method_name = node.callee.property
                    if method_name == "length":
                        string_ptr = self.generate(node.callee.object)
                        return self.builder.call(self.strlen, [string_ptr], name="str_len")
                    else:
                        raise Exception(f"Unknown string method: {method_name}")

                # Handle method calls on MemberExpressions (like this.data.length())
                if isinstance(node.callee.object, MemberExpression):
                    method_name = node.callee.property
                    # Evaluate the expression to get the value
                    obj_value = self.generate(node.callee.object)
                    if method_name == "length":
                        # Could be string or array - check type
                        if obj_value.type == self.i8_ptr_type:
                            return self.builder.call(self.strlen, [obj_value], name="str_len")
                        elif obj_value.type == self.dyn_array_ptr_type:
                            return self.array_length(obj_value)
                    # Fall through for other method handling on expressions
                    raise Exception(f"Unsupported method '{method_name}' on expression")

                # Handle method calls on 'this' (this.method_name())
                if isinstance(node.callee.object, ThisExpression):
                    method_name = node.callee.property
                    
                    # Get the current class name
                    if not hasattr(self, 'current_class') or not self.current_class:
                        raise Exception(f"Cannot call method '{method_name}' outside of a class")
                    
                    class_name = self.current_class
                    
                    # Look up the method function
                    func_name = f"{class_name}_{method_name}"
                    if func_name not in self.functions:
                        raise Exception(f"Method '{method_name}' not found in class '{class_name}'")
                    
                    # Get the method function
                    func = self.functions[func_name]
                    
                    # Get 'this' pointer (should already be stored in self.variables['this'])
                    if 'this' not in self.variables:
                        raise Exception(f"'this' is not available in method context")
                    this_ptr = self.variables['this']

                    # Generate arguments, with 'this' as first argument
                    args = [this_ptr]
                    for arg in node.arguments:
                        args.append(self.generate(arg))

                    # Get position info for better error messages
                    line = getattr(node, 'line', None)
                    column = getattr(node, 'column', None)
                    pos_info = f" at line {line}, column {column}" if line is not None else ""

                    try:
                        return self.builder.call(func, args, name=f"this_{method_name}")
                    except TypeError as e:
                        raise TypeError(f"Type mismatch in call to '{class_name}.{method_name}'{pos_info}: {e}")

                # Handle method calls on identifiers
                if not isinstance(node.callee.object, Identifier):
                    raise Exception(f"Unsupported method call object type: {type(node.callee.object)}")

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

                # Check if it's a string variable method call
                if obj_name in self.variable_types:
                    var_type = self.variable_types[obj_name]
                    if var_type == self.i8_ptr_type and obj_name not in self.array_lengths:
                        # This is a string variable - check for string methods
                        if method_name == "length":
                            string_ptr = self.builder.load(self.variables[obj_name], name="str_val")
                            return self.builder.call(self.strlen, [string_ptr], name="str_len")
                        # If not a known string method, fall through to other handlers

                # Check if it's a method call on a class instance
                if obj_name in self.variable_classes:
                    class_name = self.variable_classes[obj_name]
                    func_name = f"{class_name}_{method_name}"
                    if func_name not in self.functions:
                        raise Exception(f"Method '{method_name}' not found in class '{class_name}'")
                    func = self.functions[func_name]
                    obj_ptr = self.variables[obj_name]
                    # Get the struct type for this class
                    struct_type = self.classes[class_name]['llvm_struct']
                    # Cast to struct pointer type for the first argument
                    obj_struct_ptr = self.builder.bitcast(obj_ptr, struct_type.as_pointer(), name="obj_struct")
                    args = [obj_struct_ptr] + [self.generate(arg) for arg in node.arguments]
                    # Get position info for better error messages
                    line = getattr(node, 'line', None)
                    column = getattr(node, 'column', None)
                    pos_info = f" at line {line}, column {column}" if line is not None else ""
                    try:
                        return self.builder.call(func, args)
                    except TypeError as e:
                        raise TypeError(f"Type mismatch in call to '{class_name}.{method_name}'{pos_info}: {e}")

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
                            # Get position info for better error messages
                            line = getattr(node, 'line', None)
                            column = getattr(node, 'column', None)
                            pos_info = f" at line {line}, column {column}" if line is not None else ""
                            try:
                                return self.builder.call(func, args)
                            except TypeError as e:
                                raise TypeError(f"Type mismatch in call to '{class_name}.{method_name}'{pos_info}: {e}")
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
                # Get position info for better error messages
                line = getattr(node, 'line', None)
                column = getattr(node, 'column', None)
                pos_info = f" at line {line}, column {column}" if line is not None else ""
                try:
                    return self.builder.call(func, args)
                except TypeError as e:
                    raise TypeError(f"Type mismatch in call to '{module_name}.{func_name}'{pos_info}: {e}")
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
                elif arg.type == self.float_type:
                    buffer = self.builder.call(self.malloc, [self.create_int_constant(64)], name="str_buffer")
                    format_str = self.create_string_constant("%g", as_constant=True)
                    _ = self.builder.call(self.sprintf, [buffer, format_str, arg], name="sprintf_call")
                    return buffer
                elif arg.type == self.i8_ptr_type:
                    # String - just return it as-is
                    return arg
                elif arg.type == self.bool_type:
                    # Bool - return "true" or "false"
                    true_str = self.create_string_constant("true")
                    false_str = self.create_string_constant("false")
                    # Create a select: if arg is true, return true_str, else return false_str
                    return self.builder.select(arg, true_str, false_str, name="bool_to_str")
                elif arg.type == self.dyn_array_ptr_type:
                    elem_type = None
                    if isinstance(node.arguments[0], Identifier):
                        var_name = node.arguments[0].name
                        if var_name in self.array_lengths:
                            elem_type, _ = self.array_lengths[var_name]
                    return self.generate_array_to_string(arg, elem_type)
                else:
                    raise Exception(f"str() doesn't support type {arg.type}")
            elif isinstance(node.callee, Identifier) and node.callee.name == "int":
                # int() - convert to integer with exception throwing
                if len(node.arguments) != 1:
                    raise Exception("int() takes 1 argument")
                arg = self.generate(node.arguments[0])
                
                if arg.type == self.int_type:
                    return arg
                elif arg.type == self.float_type:
                    return self.builder.fptosi(arg, self.int_type, name="float_to_int")
                elif arg.type == self.bool_type:
                    return self.builder.zext(arg, self.int_type, name="bool_to_int")
                elif arg.type == self.i8_ptr_type:
                    # String to int conversion with validation
                    # Check if builder is available (it won't be during module import)
                    if not hasattr(self, 'builder') or self.builder is None or self.builder.block is None:
                        # During module import - just return a placeholder
                        return self.create_int_constant(0)
                    
                    func = self.builder.block.parent
                    
                    # Call atoi for conversion
                    converted = self.builder.call(self.atoi, [arg], name="atoi_result")

                    # Validate: check if all characters were consumed by atoi
                    # atoi stops at first non-digit, so we need to verify the whole string was consumed

                    # Format converted int back to string using sprintf
                    buffer = self.builder.call(self.malloc, [self.create_int_constant(32)], name="itoa_buffer")
                    sprintf_fmt = self.create_string_constant("%d")
                    sprintf_result = self.builder.call(self.sprintf, [buffer, sprintf_fmt, converted], name="itoa_sprintf")

                    # Compare strings - if different, atoi didn't consume whole string
                    # Use strcmp to compare original arg with reformatted buffer
                    cmp_result = self.builder.call(self.strcmp, [arg, buffer], name="strconv_cmp")
                    is_valid = self.builder.icmp_signed("==", cmp_result, self.create_int_constant(0), name="is_valid_int")
                    
                    # Create blocks for valid and invalid paths
                    valid_block = func.append_basic_block(name="int_valid")
                    invalid_block = func.append_basic_block(name="int_invalid")
                    continue_block = func.append_basic_block(name="int_continue")
                    
                    self.builder.cbranch(is_valid, valid_block, invalid_block)
                    
                    # Valid path - return the converted value
                    self.builder.position_at_end(valid_block)
                    self.builder.branch(continue_block)
                    
                    # Invalid path - throw exception using longjmp
                    self.builder.position_at_end(invalid_block)
                    
                    # Check if we're in a try block
                    in_try = self.variables.get("_in_try_block", False)
                    if in_try and hasattr(self, 'global_jmp_buf'):
                        # Get pointer to global jump buffer and cast to i8* for longjmp
                        jmp_buf_ptr = self.builder.gep(self.global_jmp_buf, [self.create_int_constant(0), self.create_int_constant(0)], name="jmp_buf_for_longjmp")
                        jmp_buf_void_ptr = self.builder.bitcast(jmp_buf_ptr, self.i8_ptr_type, name="jmp_buf_void_ptr")
                        self.builder.call(self.longjmp, [jmp_buf_void_ptr, self.create_int_constant(1)], name="longjmp_throw")
                    
                    # If no try block or error, just print error
                    error_msg = self.create_string_constant("ValueError: invalid value for int() conversion\n")
                    self.builder.call(self.printf, [error_msg], name="print_value_error")
                    self.builder.call(self.exit, [self.create_int_constant(1)], name="exit_on_error")
                    self.builder.unreachable()
                    
                    # Continue block for result
                    self.builder.position_at_end(continue_block)
                    return converted
                else:
                    raise Exception(f"int() doesn't support type {arg.type}")
            elif isinstance(node.callee, Identifier) and node.callee.name == "float":
                # float() - convert to float
                if len(node.arguments) != 1:
                    raise Exception("float() takes 1 argument")
                arg = self.generate(node.arguments[0])
                if arg.type == self.float_type:
                    return arg
                elif arg.type == self.int_type:
                    return self.builder.sitofp(arg, self.float_type, name="int_to_float")
                elif arg.type == self.bool_type:
                    int_val = self.builder.zext(arg, self.int_type, name="bool_to_int")
                    return self.builder.sitofp(int_val, self.float_type, name="int_to_float")
                else:
                    raise Exception(f"float() doesn't support type {arg.type}")
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

                # Check if it's an imported libc function
                if func is None and node.callee.name in self.libc_functions:
                    func = self.libc_functions[node.callee.name]

                if func is None:
                    raise Exception(f"Unknown function: {node.callee.name}")
                
                args = []
                for i, arg in enumerate(node.arguments):
                    arg_value = self.generate(arg)
                    if arg_value is None:
                        line = getattr(node, 'line', None)
                        column = getattr(node, 'column', None)
                        pos_info = f" at line {line}, column {column}" if line is not None else ""
                        func_name = getattr(node.callee, 'name', str(node.callee))
                        arg_name = getattr(arg, 'name', getattr(arg, 'value', str(arg)))
                        raise Exception(f"Argument {i+1} ('{arg_name}') evaluated to None in call to '{func_name}'{pos_info}. This usually means a type was not resolved during semantic analysis.")
                    args.append(arg_value)
                
                # Get position info for better error messages
                line = getattr(node, 'line', None)
                column = getattr(node, 'column', None)
                pos_info = f" at line {line}, column {column}" if line is not None else ""
                
                try:
                    call_result = self.builder.call(func, args)
                except TypeError as e:
                    # Enhance the error message with position info
                    func_name = getattr(node.callee, 'name', str(node.callee))
                    raise TypeError(f"Type mismatch in call to '{func_name}'{pos_info}: {e}")
                
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
        if isinstance(node, NullLiteral):
            return llvmlite.ir.Constant(self.i8_ptr_type, None)
        if isinstance(node, TypeofExpression):
            # typeof returns a string literal representing the type of the expression
            # First generate the argument to get its LLVM type
            arg_value = self.generate(node.argument)
            # Map LLVM type to type string
            if arg_value.type == self.int_type:
                type_str = "int"
            elif arg_value.type == self.float_type:
                type_str = "float"
            elif arg_value.type == self.i8_ptr_type:
                type_str = "string"
            elif arg_value.type == self.bool_type:
                type_str = "bool"
            elif arg_value.type == self.dyn_array_ptr_type:
                type_str = "array"
            elif hasattr(arg_value.type, 'elements'):
                # Struct type - likely a class instance
                type_str = "object"
            else:
                type_str = "unknown"
            return self.create_string_constant(type_str)
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
                    # Use None as element type marker - will be resolved at call site or use int_type
                    self.variables[param.name] = arg
                    self.variable_types[param.name] = llvm_type
                    self.array_lengths[param.name] = (None, 4)  # Unknown element type, use None marker
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
            # Skip if block is already terminated (dead code after a return)
            if self.builder.block.is_terminated:
                return None
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
        if isinstance(node, TryStatement):
            return self.generate_try_statement(node)
        if isinstance(node, ThrowStatement):
            return self.generate_throw_statement(node)
        if isinstance(node, IfStatement):
            func = self.builder.block.parent
            condition = self.generate(node.condition)
            if condition.type != llvmlite.ir.IntType(1):
                # For pointer types (like i8*), compare to null pointer
                if isinstance(condition.type, llvmlite.ir.PointerType):
                    null_ptr = llvmlite.ir.Constant(condition.type, None)
                    condition = self.builder.icmp_signed("!=", condition, null_ptr, name="tobool")
                else:
                    zero = llvmlite.ir.Constant(condition.type, 0)
                    condition = self.builder.icmp_signed("!=", condition, zero, name="tobool")
            then_block = func.append_basic_block(name="then")

            # Create else block BEFORE merge block so IR order is: then, else, merge
            if node.else_body:
                else_block = func.append_basic_block(name="else")
                merge_block = func.append_basic_block(name="merge")
                self.builder.cbranch(condition, then_block, else_block)
            else:
                merge_block = func.append_basic_block(name="merge")
                self.builder.cbranch(condition, then_block, merge_block)
            
            self.builder.position_at_end(then_block)
            for statement in node.then_body.statements:
                self.generate(statement)
            # After generating statements, builder might be at a different block (from nested if/while)
            # Track if then branch reaches merge (vs returning/etc)
            then_end_block = self.builder.block
            then_reaches_merge = not then_end_block.is_terminated
            if then_reaches_merge:
                self.builder.branch(merge_block)

            # Only process else block if it exists
            else_reaches_merge = True  # Default for no else body
            if node.else_body:
                self.builder.position_at_end(else_block)
                for statement in node.else_body.statements:
                    self.generate(statement)
                # Track if else branch reaches merge
                else_end_block = self.builder.block
                else_reaches_merge = not else_end_block.is_terminated
                if else_reaches_merge:
                    self.builder.branch(merge_block)

            # Position at merge block
            self.builder.position_at_end(merge_block)
            # Only add unreachable if NO paths reach merge
            if node.else_body:
                if not then_reaches_merge and not else_reaches_merge:
                    self.builder.unreachable()
            else:
                # No else branch - code after if always reachable (condition could be false)
                pass
        if isinstance(node, WhileStatement):
            func = self.builder.block.parent

            loop_block = func.append_basic_block(name="loop")
            body_block = func.append_basic_block(name="body")
            exit_block = func.append_basic_block(name="exit")

            # Save and set loop exit block for break statements
            old_exit_block = getattr(self, 'current_loop_exit_block', None)
            self.current_loop_exit_block = exit_block

            # Only branch if current block is not already terminated
            if not self.builder.block.is_terminated:
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
            # After generating body, builder might be at a different block (from nested control flow)
            # Branch back to loop from wherever we ended up, if not already terminated
            if not self.builder.block.is_terminated:
                self.builder.branch(loop_block)

            # Restore previous loop exit block
            if old_exit_block is not None:
                self.current_loop_exit_block = old_exit_block
            else:
                self.current_loop_exit_block = None

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
                # Variable reference - use Identifier generation to get proper value
                array_ptr = self.generate(node.iterable)
                elem_type, elem_size = self.array_lengths[node.iterable.name]
                # Handle unknown element type (None marker for array parameters)
                if elem_type is None:
                    # For array parameters, try to infer from variable type or default to int
                    var_type = self.variable_types.get(node.iterable.name)
                    if var_type == self.dyn_array_ptr_type:
                        # For dynamic arrays, we can't know element type at compile time
                        # Try to infer from how the array is used in the loop body
                        # For now, default to int_type but this is a limitation
                        elem_type = self.int_type
                    else:
                        elem_type = self.int_type
            else:
                # Function call or expression - evaluate to get array
                array_ptr = self.generate(node.iterable)

                # Determine element type from the call expression
                elem_type = self.int_type
                elem_size = 4

                if isinstance(node.iterable, CallExpression):
                    # Handle split() function call (from stdlib.strings)
                    if isinstance(node.iterable.callee, Identifier) and node.iterable.callee.name == "split":
                        elem_type = self.i8_ptr_type
                        elem_size = 8
                    elif isinstance(node.iterable.callee, MemberExpression):
                        method_name = node.iterable.callee.property
                        # For class methods, check if they return array
                        if isinstance(node.iterable.callee.object, Identifier):
                            obj_name = node.iterable.callee.object.name
                            if obj_name in self.variable_classes:
                                class_name = self.variable_classes[obj_name]
                                if class_name in self.classes:
                                    methods = self.classes[class_name].get('methods', {})
                                    if method_name in methods and methods[method_name].get('return_type') == 'array':
                                        # Class method returns array - default to string elements
                                        elem_type = self.i8_ptr_type
                                        elem_size = 8

            # Allocate loop index and loop variable
            index_ptr = self.builder.alloca(self.int_type, name="loop_index")
            self.builder.store(llvmlite.ir.Constant(self.int_type, 0), index_ptr)

            loop_var_ptr = self.builder.alloca(elem_type, name=node.variable)
            self.variables[node.variable] = loop_var_ptr
            self.variable_types[node.variable] = elem_type

            # Track element class type for field access in loop body
            elem_class_name = None
            if isinstance(node.iterable, Identifier):
                # Check if the array has a known element class type
                if node.iterable.name in self.array_element_classes:
                    elem_class_name = self.array_element_classes[node.iterable.name]
            # If element is a class pointer, try to infer class name from type
            if elem_class_name is None and elem_type == self.i8_ptr_type:
                # For i8* (class pointer), check if we can infer from context
                if isinstance(node.iterable, Identifier):
                    if node.iterable.name in self.array_lengths:
                        potential_class = self.array_element_classes.get(node.iterable.name)
                        if potential_class:
                            elem_class_name = potential_class

            if elem_class_name:
                self.variable_classes[node.variable] = elem_class_name

            # Create basic blocks for the loop structure
            loop_block = func.append_basic_block(name="for_loop")
            body_block = func.append_basic_block(name="for_body")
            exit_block = func.append_basic_block(name="for_exit")

            # Store exit block for break statements
            old_exit_block = getattr(self, 'current_loop_exit_block', None)
            self.current_loop_exit_block = exit_block

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
            old_variable_classes = dict(self.variable_classes)

            # Generate loop body statements
            for statement in node.body.statements:
                self.generate(statement)
                
                # If the body contains a return/break, the block is already terminated
                if self.builder.block.is_terminated:
                    break

            # Restore exit block
            if old_exit_block is not None:
                self.current_loop_exit_block = old_exit_block
            else:
                delattr(self, 'current_loop_exit_block')

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
            self.variable_classes = old_variable_classes
            self.builder.position_at_end(exit_block)
        
        if isinstance(node, BreakStatement):
            """Generate code for break statement"""
            exit_block = getattr(self, 'current_loop_exit_block', None)
            if exit_block is None:
                # Break outside of loop - undefined behavior, just return
                if self.current_return_type == self.void_type:
                    self.builder.ret_void()
                else:
                    self.builder.ret(llvmlite.ir.Constant(self.int_type, 0))
            else:
                self.builder.branch(exit_block)
    
    def generate_try_statement(self, node):
        """Generate code for try/catch statement using setjmp/longjmp"""
        # Check if builder is available (it won't be during module import)
        if not hasattr(self, 'builder') or self.builder is None or self.builder.block is None:
            # During module import - just analyze the try/catch blocks without generating code
            for statement in node.try_block.statements:
                self.generate(statement)
            for catch_block in node.catch_blocks:
                for statement in catch_block.body.statements:
                    self.generate(statement)
            return None
        
        func = self.builder.block.parent
        
        # Use the global jump buffer instead of a local one
        # This allows nested function calls to access it
        jmp_buf_ptr = self.builder.gep(self.global_jmp_buf, [self.create_int_constant(0), self.create_int_constant(0)], name="jmp_buf_ptr")
        # Cast to i8* for setjmp (it takes void*)
        jmp_buf_void_ptr = self.builder.bitcast(jmp_buf_ptr, self.i8_ptr_type, name="jmp_buf_void_ptr")
        
        # Create blocks for try-catch structure
        setjmp_block = func.append_basic_block(name="try_setjmp")
        try_block = func.append_basic_block(name="try_body")
        catch_block = func.append_basic_block(name="catch_body")
        continue_block = func.append_basic_block(name="try_continue")
        
        # Branch to setjmp block
        self.builder.branch(setjmp_block)
        
        # Setjmp block - call setjmp to save state
        self.builder.position_at_end(setjmp_block)
        setjmp_result = self.builder.call(self.setjmp, [jmp_buf_void_ptr], name="setjmp_result")
        
        # Compare setjmp result - 0 means normal execution, non-zero means exception
        zero = self.create_int_constant(0)
        is_exception = self.builder.icmp_signed("!=", setjmp_result, zero, name="is_exception")
        
        # Branch: if exception, go to catch; otherwise go to try body
        self.builder.cbranch(is_exception, catch_block, try_block)
        
        # Try block - execute the protected code
        self.builder.position_at_end(try_block)

        # Increment exception depth at runtime
        current_depth = self.builder.load(self.global_exception_depth, name="current_exc_depth")
        new_depth = self.builder.add(current_depth, self.create_int_constant(1), name="new_exc_depth")
        self.builder.store(new_depth, self.global_exception_depth)

        # Set the in-try flag so throw can check it
        self.variables["_in_try_block"] = True

        # Generate try block statements
        for statement in node.try_block.statements:
            self.generate(statement)
            if self.builder.block.is_terminated:
                break

        # Clear the in-try flag
        self.variables["_in_try_block"] = False

        # If try block completes without exception, continue
        if not self.builder.block.is_terminated:
            # Decrement exception depth before leaving try block
            depth_before_leave = self.builder.load(self.global_exception_depth, name="depth_before_leave")
            depth_after_leave = self.builder.sub(depth_before_leave, self.create_int_constant(1), name="depth_after_leave")
            self.builder.store(depth_after_leave, self.global_exception_depth)
            self.builder.branch(continue_block)

        # Catch block - handle the exception
        self.builder.position_at_end(catch_block)

        # Decrement exception depth since we're leaving the try context
        depth_in_catch = self.builder.load(self.global_exception_depth, name="depth_in_catch")
        depth_after_catch = self.builder.sub(depth_in_catch, self.create_int_constant(1), name="depth_after_catch")
        self.builder.store(depth_after_catch, self.global_exception_depth)

        # Retrieve exception info from global variables
        exc_type_tag = self.builder.load(self.global_exception_type, name="exc_type_tag")
        exc_message = self.builder.load(self.global_exception_message, name="exc_message")

        # Generate catch block statements
        for catch_clause in node.catch_blocks:
            # Store exception variable for catch block
            if catch_clause.identifier:
                self.variables[catch_clause.identifier] = exc_message
                self.variable_types[catch_clause.identifier] = self.i8_ptr_type
                self.variable_classes[catch_clause.identifier] = "Exception"

            for statement in catch_clause.body.statements:
                self.generate(statement)
            if self.builder.block.is_terminated:
                break

        # Branch to continue if not terminated
        if not self.builder.block.is_terminated:
            self.builder.branch(continue_block)

        # Continue block
        self.builder.position_at_end(continue_block)

        return None

        func = self.builder.block.parent
        
        # Allocate jump buffer on the stack
        jmp_buf = self.builder.alloca(self.jmp_buf_type, name="jmp_buf")
        
        # Create blocks for try-catch structure
        setjmp_block = func.append_basic_block(name="try_setjmp")
        try_block = func.append_basic_block(name="try_body")
        catch_block = func.append_basic_block(name="catch_body")
        continue_block = func.append_basic_block(name="try_continue")
        
        # Branch to setjmp block
        self.builder.branch(setjmp_block)
        
        # Setjmp block - call setjmp to save state
        self.builder.position_at_end(setjmp_block)
        setjmp_result = self.builder.call(self.setjmp, [jmp_buf], name="setjmp_result")
        
        # Compare setjmp result - 0 means normal execution, non-zero means exception
        zero = self.create_int_constant(0)
        is_exception = self.builder.icmp_signed("!=", setjmp_result, zero, name="is_exception")
        
        # Branch: if exception, go to catch; otherwise go to try body
        self.builder.cbranch(is_exception, catch_block, try_block)
        
        # Try block - execute the protected code
        self.builder.position_at_end(try_block)
        
        # Store the current jump buffer so throw can use it
        self.variables["_current_jmp_buf"] = jmp_buf
        self.variables["_in_try_block"] = True
        
        # Generate try block statements
        for statement in node.try_block.statements:
            self.generate(statement)
            if self.builder.block.is_terminated:
                break
        
        # Clear the in-try flag
        self.variables["_in_try_block"] = False
        
        # If try block completes without exception, continue
        if not self.builder.block.is_terminated:
            self.builder.branch(continue_block)
        
        # Catch block - handle the exception
        self.builder.position_at_end(catch_block)
        
        # Generate catch block statements
        for catch_clause in node.catch_blocks:
            for statement in catch_clause.body.statements:
                self.generate(statement)
            if self.builder.block.is_terminated:
                break
        
        # Branch to continue if not terminated
        if not self.builder.block.is_terminated:
            self.builder.branch(continue_block)
        
        # Continue block
        self.builder.position_at_end(continue_block)
        
        return None
    
    def generate_catch_handlers(self, node, exc_type_tag, exc_message):
        """Generate catch block handlers"""
        func = self.builder.block.parent
        
        # Get or create landing_pad from variables (set by generate_try_statement)
        landing_pad = self.variables.get("_landing_pad")
        
        # Create continue block for after catch handling
        catch_continue = func.append_basic_block(name="catch_continue")
        
        # Process each catch block
        for i, catch_block in enumerate(node.catch_blocks):
            handler_block = func.append_basic_block(name=f"catch_handler_{i}")
            
            # For typed catches, generate type comparison
            if catch_block.exception_type and i == 0:
                # Only do type check for the first catch
                expected_tag = self.get_exception_type_tag(catch_block.exception_type)
                expected_tag_val = self.builder.load(expected_tag, name=f"expected_tag_{catch_block.exception_type}")
                is_match = self.builder.icmp_signed("==", exc_type_tag, expected_tag_val, name=f"is_match")
                
                # Create next block for fallthrough
                next_block = func.append_basic_block(name="catch_next")
                
                # Branch based on type match
                self.builder.cond_br(is_match, handler_block, next_block)
                
                # Position at next block for potential fallthrough
                self.builder.position_at_end(next_block)
            else:
                # Catch-all or subsequent catches - branch to handler
                if i > 0:
                    # This is a fallthrough from previous catch
                    pass
            
            # Generate handler body
            self.builder.position_at_end(handler_block)
            
            # Store exception info for binding
            if catch_block.identifier:
                self.variables[catch_block.identifier] = exc_message
                # Set variable type if exception type is known
                if catch_block.exception_type:
                    self.variable_types[catch_block.identifier] = catch_block.exception_type
                    self.variable_classes[catch_block.identifier] = catch_block.exception_type
            
            # Generate catch body
            for statement in catch_block.body.statements:
                self.generate(statement)
            
            # Branch to continue if not terminated
            if not self.builder.block.is_terminated:
                self.builder.branch(catch_continue)
        
        # Position at continue block
        self.builder.position_at_end(catch_continue)
    
    def generate_throw_statement(self, node):
        """Generate code for throw statement"""
        if not hasattr(self, 'builder') or self.builder is None or self.builder.block is None:
            return None

        # If no expression, print generic error and exit
        if node.expression is None:
            error_msg = self.create_string_constant("Error thrown, exiting.\n")
            self.builder.call(self.printf, [error_msg], name="print_error")
            self.builder.call(self.exit, [llvmlite.ir.Constant(self.int_type, 1)], name="exit_on_throw")
            self.builder.unreachable()
            return None

        # Determine the class name and get the exception object pointer
        class_name = None
        exc_obj_ptr = None

        if isinstance(node.expression, NewExpression):
            class_name = node.expression.class_name
            exc_obj_ptr = self.generate(node.expression)
        elif isinstance(node.expression, Identifier):
            var_name = node.expression.name
            if var_name in self.variable_classes:
                class_name = self.variable_classes[var_name]
            if var_name in self.variables:
                exc_obj_ptr = self.variables[var_name]

        if class_name is None or exc_obj_ptr is None:
            error_msg = self.create_string_constant("Uncaught exception\n")
            self.builder.call(self.printf, [error_msg], name="print_uncaught")
            self.builder.call(self.exit, [llvmlite.ir.Constant(self.int_type, 1)], name="exit_on_exception")
            self.builder.unreachable()
            return None

        # Store exception type tag in global variable
        tag_global = self.get_exception_type_tag(class_name)
        tag_value = self.builder.load(tag_global, name=f"tag_{class_name}")
        self.builder.store(tag_value, self.global_exception_type)

        # Allocate exception object on heap (must persist across longjmp)
        struct_type = self.classes[class_name]['llvm_struct']
        struct_size = self.get_type_size(struct_type)
        heap_ptr = self.builder.call(self.malloc, [struct_size], name="exc_heap_ptr")
        heap_ptr_typed = self.builder.bitcast(heap_ptr, struct_type.as_pointer(), name="exc_heap_typed")

        # Copy exception object to heap (use i8* for memcpy)
        exc_obj_i8ptr = self.builder.bitcast(exc_obj_ptr, self.i8_ptr_type, name="exc_obj_i8ptr")
        self.builder.call(self.memcpy, [heap_ptr, exc_obj_i8ptr, struct_size, self.create_int_constant(0)], name="exc_copy")

        # Store heap pointer (cast to i8*) in global variable
        exc_obj_i8ptr = self.builder.bitcast(heap_ptr_typed, self.i8_ptr_type, name="exc_obj_i8ptr")
        self.builder.store(exc_obj_i8ptr, self.global_exception_message)

        # Check if we're in a try block and use longjmp
        in_try = self.variables.get("_in_try_block", False)
        if in_try and hasattr(self, 'global_jmp_buf'):
            jmp_buf_ptr = self.builder.gep(self.global_jmp_buf, [self.create_int_constant(0), self.create_int_constant(0)], name="jmp_buf_for_longjmp")
            jmp_buf_void_ptr = self.builder.bitcast(jmp_buf_ptr, self.i8_ptr_type, name="jmp_buf_void_ptr")
            self.builder.call(self.longjmp, [jmp_buf_void_ptr, self.create_int_constant(1)], name="longjmp_throw")

        # If not in try block or no global_jmp_buf, print error and exit
        error_msg = self.create_string_constant("Uncaught exception\n")
        self.builder.call(self.printf, [error_msg], name="print_uncaught")
        self.builder.call(self.exit, [llvmlite.ir.Constant(self.int_type, 1)], name="exit_on_exception")
        self.builder.unreachable()

        return None
        
    def generate_in_expression(self, node):
        """Generate code for 'item in collection' expression - returns bool"""
        item = self.generate(node.item)
        collection = self.generate(node.container)
        
        func = self.builder.block.parent
        
        # Create blocks for the comparison loop
        loop_block = func.append_basic_block(name="in_loop")
        found_block = func.append_basic_block(name="in_found")
        not_found_block = func.append_basic_block(name="in_not_found")
        continue_block = func.append_basic_block(name="in_continue")
        
        # Get array length
        len_ptr = self.builder.gep(collection, [self.create_int_constant(0), self.create_int_constant(0)], name="len_ptr")
        arr_length = self.builder.load(len_ptr, name="arr_length")
        
        # Initialize index to 0
        index_ptr = self.builder.alloca(self.int_type, name="index")
        self.builder.store(self.create_int_constant(0), index_ptr)
        
        # Branch to loop
        self.builder.branch(loop_block)
        
        # Loop block - compare current element with item
        self.builder.position_at_end(loop_block)
        index = self.builder.load(index_ptr, name="index")
        cond = self.builder.icmp_signed("<", index, arr_length, name="loop_cond")
        self.builder.cbranch(cond, found_block, not_found_block)
        
        # Found block - get element and compare
        self.builder.position_at_end(found_block)
        data_ptr_ptr = self.builder.gep(collection, [self.create_int_constant(0), self.create_int_constant(2)], name="data_ptr_ptr")
        data_ptr = self.builder.load(data_ptr_ptr, name="data_ptr")
        
        # Array stores i8* (string pointers), so load as i8**
        str_array_ptr = self.builder.bitcast(data_ptr, llvmlite.ir.PointerType(self.i8_ptr_type), name="str_array_ptr")
        elem_ptr_ptr = self.builder.gep(str_array_ptr, [index], name="elem_ptr_ptr")
        elem = self.builder.load(elem_ptr_ptr, name="elem")
        
        # Compare item with elem using strcmp
        cmp_result = self.builder.call(self.strcmp, [item, elem], name="strcmp_result")
        zero = self.create_int_constant(0)
        is_equal = self.builder.icmp_signed("==", cmp_result, zero, name="is_equal")
        
        # If found, go to continue with result=true; otherwise, increment index and continue loop
        increment_block = func.append_basic_block(name="in_increment")
        self.builder.cbranch(is_equal, continue_block, increment_block)
        
        # Increment index block
        self.builder.position_at_end(increment_block)
        next_index = self.builder.add(index, self.create_int_constant(1), name="next_index")
        self.builder.store(next_index, index_ptr)
        self.builder.branch(loop_block)
        
        # Not found block - go to continue with result=false
        self.builder.position_at_end(not_found_block)
        self.builder.branch(continue_block)
        
        # Continue block - return result
        self.builder.position_at_end(continue_block)
        
        # Create phi node for result
        result_phi = self.builder.phi(self.bool_type, name="in_result")
        result_phi.add_incoming(llvmlite.ir.Constant(self.bool_type, 1), found_block)
        result_phi.add_incoming(llvmlite.ir.Constant(self.bool_type, 0), not_found_block)
        
        return result_phi

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