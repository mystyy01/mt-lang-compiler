from ast_nodes import *
import os

class SymbolTable:
    def __init__(self):
        self.scopes = []
        self.enter_scope() # create global scope
    def enter_scope(self):
        self.scopes.append({})
    def exit_scope(self):
        self.scopes.pop()
    def declare(self, name, symbol_type, data_type):
        current_scope = self.scopes[-1]
        current_scope[name] = {"symbol_type": symbol_type, "data_type": data_type}
    def lookup(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        return None
class SemanticAnalyzer:
    def __init__(self, file_path=None):
        self.symbol_table = SymbolTable()
        self.classes = {}  # Initialize class registry
        self.symbol_table.declare("print", "builtin", "void")
        # Declare built-in array methods
        self.symbol_table.declare("length", "builtin", "int")
        self.symbol_table.declare("append", "builtin", "void")
        # Declare built-in type conversion functions
        self.symbol_table.declare("str", "builtin", "string")
        self.symbol_table.declare("int", "builtin", "int")
        self.symbol_table.declare("float", "builtin", "float")
        # Declare built-in input function
        self.symbol_table.declare("read", "builtin", "string")
        # Declare libc functions
        self.symbol_table.declare("fopen", "builtin", "string")
        self.symbol_table.declare("fclose", "builtin", "int")
        self.symbol_table.declare("fread", "builtin", "int")
        self.symbol_table.declare("fseek", "builtin", "int")
        self.symbol_table.declare("ftell", "builtin", "int")
        self.symbol_table.declare("malloc", "builtin", "string")
        self.symbol_table.declare("free", "builtin", "void")
        self.symbol_table.declare("strlen", "builtin", "int")
        self.symbol_table.declare("strcpy", "builtin", "string")
        self.symbol_table.declare("strcat", "builtin", "string")
        self.symbol_table.declare("strcmp", "builtin", "int")
        self.symbol_table.declare("sprintf", "builtin", "int")
        self.symbol_table.declare("printf", "builtin", "int")
        self.symbol_table.declare("fgets", "builtin", "string")
        self.symbol_table.declare("fwrite", "builtin", "void")
        self.symbol_table.declare("fputs", "builtin", "int")
        self.errors = []
        self.file_path = file_path or "unknown"
    
    def get_position_info(self, node):
        """Get formatted position information for error messages"""
        pos_info = ""
        if hasattr(node, 'line') and node.line is not None:
            pos_info = f" at line {node.line}"
            if hasattr(node, 'column') and node.column is not None:
                pos_info += f", column {node.column}"
        
        return pos_info
    
    def add_error(self, message, include_file=True):
        """Add an error with file information"""
        if include_file:
            file_info = f" in {self.file_path}" if self.file_path and self.file_path != "unknown" else ""
            full_message = f"{message}{file_info}"
        else:
            full_message = message
        self.errors.append(full_message)
    def analyze(self, node):
        print(f"DEBUG: Analyzing {node.__class__.__name__}: {node}")
        if isinstance(node, Program):
            return self.analyze_program(node)
        if isinstance(node, ClassDeclaration):
            return self.analyze_class_declaration(node)
        if isinstance(node, VariableDeclaration):
            return self.analyze_variable_declaration(node)
        if isinstance(node, FunctionDeclaration):
            return self.analyze_function_declaration(node)
        if isinstance(node, DynamicFunctionDeclaration):
            return self.analyze_dynamic_function_declaration(node)
        if isinstance(node, FromImportStatement):
            return self.analyze_from_import(node)
        if isinstance(node, SimpleImportStatement):
            return self.analyze_simple_import(node)
        if isinstance(node, NumberLiteral):
            return self.analyze_number_literal(node)
        if isinstance(node, FloatLiteral):
            return self.analyze_float_literal(node)
        if isinstance(node, ArrayLiteral):
            return self.analyze_array_literal(node)
        if isinstance(node, StringLiteral):
            return self.analyze_string_literal(node)
        if isinstance(node, NewExpression):
            return self.analyze_new_expression(node)
        if isinstance(node, Identifier):
            return self.analyze_identifier(node)
        if isinstance(node, CallExpression):
            return self.analyze_call_expression(node)
        if isinstance(node, BinaryExpression):
            return self.analyze_binary_expression(node)
        if isinstance(node, MemberExpression):
            return self.analyze_member_expression(node)
        if isinstance(node, ExpressionStatement):
            return self.analyze_expression_statement(node)
        # cant implement bools yet since they arent an AST class yet (adding soon)
    def analyze_program(self, node: Program):
        for statement in node.statements:
            self.analyze(statement)
    def analyze_class_declaration(self, node: ClassDeclaration):
        # Declare the class in the symbol table
        self.symbol_table.declare(node.name, "class", node.name)

        # Enter class scope for analyzing fields and methods
        self.symbol_table.enter_scope()

        # Store current class for method analysis
        old_current_class = getattr(self, 'current_class', None)
        self.current_class = node.name

        # Analyze fields
        for field in node.fields:
            self.analyze_field_declaration(field)

        # Analyze methods
        for method in node.methods:
            self.analyze_method_declaration(method)

        # Restore current_class
        self.current_class = old_current_class
            
        # Debug: Print class info
        print(f"DEBUG: Analyzing class '{node.name}' with {len(node.fields)} fields and {len(node.methods)} methods")

        self.symbol_table.exit_scope()
        
        # Update class registry after analyzing this class
        if not hasattr(self, 'classes'):
            self.classes = {}
        fields_info = []
        for field in node.fields:
            fields_info.append({
                'name': field.name,
                'type': field.type,
                'is_constructor_arg': field.is_constructor_arg
            })
        methods_info = {}
        for method in node.methods:
            methods_info[method.name] = {
                'return_type': method.return_type,
                'params': [(p.name, p.param_type) for p in method.params]
            }
        self.classes[node.name] = {
            "symbol_type": "class",
            "data_type": node.name,
            "fields": fields_info,
            "methods": methods_info
        }
        self.current_class = node.name

    def analyze_field_declaration(self, node: FieldDeclaration):
        # Fields are just stored in the class scope
        self.symbol_table.declare(node.name, "field", node.type)

        # Analyze initializer if present
        if node.initializer:
            init_type = self.analyze(node.initializer)
            # Check type compatibility
            if init_type != node.type and init_type != "any":
                pos_info = self.get_position_info(node)
                self.add_error(f"Field initializer type mismatch{pos_info}. Cannot assign {init_type} to {node.type}")

    def analyze_method_declaration(self, node: MethodDeclaration):
        # Declare method in current scope (class scope)
        self.symbol_table.declare(node.name, "method", node.return_type)

        # Enter method scope
        self.symbol_table.enter_scope()

        # Declare 'this' parameter
        self.symbol_table.declare("this", "parameter", self.current_class)

        # Declare parameters
        for param in node.params:
            self.symbol_table.declare(param.name, "parameter", param.param_type or "any")

        # Analyze method body
        if node.body and hasattr(node.body, 'statements') and node.body.statements:
            for statement in node.body.statements:
                self.analyze(statement)

        self.symbol_table.exit_scope()

    def analyze_new_expression(self, node: NewExpression):
        # Check that the class exists
        class_symbol = self.symbol_table.lookup(node.class_name)
        if not class_symbol or class_symbol["symbol_type"] != "class":
            pos_info = self.get_position_info(node)
            self.add_error(f"Unknown class '{node.class_name}'{pos_info}")
            return "unknown"

        # For now, we can't check constructor arguments here because we don't have
        # access to the class definition during semantic analysis of the importing file.
        # This would need to be checked during code generation or a separate pass.

        return node.class_name  # Return the class name as the type



    def analyze_member_expression(self, node: MemberExpression):
        # Analyze the object
        object_type = self.analyze(node.object)
        
        # If this is a 'this' expression, look up the field in current class
        if hasattr(node.object, '__class__') and node.object.__class__.__name__ == 'ThisExpression':
            if hasattr(self, 'current_class') and self.current_class:
                class_info = self.classes.get(self.current_class, {})
                fields = class_info.get('fields', [])
                for field in fields:
                    if field['name'] == node.property:
                        return field['type']
                # Check if it's a method
                # For now, return any for methods
                return "any"
        
        # For module.function calls, return "any" since we don't track module function types
        if object_type == "module":
            return "any"
        return object_type

    def analyze_variable_declaration(self, node: VariableDeclaration):
        # Check if the declared type is valid (built-in or user-defined class)
        type_symbol = self.symbol_table.lookup(node.type)
        if not type_symbol and node.type not in ["int", "float", "string", "bool", "array", "void"]:
            pos_info = self.get_position_info(node)
            self.add_error(f"Unknown type '{node.type}'{pos_info}")

        if node.value:
            type_of_node = self.analyze(node.value)
            # "any" is a wildcard that matches any type
            if type_of_node != node.type and type_of_node != "any":
                # Allow assignment if both are user-defined classes or if value is a new expression of the right type
                if not (isinstance(node.value, NewExpression) and node.value.class_name == node.type):
                    pos_info = self.get_position_info(node)
                    print(f"DEBUG: Assignment type mismatch: {type_of_node} to {node.type}")
                    print(f"DEBUG: Assignment node: {node}")
                    self.add_error(f"Type mismatch{pos_info}. Cannot assign type {type_of_node} to {node.type}")
        self.symbol_table.declare(node.name, "variable", node.type)
    def analyze_function_declaration(self, node: FunctionDeclaration):
        self.symbol_table.declare(node.name, "function", node.return_type)
        self.symbol_table.enter_scope()
        for param in node.parameters:
            self.symbol_table.declare(param.name, "parameter", "any")
        for statement in node.body.statements:
            self.analyze(statement)
        self.symbol_table.exit_scope()
    
    def analyze_dynamic_function_declaration(self, node: DynamicFunctionDeclaration):
        # Dynamic function with statically typed parameters and dynamic return
        self.symbol_table.declare(node.name, "function", "any")
        self.symbol_table.enter_scope()
        
        # Validate and declare parameters with their static types
        for param in node.parameters:
            if param.param_type is None:
                pos_info = self.get_position_info(node)
                self.add_error(f"Dynamic function '{node.name}' requires type annotation for parameter '{param.name}'{pos_info}")
                param.param_type = "int"  # Default fallback
            self.symbol_table.declare(param.name, "parameter", param.param_type)
        
        # Analyze function body
        for statement in node.body.statements:
            self.analyze(statement)
        
        self.symbol_table.exit_scope()
        return "any"  # Dynamic function can return any type
        
    def analyze_identifier(self, node: Identifier):
        res = self.symbol_table.lookup(node.name) # check if its declared or not
        if not res:
            pos_info = self.get_position_info(node)
            self.add_error(f"Undeclared variable '{node.name}'{pos_info}")
            return
        return res["data_type"]
    def analyze_expression_statement(self, node: ExpressionStatement):
        self.analyze(node.expression)
    def analyze_set_statement(self, node: SetStatement):
        # Analyze the value
        self.analyze(node.value)
    def analyze_binary_expression(self, node: BinaryExpression):
        left_type = self.analyze(node.left)
        right_type = self.analyze(node.right)
        print(f"DEBUG: Binary op {node.operator.value}: {left_type} + {right_type}")
        print(f"DEBUG: Left node: {node.left}")
        print(f"DEBUG: Right node: {node.right}")
        
        # Comparison operators always return bool
        if node.operator.value in ["==", "!=", ">", "<", ">=", "<="]:
            return "bool"
        
        # Logical operators always return bool (handle before arithmetic promotion)
        elif node.operator.value in ["&&", "||"]:
            return "bool"
        
        # Arithmetic operators use type promotion
        else:  # +, -, *, /
            if left_type == "float" or right_type == "float":
                return "float"
            elif left_type == "int" or right_type == "int":
                return "int"
            elif left_type == "bool" or right_type == "bool":
                return "bool"
            else:
                # For mixed types (string, array, etc.), return "any" for now
                return "any"

    def analyze_if_statement(self, node: IfStatement):
        self.analyze(node.condition)
        self.symbol_table.enter_scope()
        for statement in node.then_body.statements:
            self.analyze(statement)
        self.symbol_table.exit_scope()
        if node.else_body:
            self.symbol_table.enter_scope()
            for statement in node.else_body.statements:
                self.analyze(statement)
            self.symbol_table.exit_scope()

    def analyze_for_statement(self, node: ForInStatement):
        self.analyze(node.iterable)
        self.symbol_table.enter_scope()
        self.symbol_table.declare(node.variable, "variable", "any")
        for statement in node.body.statements:
            self.analyze(statement)
        self.symbol_table.exit_scope()
    def analyze_simple_import(self, node: SimpleImportStatement):
        if node.alias:
            self.symbol_table.declare(node.alias, "import", "module")
        else:
            self.symbol_table.declare(node.module_name, "import", "module")
    def analyze_from_import(self, node: FromImportStatement):
        # Load the module and analyze it to find classes and functions
        module_path = node.module_path

        if isinstance(module_path, Identifier):
            module_name = module_path.name
        elif isinstance(module_path, MemberExpression):
            # Handle module paths like stdlib.io
            if isinstance(module_path.object, Identifier) and isinstance(module_path.property, str):
                if module_path.object.name == "stdlib":
                    module_name = module_path.property
                else:
                    return
            else:
                return
        else:
            # Handle more complex module paths if needed
            return

        print(f"module_name = {module_name}")
        # Try to load the module
        try:
            # Check stdlib first
            stdlib_path = os.path.join(os.path.dirname(__file__), "stdlib", module_name + ".mtc")
            if os.path.exists(stdlib_path):
                with open(stdlib_path, "r") as f:
                    module_source = f.read()
            else:
                # Could check other paths, but for now just skip
                return

            # Parse and analyze the module
            from tokenizer import Tokenizer
            from parser import Parser
            tokens = Tokenizer(module_source, stdlib_path).tokenize()
            module_ast = Parser(tokens, stdlib_path).parse_program()

            # Analyze the module to find classes
            module_analyzer = SemanticAnalyzer(stdlib_path)
            module_analyzer.analyze(module_ast)
            
            # Update our class registry with the findings
            if hasattr(module_analyzer, 'classes'):
                if not hasattr(self, 'classes'):
                    self.classes = {}
                # Merge module classes into our registry
                self.classes.update(module_analyzer.classes)

            # Debug: Print all symbols found in module
            print(f"DEBUG: Module symbol table contains:")
            for scope in module_analyzer.symbol_table.scopes:
                for name, info in scope.items():
                    print(f"  {name}: {info}")
            
            # Register imported symbols
            for symbol in node.symbols:
                print(f"DEBUG: Looking for symbol '{symbol}'")
                # Check if it's a class in the module
                class_symbol = module_analyzer.symbol_table.lookup(symbol)
                print(f"DEBUG: Found symbol: {class_symbol}")
                if class_symbol and class_symbol["symbol_type"] == "class":
                    # Register the class in our symbol table
                    self.symbol_table.declare(symbol, "class", symbol)
                    # Also add to a separate class registry for codegen
                    if not hasattr(self, 'classes'):
                        self.classes = {}
                    # Copy class information from module
                    module_class_info = module_analyzer.classes.get(symbol, {})
                    self.classes[symbol] = module_class_info
                    print(f"DEBUG: Registered class '{symbol}' with fields: {module_class_info.get('fields', [])}")
                else:
                    # It's a function - use its actual return type from the module analysis
                    if class_symbol:
                        self.symbol_table.declare(symbol, "function", class_symbol["data_type"])
                    else:
                        # Fallback for unknown symbols
                        self.symbol_table.declare(symbol, "function", "any")

        except Exception as e:
            # If we can't load the module, just declare as function for now
            for symbol in node.symbols:
                self.symbol_table.declare(symbol, "function", "any")

    def analyze_call_expression(self, node: CallExpression):
        func_name = None
        if isinstance(node.callee, Identifier):
            func_name = node.callee.name
            print(f"DEBUG: Analyzing call to {func_name}")
            # Check for built-in libc functions
            libc_functions = {
                "fopen": ("i8*", ["i8*", "i8*"]),  # FILE*
                "fclose": ("int", ["i8*"]),        # int
                "fread": ("int", ["i8*", "int", "int", "i8*"]),  # size_t
                "fseek": ("int", ["i8*", "int", "int"]),  # int
                "ftell": ("int", ["i8*"]),        # long
                "malloc": ("i8*", ["int"]),       # void*
                "free": ("void", ["i8*"]),        # void
            }

            # Check if function exists in symbol table
            func_symbol = self.symbol_table.lookup(func_name)
            if not func_symbol:
                pos_info = self.get_position_info(node)
                self.add_error(f"Unknown function '{func_name}'{pos_info}")
                return "unknown"
            
            # For built-in functions, return their declared type
            if func_symbol.get("symbol_type") == "builtin":
                result = func_symbol.get("data_type", "unknown")
                print(f"DEBUG: Builtin {func_name} returns {result}")
                return result
            else:
                # For user-defined functions, we'd need more sophisticated tracking
                result = func_symbol.get("data_type", "unknown")
                print(f"DEBUG: User function {func_name} returns {result}")
                return result
                
                # Handle built-in function return types
                if func_name == "read":
                    return "string"
                elif func_name == "str":
                    return "string"
                elif func_name == "int":
                    return "int"
                elif func_name == "float":
                    return "float"
                elif func_name == "fopen":
                    return "string"
                elif func_name == "fclose":
                    return "int"
                elif func_name == "fread":
                    return "int"
                elif func_name == "fseek":
                    return "int"
                elif func_name == "ftell":
                    return "int"
                elif func_name == "malloc":
                    return "string"
                elif func_name == "free":
                    return "void"
                elif func_name == "strlen":
                    return "int"
                elif func_name == "strcpy":
                    return "string"
                elif func_name == "strcat":
                    return "string"
                elif func_name == "strcmp":
                    return "int"
                elif func_name == "sprintf":
                    return "int"
                elif func_name == "printf":
                    return "int"
                elif func_name == "fgets":
                    return "string"
                elif func_name == "fwrite":
                    return "void"
                elif func_name == "fputs":
                    return "int"

        # Analyze arguments
        for arg in node.arguments:
            self.analyze(arg)

        # For method calls, try to determine return type
        if isinstance(node.callee, MemberExpression):
            method_name = node.callee.property

            # Handle string literal method calls: "text".split(",")
            if isinstance(node.callee.object, StringLiteral):
                if method_name == "split":
                    return "array"
                elif method_name == "length":
                    return "int"

            # Handle method calls on variables: str.split(",")
            if isinstance(node.callee.object, Identifier):
                obj_name = node.callee.object.name
                obj_symbol = self.symbol_table.lookup(obj_name)
                if obj_symbol and obj_symbol["symbol_type"] == "variable":
                    obj_type = obj_symbol["data_type"]

                    # String methods (built into the compiler)
                    if obj_type == "string":
                        if method_name == "split":
                            return "array"
                        elif method_name == "length":
                            return "int"

                    # Look up method return type from class definition
                    if obj_type in self.classes:
                        class_info = self.classes[obj_type]
                        methods = class_info.get("methods", {})
                        if method_name in methods:
                            return methods[method_name]["return_type"]

        return "unknown"  # We don't know the return type
    def analyze_number_literal(self, node):
        return "int"
    def analyze_float_literal(self, node):
        return "float"
    def analyze_string_literal(self, node):
        return "string"
    def analyze_array_literal(self, node):
        if len(node.elements) == 0:
            return "array"

        first_type = self.analyze(node.elements[0])
        for element in node.elements[1:]:
            elem_type = self.analyze(element)
            if elem_type != first_type:
                self.add_error(f"Array elements must all be the same type. Expected {first_type}, got {elem_type}")

        return "array"
    def analyze_bool_literal(self, node):
        return "bool"