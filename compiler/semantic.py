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
        self.errors = []
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
    def analyze_program(self, node: Program):
        for statement in node.statements:
            self.analyze(statement)
    def analyze_variable_declaration(self, node: VariableDeclaration):
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
            self.errors.append(f"Undeclared variable: {node.name}")
    def analyze_expression_statement(self, node: ExpressionStatement):
        self.analyze(node.expression)
    def analyze_set_statement(self, node: SetStatement):
        if not self.symbol_table.lookup(node.name):
            self.errors.append(f"Undeclared variable: {node.name}")
            return None
        self.analyze(node.value)
    def analyze_binary_expression(self, node: BinaryExpression):
        self.analyze(node.left)
        self.analyze(node.right)
    def analyze_call_expression(self, node: CallExpression):
        self.analyze(node.callee)
        for arg in node.arguments:
            self.analyze(arg)
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
        self.analyze(node.object)
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
        if node.alias:
            self.symbol_table.declare(node.alias, node.symbol, "any")
        else:
            self.symbol_table.declare(node.symbol, node.symbol, "any")