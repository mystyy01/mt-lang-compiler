class NumberLiteral:
    def __init__(self, number, line=None, column=None):
        self.value = number
        self.line = line
        self.column = column
    def __repr__(self):
        return f"NumberLiteral({self.value})"
class StringLiteral:
    def __init__(self, string: str, line=None, column=None):
        self.value = string
        self.line = line
        self.column = column
    def __repr__(self):
        return f"StringLiteral({repr(self.value)})"
class Identifier:
    def __init__(self, name, line=None, column=None):
        self.name = name
        self.line = line
        self.column = column
    def __repr__(self):
        return f"Identifier({self.name})"
class BinaryExpression:
    def __init__(self, left, operator, right):
        self.left = left
        self.operator = operator
        self.right = right
    def __repr__(self):
        return f"BinaryExpression({self.left}, {self.operator.value}, {self.right})"
class ArrayLiteral:
    def __init__(self, elements: list):
        self.elements = elements
    def __repr__(self):
        return f"ArrayLiteral({self.elements})"
class VariableDeclaration:
    def __init__(self, var_type, name, value = None, line=None, column=None):
        self.type = var_type
        self.name = name
        self.value = value
        self.line = line
        self.column = column
    def __repr__(self):
        return f"VariableDeclaration({self.type}, {self.name}, {self.value})"
class SetStatement:
    def __init__(self, name, value, line=None, column=None):
        self.name = name
        self.value = value
        self.line = line
        self.column = column
    def __repr__(self):
        return f"SetStatement({self.name}, {self.value})"
class ReturnStatement:
    def __init__(self, value = None):
        self.value = value
    def __repr__(self):
        return f"ReturnStatement({self.value})"
class Block:
    def __init__(self, statements):
        self.statements = statements
    def __repr__(self):
        return f"Block({self.statements})"
class ExpressionStatement:
    def __init__(self, expression):
        self.expression = expression
    def __repr__(self):
        return f"ExpressionStatement({self.expression})"
class CallExpression:
    def __init__(self, callee, arguments: list):
        self.callee = callee
        self.arguments = arguments
    def __repr__(self):
        return f"CallExpression({self.callee}, {self.arguments})"
class MemberExpression:
    def __init__(self, object, property):
        self.object = object
        self.property = property
    def __repr__(self):
        return f"MemberExpression({self.object}, {self.property})"
class TypeofExpression:
    def __init__(self, argument):
        self.argument = argument
    def __repr__(self):
        return f"TypeofExpression({self.argument})"
class IfStatement:
    def __init__(self, condition, then_body: Block, else_body: Block = None):
        self.condition = condition
        self.then_body = then_body
        self.else_body = else_body
    def __repr__(self):
        return f"IfStatement({self.condition}, {self.then_body}, {self.else_body})"
class WhileStatement:
    def __init__(self, condition, then_body: Block):
        self.condition = condition
        self.then_body = then_body
    def __repr__(self):
        return f"WhileStatement({self.condition}, {self.then_body})"
class ForInStatement:
    def __init__(self, variable, iterable, body: Block):
        self.variable = variable
        self.iterable = iterable
        self.body = body
    def __repr__(self):
        return f"ForInStatement({self.variable}, {self.iterable}, {self.body})"
class FunctionDeclaration:
    def __init__(self, return_type, name, parameters: list, body: Block):
        self.return_type = return_type
        self.name = name
        self.parameters = parameters
        self.body = body
    def __repr__(self):
        return f"FunctionDeclaration({self.return_type}, {self.name}, {self.parameters}, {self.body})"
class FromImportStatement:
    def __init__(self, module_path, symbols):
        self.module_path = module_path
        self.symbols = symbols  # List of symbol names
    def __repr__(self):
        return f"FromImportStatement({self.module_path}, {self.symbols})"
class SimpleImportStatement:
    def __init__(self, module_name, alias = None):
        self.module_name = module_name
        self.alias = alias
    def __repr__(self):
        return f"SimpleImportStatement({self.module_name}, {self.alias})"
class Program:
    def __init__(self, statements: list):
        self.statements = statements
    def __repr__(self):
        return f"Program({self.statements})"
class TypeLiteral:
    def __init__(self, name):
        self.name = name
    def __repr__(self):
        return f"TypeLiteral({self.name})"
class BoolLiteral:
    def __init__(self, value: bool):
        self.value = value
    def __repr__(self):
        return f"BoolLiteral({self.value})"
