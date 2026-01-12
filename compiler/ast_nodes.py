class NumberLiteral:
    def __init__(self, number):
        self.value = number
class StringLiteral:
    def __init__(self, string: str):
        self.value = string
class Identifier:
    def __init__(self, name):
        self.name = name
class BinaryExpression:
    def __init__(self, left, operator, right):
        self.left = left
        self.operator = operator
        self.right = right
class ArrayLiteral:
    def __init__(self, elements: list):
        self.elements = elements
class VariableDeclaration:
    def __init__(self, var_type, name, value = None):
        self.type = var_type
        self.name = name
        self.value = value 
class SetStatement:
    def __init__(self, name, value):
        self.name = name
        self.value = value
class ReturnStatement:
    def __init__(self, value = None):
        self.value = value
class Block:
    def __init__(self, statements):
        self.statements = statements
class ExpressionStatement:
    def __init__(self, expression):
        self.expression = expression
class CallExpression:
    def __init__(self, callee, arguments: list):
        self.callee = callee
        self.arguments = arguments
class MemberExpression:
    def __init__(self, object, property):
        self.object = object
        self.property = property
class TypeofExpression:
    def __init__(self, argument):
        self.argument = argument
class IfStatement:
    def __init__(self, condition, then_body: list, else_body = None):
        self.condition = condition
        self.then_body = then_body
        self.else_body = else_body
class ForInStatement:
    def __init__(self, variable, iterable, body: list):
        self.variable = variable
        self.iterable = iterable
        self.body = body
class FunctionDeclaration:
    def __init__(self, return_type, name, parameters: list, body: list):
        self.return_type = return_type
        self.name = name
        self.parameters = parameters
        self.body = body
class ImportStatement:
    def __init__(self, module_path, symbol, alias = None):
        self.module_path = module_path
        self.symbol = symbol
        self.alias = alias
class Program:
    def __init__(self, statements: list):
        self.statements = statements