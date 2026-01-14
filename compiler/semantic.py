from ast_nodes import *

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
    def __init__(self):
        self.symbol_table = SymbolTable()
        self.symbol_table.declare("print", "builtin", "function")
        # Declare built-in array methods
        self.symbol_table.declare("length", "builtin", "method")
        self.symbol_table.declare("append", "builtin", "method")
        self.errors = []
    
    def get_position_info(self, node):
        """Get formatted position information for error messages"""
        if hasattr(node, 'line') and node.line is not None:
            pos_info = f" at line {node.line}"
            if hasattr(node, 'column') and node.column is not None:
                pos_info += f", column {node.column}"
            return pos_info
        return ""
    def analyze(self, node):
        if isinstance(node, Program):
            return self.analyze_program(node)
        if isinstance(node, VariableDeclaration):
            return self.analyze_variable_declaration(node)
        if isinstance(node, FunctionDeclaration):
            return self.analyze_function_declaration(node)
        if isinstance(node, Identifier):
            return self.analyze_identifier(node)
        if isinstance(node, ExpressionStatement):
            return self.analyze_expression_statement(node)
        if isinstance(node, SetStatement):
            return self.analyze_set_statement(node)
        if isinstance(node, BinaryExpression):
            return self.analyze_binary_expression(node)
        if isinstance(node, CallExpression):
            return self.analyze_call_expression(node)
        if isinstance(node, IfStatement):
            return self.analyze_if_statement(node)
        if isinstance(node, MemberExpression):
            return self.analyze_member_expression(node)
        if isinstance(node, ForInStatement):
            return self.analyze_for_statement(node)
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
        # cant implement bools yet since they arent an AST class yet (adding soon)
    def analyze_program(self, node: Program):
        for statement in node.statements:
            self.analyze(statement)
    def analyze_variable_declaration(self, node: VariableDeclaration):
        if node.value:
            type_of_node = self.analyze(node.value)
            # "any" is a wildcard that matches any type
            if type_of_node != node.type and type_of_node != "any":
                pos_info = self.get_position_info(node)
                self.errors.append(f"Type mismatch at {pos_info}. Cannot assign type {type_of_node} to {node.type}")
        self.symbol_table.declare(node.name, "variable", node.type)
    def analyze_function_declaration(self, node: FunctionDeclaration):
        self.symbol_table.declare(node.name, "function", node.return_type)
        self.symbol_table.enter_scope()
        for param in node.parameters:
            self.symbol_table.declare(param.name, "parameter", "any")
        for statement in node.body.statements:
            self.analyze(statement)
        self.symbol_table.exit_scope()
    def analyze_identifier(self, node: Identifier):
        res = self.symbol_table.lookup(node.name) # check if its declared or not
        if not res:
            pos_info = self.get_position_info(node)
            self.errors.append(f"Undeclared variable '{node.name}'{pos_info}")
            return
        return res["data_type"]
    def analyze_expression_statement(self, node: ExpressionStatement):
        self.analyze(node.expression)
    def analyze_set_statement(self, node: SetStatement):
        if not self.symbol_table.lookup(node.name):
            pos_info = self.get_position_info(node)
            self.errors.append(f"Undeclared variable '{node.name}' in assignment{pos_info}")
            return None
        original_type = self.symbol_table.lookup(node.name)["data_type"]
        type_assigned = self.analyze(node.value)
        if original_type != type_assigned:
                self.errors.append(f"Type mismatch. Cannot assign type {type_assigned} to {original_type}")
                return None
        self.analyze(node.value)
    def analyze_binary_expression(self, node: BinaryExpression):
        left_type = self.analyze(node.left)
        right_type = self.analyze(node.right)
        
        # Comparison operators always return bool
        if node.operator.value in ["==", "!=", ">", "<", ">=", "<="]:
            return "bool"
        
        # Logical operators always return bool
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
    def analyze_call_expression(self, node: CallExpression):
        callee_type = self.analyze(node.callee)
        for arg in node.arguments:
            self.analyze(arg)
        # Return the function's return type, or "any" if unknown
        return callee_type if callee_type else "any"
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
    def analyze_member_expression(self, node: MemberExpression):
        obj_type = self.analyze(node.object)
        # For module.function calls, return "any" since we don't track module function types
        if obj_type == "module":
            return "any"
        return obj_type
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
        # For "from math use add, square" style - declare each function for direct access
        for symbol in node.symbols:
            self.symbol_table.declare(symbol, "function", "any")
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
                self.errors.append(f"Array elements must all be the same type. Expected {first_type}, got {elem_type}")

        return "array"
    def analyze_bool_literal(self, node):
        return "bool"