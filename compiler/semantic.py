from ast_nodes import *
from codegen import LIBC_FUNCTIONS
import os

class SymbolTable:
    def __init__(self):
        self.scopes = []
        self.enter_scope() # create global scope
    def enter_scope(self):
        self.scopes.append({})
    def exit_scope(self):
        self.scopes.pop()
    def declare(self, name, symbol_type, data_type, parameters=None, element_type=None):
        current_scope = self.scopes[-1]
        current_scope[name] = {"symbol_type": symbol_type, "data_type": data_type, "parameters": parameters, "element_type": element_type}
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
        # print(f"DEBUG: Analyzing {node.__class__.__name__}: {node}")
        if isinstance(node, Program):
            return self.analyze_program(node)
        if isinstance(node, ClassDeclaration):
            return self.analyze_class_declaration(node)
        if isinstance(node, VariableDeclaration):
            return self.analyze_variable_declaration(node)
        if isinstance(node, FunctionDeclaration):
            return self.analyze_function_declaration(node)
        if isinstance(node, ExternalDeclaration):
            return self.analyze_external_declaration(node)
        if isinstance(node, DynamicFunctionDeclaration):
            return self.analyze_dynamic_function_declaration(node)
        if isinstance(node, FromImportStatement):
            return self.analyze_from_import(node)
        if isinstance(node, LibcImportStatement):
            return self.analyze_libc_import(node)
        if isinstance(node, SimpleImportStatement):
            return self.analyze_simple_import(node)
        if isinstance(node, NumberLiteral):
            return self.analyze_number_literal(node)
        if isinstance(node, FloatLiteral):
            return self.analyze_float_literal(node)
        if isinstance(node, ArrayLiteral):
            return self.analyze_array_literal(node)
        if isinstance(node, DictLiteral):
            return self.analyze_dict_literal(node)
        if isinstance(node, StringLiteral):
            return self.analyze_string_literal(node)
        if isinstance(node, NullLiteral):
            return self.analyze_null_literal(node)
        if isinstance(node, NewExpression):
            return self.analyze_new_expression(node)
        if isinstance(node, Identifier):
            return self.analyze_identifier(node)
        if isinstance(node, CallExpression):
            return self.analyze_call_expression(node)
        if isinstance(node, BinaryExpression):
            return self.analyze_binary_expression(node)
        if isinstance(node, InExpression):
            return self.analyze_in_expression(node)
        if isinstance(node, IndexExpression):
            return self.analyze_index_expression(node)
        if isinstance(node, MemberExpression):
            return self.analyze_member_expression(node)
        if isinstance(node, ExpressionStatement):
            return self.analyze_expression_statement(node)
        if isinstance(node, WhileStatement):
            return self.analyze_while_statement(node)
        if isinstance(node, IfStatement):
            return self.analyze_if_statement(node)
        if isinstance(node, ForInStatement):
            return self.analyze_for_in_statement(node)
        if isinstance(node, SetStatement):
            return self.analyze_set_statement(node)
        if isinstance(node, ReturnStatement):
            return self.analyze_return_statement(node)
        if isinstance(node, Block):
            return self.analyze_block(node)
        if isinstance(node, TryStatement):
            return self.analyze_try_statement(node)
        if isinstance(node, CatchBlock):
            return self.analyze_catch_block(node)
        if isinstance(node, ThrowStatement):
            return self.analyze_throw_statement(node)
        if isinstance(node, BreakStatement):
            return self.analyze_break_statement(node)
        if isinstance(node, BoolLiteral):
            return "bool"
        if isinstance(node, TypeofExpression):
            self.analyze(node.argument)
            return "string"
        if isinstance(node, HasattrExpression):
            return self.analyze_hasattr_expression(node)
        if isinstance(node, ClassofExpression):
            self.analyze(node.argument)
            return "string"
        # cant implement bools yet since they arent an AST class yet (adding soon)
    def analyze_program(self, node: Program):
        for statement in node.statements:
            self.analyze(statement)
    def analyze_class_declaration(self, node: ClassDeclaration):
        # Declare the class in the symbol table
        self.symbol_table.declare(node.name, "class", node.name)

        # Store current class for method analysis
        old_current_class = getattr(self, 'current_class', None)
        self.current_class = node.name

        # Register class info FIRST (before analyzing method bodies)
        # so that this.method() calls can look up method parameters
        if not hasattr(self, 'classes'):
            self.classes = {}
        fields_info = []
        for field in node.fields:
            fields_info.append({
                'name': field.name,
                'type': field.type,
                'is_constructor_arg': field.is_constructor_arg,
                'initializer': field.initializer,
                'element_type': getattr(field, 'element_type', None)
            })
        methods_info = {}
        for method in node.methods:
            methods_info[method.name] = {
                'return_type': method.return_type,
                'params': method.params  # Store full Parameter objects
            }
        self.classes[node.name] = {
            "symbol_type": "class",
            "data_type": node.name,
            "fields": fields_info,
            "methods": methods_info,
            "parent_class": node.inherits_from
        }

        # Enter class scope for analyzing fields and methods
        self.symbol_table.enter_scope()

        # Analyze fields
        for field in node.fields:
            self.analyze_field_declaration(field)

        # Analyze methods (now class info is available for this.method() calls)
        for method in node.methods:
            self.analyze_method_declaration(method)

        # Restore current_class
        self.current_class = old_current_class

        # Debug: Print class info
        # print(f"DEBUG: Analyzing class '{node.name}' with {len(node.fields)} fields and {len(node.methods)} methods")

        self.symbol_table.exit_scope()

    def analyze_field_declaration(self, node: FieldDeclaration):
        # Fields are just stored in the class scope
        self.symbol_table.declare(node.name, "field", node.type, element_type=getattr(node, 'element_type', None))

        # Analyze initializer if present
        if node.initializer:
            init_type = self.analyze(node.initializer)
            # Check type compatibility
            if init_type != node.type and init_type != "any" and init_type != "null":
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



    def analyze_index_expression(self, node: IndexExpression):
        # Analyze the object being indexed
        obj_type = self.analyze(node.object)
        if not obj_type:
            return None

        # Analyze the index
        index_type = self.analyze(node.index)

        # Dict indexing - keys should be string (or any for dynamic keys)
        if obj_type == "dict" or (obj_type and obj_type.startswith("dict<")):
            # Dict keys can be strings or any type (for dynamic access)
            if index_type not in ["string", "any", "int"]:
                pos_info = self.get_position_info(node)
                self.add_error(f"Dict key must be a valid type, got {index_type}{pos_info}")
            # Return "any" for dict value access (could be refined with typed dicts)
            return "any"

        # Any type indexing - allow any index type
        if obj_type == "any":
            return "any"

        # Array/string indexing - must be int
        if obj_type == "string" or obj_type == "array":
            if index_type != "int" and index_type != "any":
                pos_info = self.get_position_info(node)
                self.add_error(f"Array/string index must be an integer{pos_info}")
                return None

        # Determine return type based on object type
        if obj_type == "string":
            return "string"  # Single character as string
        elif obj_type == "array":
            # Check if the array has a declared element type
            element_type = None
            if isinstance(node.object, Identifier):
                symbol = self.symbol_table.lookup(node.object.name)
                if symbol and symbol.get("element_type"):
                    return symbol["element_type"]
            elif isinstance(node.object, MemberExpression):
                # Handle this.tokens or similar member expressions
                field_name = None
                if isinstance(node.object.property, Identifier):
                    field_name = node.object.property.name
                elif isinstance(node.object.property, str):
                    field_name = node.object.property

                if field_name:
                    symbol = self.symbol_table.lookup(field_name)
                    if symbol and symbol.get("element_type"):
                        return symbol["element_type"]
            # Default to any for untyped arrays
            return "any"
        else:
            pos_info = self.get_position_info(node)
            self.add_error(f"Cannot index into type '{obj_type}'{pos_info}")
            return None

    def analyze_member_expression(self, node: MemberExpression):
        # Analyze the object
        object_type = self.analyze(node.object)
        
        # Handle .length property on strings and arrays - returns int
        if node.property == "length" and object_type in ["string", "array"]:
            return "int"
        
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
        
        # Handle obj.field for class instances (Identifier with known class type)
        if isinstance(node.object, Identifier):
            var_name = node.object.name
            # Look up the variable in the symbol table
            var_symbol = self.symbol_table.lookup(var_name)
            if var_symbol and var_symbol.get('symbol_type') == 'variable':
                var_decl_type = var_symbol.get('data_type')
                # Look up the class info
                class_info = self.classes.get(var_decl_type, {})
                fields = class_info.get('fields', [])
                for field in fields:
                    if field['name'] == node.property:
                        return field['type']
        
        # For module.function calls, return "any" since we don't track module function types
        if object_type == "module":
            return "any"

        # Handle field access on call results (e.g., this.current_token().value)
        # object_type is the return type of the call, look up the field in that class
        if isinstance(node.object, CallExpression):
            class_info = self.classes.get(object_type, {})
            fields = class_info.get('fields', [])
            for field in fields:
                if field['name'] == node.property:
                    return field['type']
            # Field not found, return any
            return "any"

        return object_type

    def analyze_variable_declaration(self, node: VariableDeclaration):
        # Check if the declared type is valid (built-in or user-defined class)
        type_symbol = self.symbol_table.lookup(node.type)
        if not type_symbol and node.type not in ["int", "float", "string", "bool", "array", "dict", "void", "any"]:
            pos_info = self.get_position_info(node)
            self.add_error(f"Unknown type '{node.type}'{pos_info}")

        # Validate element_type for typed arrays
        element_type = getattr(node, 'element_type', None)
        if element_type:
            elem_type_symbol = self.symbol_table.lookup(element_type)
            if not elem_type_symbol and element_type not in ["int", "float", "string", "bool"]:
                pos_info = self.get_position_info(node)
                self.add_error(f"Unknown element type '{element_type}'{pos_info}")

        # Validate key_type and value_type for typed dicts
        key_type = getattr(node, 'key_type', None)
        value_type = getattr(node, 'value_type', None)
        if key_type:
            key_type_symbol = self.symbol_table.lookup(key_type)
            if not key_type_symbol and key_type not in ["int", "float", "string", "bool"]:
                pos_info = self.get_position_info(node)
                self.add_error(f"Unknown dict key type '{key_type}'{pos_info}")
        if value_type:
            value_type_symbol = self.symbol_table.lookup(value_type)
            if not value_type_symbol and value_type not in ["int", "float", "string", "bool", "any"]:
                pos_info = self.get_position_info(node)
                self.add_error(f"Unknown dict value type '{value_type}'{pos_info}")

        if node.value:
            type_of_node = self.analyze(node.value)
            # "any" is a wildcard that matches any type (both directions)
            # Build expected type string for dict
            expected_type = node.type
            if node.type == "dict" and key_type and value_type:
                expected_type = f"dict<{key_type}, {value_type}>"
            elif node.type == "dict":
                # If variable is declared as dict without types, accept any dict type
                # Just set expected_type to type_of_node so comparison passes
                expected_type = type_of_node
            
            if type_of_node != expected_type and type_of_node != "any" and node.type != "any":
                # Allow assignment if both are user-defined classes or if value is a new expression of the right type
                # Also allow null to be assigned to any type (null is compatible with all pointer types)
                if not ((isinstance(node.value, NewExpression) and node.value.class_name == node.type) or
                        (isinstance(node.value, CallExpression) and type_of_node == expected_type)):
                    # null can be assigned to any type
                    if type_of_node != "null":
                        pos_info = self.get_position_info(node)
                        # print(f"DEBUG: Assignment type mismatch: {type_of_node} to {expected_type}")
                        # print(f"DEBUG: Assignment node: {node}")
                        self.add_error(f"Type mismatch{pos_info}. Cannot assign type {type_of_node} to {expected_type}")
        self.symbol_table.declare(node.name, "variable", node.type, element_type=element_type)
    def analyze_function_declaration(self, node: FunctionDeclaration):
        self.symbol_table.declare(node.name, "function", node.return_type, node.parameters)
        self.symbol_table.enter_scope()
        for param in node.parameters:
            self.symbol_table.declare(param.name, "parameter", param.param_type or "any")
        for statement in node.body.statements:
            self.analyze(statement)
        self.symbol_table.exit_scope()
    def analyze_external_declaration(self, node: ExternalDeclaration):
        self.symbol_table.declare(node.name, "function", node.return_type, node.parameters)
    def analyze_dynamic_function_declaration(self, node: DynamicFunctionDeclaration):
        # Dynamic function with statically typed parameters and dynamic return
        self.symbol_table.declare(node.name, "function", "any", node.parameters)
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
    def analyze_binary_expression(self, node: BinaryExpression):
        left_type = self.analyze(node.left)
        right_type = self.analyze(node.right)
        # print(f"DEBUG: Binary op {node.operator.value}: {left_type} + {right_type}")
        # print(f"DEBUG: Left node: {node.left}")
        # print(f"DEBUG: Right node: {node.right}")
        
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
    
    def analyze_in_expression(self, node: InExpression):
        """Analyze 'item in collection' expression - returns bool"""
        self.analyze(node.item)
        self.analyze(node.container)
        return "bool"
    
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
    
    def analyze_break_statement(self, node: BreakStatement):
        """Analyze break statement - just marks that we're in a loop"""
        # Track that this scope contains a break for validation
        if not hasattr(self, 'break_depth'):
            self.break_depth = 0
        self.break_depth += 1
    
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
    def analyze_libc_import(self, node: LibcImportStatement):
        """Register imported libc functions as builtins in the symbol table"""
        # Map libc types to semantic types
        type_map = {
            "int": "int",
            "ptr": "string",  # pointers are treated as strings
            "void": "void",
            "float": "float"
        }
        for func_name in node.symbols:
            if func_name in LIBC_FUNCTIONS:
                func_info = LIBC_FUNCTIONS[func_name]
                ret_type = type_map.get(func_info["ret"], "unknown")
                self.symbol_table.declare(func_name, "builtin", ret_type)
            else:
                pos_info = self.get_position_info(node)
                self.add_error(f"Unknown libc function '{func_name}'{pos_info}")
    def analyze_from_import(self, node: FromImportStatement):
        # Load the module and analyze it to find classes and functions
        module_path = node.module_path

        # Flatten module path to parts                                                                                                                                                          
        if isinstance(module_path, Identifier):                                                                                                                                                 
            parts = [module_path.name]                                                                                                                                                          
        elif isinstance(module_path, MemberExpression):                                                                                                                                         
            parts = []                                                                                                                                                                          
            current = module_path                                                                                                                                                                  
            while isinstance(current, MemberExpression):                                                                                                                                           
                parts.append(current.property)                                                                                                                                                     
                current = current.object                                                                                                                                                              
            parts.append(current.name)                                                                                                                                                             
            parts.reverse()                                                                                                                                                                     
        else:                                                                                                                                                                                   
            return    
                                                                                                                                                                                  
        rel_path = os.path.join(*parts) + ".mtc"     

        # print(f"module_name = {rel_path}")
        # Try to load the module
        try:
            # Check multiple paths for the module
            module_source = None
            module_file_path = None
            
            # 1. Check current directory
            local_path = rel_path
            if os.path.exists(local_path):
                with open(local_path, "r") as f:
                    module_source = f.read()
                module_file_path = local_path
            
            # 2. Check stdlib directory
            if module_source is None:
                module_file_path = os.path.join(os.path.dirname(__file__), rel_path)
                if os.path.exists(module_file_path):
                    with open(module_file_path, "r") as f:
                        module_source = f.read()
            
            if module_source is None:
                # print(f"DEBUG: Could not find module '{rel_path}'")
                return

            # Parse and analyze the module
            from tokenizer import Tokenizer
            from parser import Parser
            tokens = Tokenizer(module_source, module_file_path).tokenize()
            module_ast = Parser(tokens, module_file_path).parse_program()

            # Analyze the module to find classes
            module_analyzer = SemanticAnalyzer(module_file_path)
            module_analyzer.analyze(module_ast)
            
            # Update our class registry with the findings
            if hasattr(module_analyzer, 'classes'):
                if not hasattr(self, 'classes'):
                    self.classes = {}
                # Merge module classes into our registry
                self.classes.update(module_analyzer.classes)

            # Debug: Print all symbols found in module
            # print(f"DEBUG: Module symbol table contains:")
            for scope in module_analyzer.symbol_table.scopes:
                for name, info in scope.items():
                    pass
                    # print(f"  {name}: {info}")
            
            # Determine which symbols to import
            if node.is_wildcard:
                # Wildcard import: collect all classes and functions from the module
                symbols_to_import = []
                for scope in module_analyzer.symbol_table.scopes:
                    for name, info in scope.items():
                        if info.get("symbol_type") in ("class", "function"):
                            symbols_to_import.append((name, info))
            else:
                # Specific import: use the symbols list
                symbols_to_import = []
                for symbol in node.symbols:
                    class_symbol = module_analyzer.symbol_table.lookup(symbol)
                    if class_symbol:
                        symbols_to_import.append((symbol, class_symbol))
            
            # Register imported symbols (with conflict detection)
            for symbol_name, symbol_info in symbols_to_import:
                # Check for conflicts with existing symbols
                existing = self.symbol_table.lookup(symbol_name)
                if existing:
                    pos_info = self.get_position_info(node)
                    self.add_error(f"Import conflict: '{symbol_name}' is already defined{pos_info}")
                    continue
                
                if symbol_info["symbol_type"] == "class":
                    # Register the class in our symbol table
                    self.symbol_table.declare(symbol_name, "class", symbol_name)
                    # Also add to a separate class registry for codegen
                    if not hasattr(self, 'classes'):
                        self.classes = {}
                    # Copy class information from module
                    module_class_info = module_analyzer.classes.get(symbol_name, {})
                    self.classes[symbol_name] = module_class_info
                    # print(f"DEBUG: Registered class '{symbol_name}' with fields: {module_class_info.get('fields', [])}")
                else:
                    # It's a function - use its actual return type from the module analysis
                    self.symbol_table.declare(symbol_name, "function", symbol_info["data_type"], symbol_info.get("parameters"))
                    # print(f"DEBUG: Registered function '{symbol_name}' with type: {symbol_info['data_type']}")

        except Exception as e:
            # If we can't load the module, just declare as function for now
            # print(f"DEBUG: Exception loading module '{rel_path}': {e}")
            if node.is_wildcard:
                # For wildcard, we can't declare fallback symbols since we don't know what they are
                self.add_error(f"Could not load module '{rel_path}' for wildcard import")
            else:
                for symbol in node.symbols:
                    self.symbol_table.declare(symbol, "function", "any")

    def analyze_call_expression(self, node: CallExpression):
        func_name = None
        # Handle type conversion calls like string(x), int(x), float(x)
        if isinstance(node.callee, TypeLiteral):
            type_name = node.callee.name
            # Analyze the argument
            for arg in node.arguments:
                self.analyze(arg)
            # Return the target type
            return type_name
        if isinstance(node.callee, Identifier):
            func_name = node.callee.name
            # print(f"DEBUG: Analyzing call to {func_name}")
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
                # print(f"DEBUG: Builtin {func_name} returns {result}")
                return result
            else:
                # For user-defined functions, check parameters
                parameters = func_symbol.get("parameters") or []
                num_args = len(node.arguments)
                num_params = len(parameters)
                
                # Expand arguments with defaults if needed
                while len(node.arguments) < num_params and parameters[len(node.arguments)].default_value is not None:
                    node.arguments.append(parameters[len(node.arguments)].default_value)
                
                num_args = len(node.arguments)
                min_args = sum(1 for p in parameters if p.default_value is None)
                if num_args < min_args or num_args > num_params:
                    pos_info = self.get_position_info(node)
                    self.add_error(f"Function '{func_name}' expects {min_args} to {num_params} arguments, got {num_args}{pos_info}")
                
                # Analyze provided arguments
                for i, arg in enumerate(node.arguments):
                    if i < num_params:
                        expected_type = parameters[i].param_type
                        if expected_type:
                            arg_type = self.analyze(arg)
                            if arg_type != expected_type and arg_type != "any" and expected_type != "any" and arg_type != "null":
                                pos_info = self.get_position_info(arg)
                                self.add_error(f"Argument {i+1} of '{func_name}' expects {expected_type}, got {arg_type}{pos_info}")
                
                result = func_symbol.get("data_type", "unknown")
                # print(f"DEBUG: User function {func_name} returns {result}")
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

            # Handle string literal method calls: "text".length()
            if isinstance(node.callee.object, StringLiteral):
                if method_name == "length":
                    return "int"

            # Handle method calls on 'this': this.method()
            if isinstance(node.callee.object, ThisExpression):
                if hasattr(self, 'current_class') and self.current_class:
                    class_info = self.classes.get(self.current_class, {})
                    methods = class_info.get("methods", {})
                    if method_name in methods:
                        method_info = methods[method_name]
                        parameters = method_info["params"]

                        # Expand arguments with defaults if needed
                        while len(node.arguments) < len(parameters) and parameters[len(node.arguments)].default_value is not None:
                            node.arguments.append(parameters[len(node.arguments)].default_value)

                        num_args = len(node.arguments)
                        num_params = len(parameters)
                        min_args = sum(1 for p in parameters if p.default_value is None)
                        if num_args < min_args or num_args > num_params:
                            pos_info = self.get_position_info(node)
                            self.add_error(f"Method '{method_name}' expects {min_args} to {num_params} arguments, got {num_args}{pos_info}")

                        # Analyze provided arguments
                        for i, arg in enumerate(node.arguments):
                            if i < num_params:
                                expected_type = parameters[i].param_type
                                if expected_type:
                                    arg_type = self.analyze(arg)
                                    if arg_type != expected_type and arg_type != "any" and expected_type != "any" and arg_type != "null":
                                        pos_info = self.get_position_info(arg)
                                        self.add_error(f"Argument {i+1} of method '{method_name}' expects {expected_type}, got {arg_type}{pos_info}")

                        # Return "any" for dynamic functions (return_type is None)
                        return method_info["return_type"] or "any"

            # Handle method calls on variables/parameters: str.length(), arr.length()
            if isinstance(node.callee.object, Identifier):
                obj_name = node.callee.object.name
                obj_symbol = self.symbol_table.lookup(obj_name)
                if obj_symbol and obj_symbol["symbol_type"] in ("variable", "parameter"):
                    obj_type = obj_symbol["data_type"]

                    # String methods (built into the compiler)
                    if obj_type == "string":
                        if method_name == "length":
                            return "int"

                    # Array methods (built into the compiler)
                    if obj_type == "array":
                        if method_name == "length":
                            return "int"
                        elif method_name == "append":
                            return "void"
                        elif method_name == "pop":
                            return "any"

                    # Dict methods
                    if obj_type == "dict" or (obj_type and obj_type.startswith("dict<")):
                        if method_name == "keys":
                            return "array"
                        elif method_name == "values":
                            return "array"

                    # Look up method return type from class definition
                    if obj_type in self.classes:
                        class_info = self.classes[obj_type]
                        methods = class_info.get("methods", {})
                        if method_name in methods:
                            method_info = methods[method_name]
                            parameters = method_info["params"]

                            # Expand arguments with defaults if needed
                            while len(node.arguments) < len(parameters) and parameters[len(node.arguments)].default_value is not None:
                                node.arguments.append(parameters[len(node.arguments)].default_value)

                            num_args = len(node.arguments)
                            num_params = len(parameters)
                            min_args = sum(1 for p in parameters if p.default_value is None)
                            if num_args < min_args or num_args > num_params:
                                pos_info = self.get_position_info(node)
                                self.add_error(f"Method '{method_name}' expects {min_args} to {num_params} arguments, got {num_args}{pos_info}")

                            # Analyze provided arguments
                            for i, arg in enumerate(node.arguments):
                                if i < num_params:
                                    expected_type = parameters[i].param_type
                                    if expected_type:
                                        arg_type = self.analyze(arg)
                                        if arg_type != expected_type and arg_type != "any" and expected_type != "any" and arg_type != "null":
                                            pos_info = self.get_position_info(arg)
                                            self.add_error(f"Argument {i+1} of method '{method_name}' expects {expected_type}, got {arg_type}{pos_info}")

                            # Return "any" for dynamic functions (return_type is None)
                            return method_info["return_type"] or "any"

            # Handle method calls on MemberExpressions: this.scopes.length(), obj.field.method()
            if isinstance(node.callee.object, MemberExpression):
                # Analyze the object to get its type
                obj_type = self.analyze(node.callee.object)

                # Built-in methods for arrays and strings
                if obj_type == "array":
                    if method_name == "length":
                        return "int"
                    elif method_name == "append":
                        return "void"
                    elif method_name == "pop":
                        return "any"
                elif obj_type == "string":
                    if method_name == "length":
                        return "int"
                elif obj_type == "dict" or (obj_type and obj_type.startswith("dict<")):
                    if method_name == "keys":
                        return "array"
                    elif method_name == "values":
                        return "array"

                # Look up method return type from class definition
                if obj_type in self.classes:
                    class_info = self.classes[obj_type]
                    methods = class_info.get("methods", {})
                    if method_name in methods:
                        method_info = methods[method_name]
                        return method_info["return_type"] or "any"

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
    def analyze_dict_literal(self, node):
        """Analyze a dict literal and return its type"""
        # Build the type string
        key_type = node.key_type if node.key_type else "any"
        value_type = node.value_type if node.value_type else "any"
        
        # If no explicit types and we have elements, infer from first element
        if not node.key_type and len(node.keys) > 0:
            key_type = self.analyze(node.keys[0])
        if not node.value_type and len(node.values) > 0:
            value_type = self.analyze(node.values[0])
        
        # Validate all key/value pairs
        if len(node.keys) > 0:
            first_key_type = self.analyze(node.keys[0])
            first_value_type = self.analyze(node.values[0])
            
            for i, key in enumerate(node.keys):
                key_type_result = self.analyze(key)
                value_type_result = self.analyze(node.values[i])
                
                # Check key type matches (if explicit)
                if node.key_type and key_type_result != node.key_type:
                    self.add_error(f"Dict key type mismatch. Expected {node.key_type}, got {key_type_result}")
                
                # Check value type matches (if explicit)
                if node.value_type and value_type_result != node.value_type:
                    self.add_error(f"Dict value type mismatch. Expected {node.value_type}, got {value_type_result}")
        
        # Return the dict type
        if key_type == "any" and value_type == "any":
            return "dict"
        return f"dict<{key_type}, {value_type}>"
    def analyze_bool_literal(self, node):
        return "bool"

    def analyze_null_literal(self, node):
        return "null"

    def analyze_hasattr_expression(self, node):
        """Analyze hasattr(obj, 'attr') - compile-time check if class has attribute"""
        obj_type = self.analyze(node.obj)
        attr_name = node.attr_name

        # Check if the object is a class instance
        if obj_type in self.classes:
            class_info = self.classes[obj_type]
            # Check fields (list of dicts with 'name' key)
            field_names = [f['name'] for f in class_info['fields']]
            # Check methods (dict with method names as keys)
            method_names = list(class_info['methods'].keys())
            has_attr = attr_name in field_names or attr_name in method_names
            # Store the result for codegen (compile-time evaluation)
            node.compile_time_result = has_attr
        else:
            # For non-class types, check if it's a primitive type attribute
            # Primitives don't have attributes, so always false
            node.compile_time_result = False

        return "bool"

    def analyze_while_statement(self, node):
        """Analyze a while statement, including its condition and body"""
        # Analyze the condition
        self.analyze(node.condition)
        # Analyze the body
        if node.then_body:
            self.analyze(node.then_body)

    def analyze_for_in_statement(self, node):
        """Analyze a for-in statement"""
        # Analyze the iterable
        self.analyze(node.iterable)
        # Enter a new scope for the loop variable
        self.symbol_table.enter_scope()
        # Declare the loop variable
        self.symbol_table.declare(node.variable, "variable", "any")
        # Analyze the body
        if node.body:
            self.analyze(node.body)
        self.symbol_table.exit_scope()

    def analyze_set_statement(self, node):
        """Analyze a set statement (assignment)"""
        # Analyze the target (could be an identifier or member expression)
        target_type = self.analyze(node.target)
        # Analyze the value
        value_type = self.analyze(node.value)
        
        # Type checking for assignment
        if target_type and value_type:
            if target_type != value_type and value_type != "any" and target_type != "any":
                # null can be assigned to any type
                if value_type != "null":
                    pos_info = self.get_position_info(node)
                    # print(f"DEBUG: SetStatement type mismatch: {value_type} to {target_type}")
                    # print(f"DEBUG: SetStatement node: {node}")
                    self.add_error(f"Type mismatch{pos_info}. Cannot assign type {value_type} to {target_type}")

    def analyze_return_statement(self, node):
        """Analyze a return statement"""
        if node.value:
            return self.analyze(node.value)
        return "void"

    def analyze_block(self, node):
        """Analyze a block of statements"""
        for statement in node.statements:
            self.analyze(statement)
    
    def analyze_try_statement(self, node):
        """Analyze a try/catch statement"""
        # print(f"DEBUG: Analyzing TryStatement")
        
        # Analyze try block
        self.symbol_table.enter_scope()
        self.analyze(node.try_block)
        self.symbol_table.exit_scope()
        
        # Analyze catch blocks
        catch_types_seen = []
        for catch_block in node.catch_blocks:
            self.analyze_catch_block(catch_block, catch_types_seen)
        
        return None
    
    def analyze_catch_block(self, node, catch_types_seen=None):
        """Analyze a catch block"""
        if catch_types_seen is None:
            catch_types_seen = []
        
        # Check exception type if specified
        if node.exception_type:
            # Verify exception type exists (Exception or subclass)
            exc_class = self.symbol_table.lookup(node.exception_type)
            if not exc_class or exc_class.get("symbol_type") != "class":
                pos_info = self.get_position_info(node)
                self.add_error(f"Unknown exception type '{node.exception_type}'{pos_info}")
                return None
            
            # Check for duplicate catch types
            if node.exception_type in catch_types_seen:
                pos_info = self.get_position_info(node)
                self.add_error(f"Duplicate catch block for exception type '{node.exception_type}'{pos_info}")
            catch_types_seen.append(node.exception_type)
        
        # Enter scope for catch block body
        self.symbol_table.enter_scope()
        
        # Declare exception binding if present
        if node.identifier:
            exc_type = node.exception_type if node.exception_type else "Exception"
            self.symbol_table.declare(node.identifier, "variable", exc_type)
        
        # Analyze catch body
        self.analyze(node.body)
        
        self.symbol_table.exit_scope()
        
        return None
    
    def analyze_throw_statement(self, node):
        """Analyze a throw statement"""
        # print(f"DEBUG: Analyzing ThrowStatement")

        # If no expression, it's a standalone throw - just exits with generic error
        if node.expression is None:
            return None

        # Analyze the expression being thrown
        expr_type = self.analyze(node.expression)

        # Verify it's an Exception type
        if expr_type is None or expr_type == "unknown":
            pos_info = self.get_position_info(node)
            self.add_error(f"Cannot determine type of expression being thrown{pos_info}")
            return None

        # Check if it's an Exception or subclass
        if expr_type != "Exception":
            # Check if it's a subclass of Exception using self.classes
            exc_class_info = self.classes.get(expr_type)
            if exc_class_info:
                # Check if it extends Exception (directly or indirectly)
                parent = exc_class_info.get("parent_class")
                extends_exception = False
                while parent:
                    if parent == "Exception":
                        extends_exception = True
                        break
                    parent_info = self.classes.get(parent)
                    if parent_info:
                        parent = parent_info.get("parent_class")
                    else:
                        break
                if not extends_exception:
                    pos_info = self.get_position_info(node)
                    self.add_error(f"Cannot throw type '{expr_type}' - must extend Exception{pos_info}")
            else:
                pos_info = self.get_position_info(node)
                self.add_error(f"Cannot throw type '{expr_type}' - must be an Exception{pos_info}")

        return None