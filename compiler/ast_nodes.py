class NumberLiteral:
    def __init__(self, number, line=None, column=None):
        self.value = number
        self.line = line
        self.column = column
    def __repr__(self):
        return f"NumberLiteral({self.value})"
class FloatLiteral:
    def __init__(self, value: float, line=None, column=None):
        self.value = value
        self.line = line
        self.column = column
    def __repr__(self):
        return f"FloatLiteral({self.value})"
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
class InExpression:
    def __init__(self, item, container, line=None, column=None):
        self.item = item
        self.container = container
        self.line = line
        self.column = column
    def __repr__(self):
        return f"InExpression({self.item} in {self.container})"
class ArrayLiteral:
    def __init__(self, elements: list):
        self.elements = elements
    def __repr__(self):
        return f"ArrayLiteral({self.elements})"

class DictType:
    def __init__(self, key_type="any", value_type="any"):
        self.key_type = key_type
        self.value_type = value_type
    def __repr__(self):
        if self.key_type == "any" and self.value_type == "any":
            return "dict"
        return f"dict<{self.key_type}, {self.value_type}>"

class DictLiteral:
    def __init__(self, keys: list, values: list, key_type=None, value_type=None):
        self.keys = keys  # list of key expressions
        self.values = values  # list of value expressions
        self.key_type = key_type  # explicit key type if specified
        self.value_type = value_type  # explicit value type if specified
    def __repr__(self):
        pairs = []
        for k, v in zip(self.keys, self.values):
            pairs.append(f"{k}: {v}")
        type_str = ""
        if self.key_type is not None and self.value_type is not None:
            type_str = f"dict<{self.key_type}, {self.value_type}>"
        return f"DictLiteral({type_str}, {{{', '.join(pairs)}}})"

class VariableDeclaration:
    def __init__(self, var_type, name, value = None, element_type=None, key_type=None, value_type=None, line=None, column=None):
        self.type = var_type
        self.name = name
        self.value = value
        self.element_type = element_type  # For typed arrays: array<int>, array<Token>, etc.
        self.key_type = key_type  # For typed dicts: dict<KeyType, ValueType>
        self.value_type = value_type  # For typed dicts: dict<KeyType, ValueType>
        self.line = line
        self.column = column
    def __repr__(self):
        elem_str = f"<{self.element_type}>" if self.element_type else ""
        if self.type == "dict" and self.key_type and self.value_type:
            return f"VariableDeclaration(dict<{self.key_type}, {self.value_type}>, {self.name}, {self.value})"
        return f"VariableDeclaration({self.type}{elem_str}, {self.name}, {self.value})"
class SetStatement:
    def __init__(self, target, value, line=None, column=None):
        self.target = target
        self.value = value
        self.line = line
        self.column = column
    def __repr__(self):
        return f"SetStatement({self.target}, {self.value})"
class BreakStatement:
    def __init__(self, line=None, column=None):
        self.line = line
        self.column = column
    def __repr__(self):
        return f"BreakStatement()"
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
    def __init__(self, callee, arguments: list, line=None, column=None):
        self.callee = callee
        self.arguments = arguments
        self.line = line
        self.column = column
    def __repr__(self):
        return f"CallExpression({self.callee}, {self.arguments})"
class MemberExpression:
    def __init__(self, object, property):
        self.object = object
        self.property = property
    def __repr__(self):
        return f"MemberExpression({self.object}, {self.property})"
class IndexExpression:
    def __init__(self, object, index):
        self.object = object
        self.index = index
    def __repr__(self):
        return f"IndexExpression({self.object}, {self.index})"
class TypeofExpression:
    def __init__(self, argument):
        self.argument = argument
    def __repr__(self):
        return f"TypeofExpression({self.argument})"
class HasattrExpression:
    def __init__(self, obj, attr_name):
        self.obj = obj
        self.attr_name = attr_name  # string literal
    def __repr__(self):
        return f"HasattrExpression({self.obj}, {self.attr_name})"
class ClassofExpression:
    def __init__(self, argument):
        self.argument = argument
    def __repr__(self):
        return f"ClassofExpression({self.argument})"
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
class ExternalDeclaration:
    def __init__(self, return_type, name, parameters: list):
        self.return_type = return_type
        self.name = name
        self.parameters = parameters
    def __repr__(self):
        return f"ExternalDeclaration({self.return_type}, {self.name}, {self.parameters})"
class DynamicFunctionDeclaration:
    def __init__(self, name, parameters: list, body: Block):
        self.name = name
        self.parameters = parameters
        self.body = body
        # No return_type - will be inferred
    def __repr__(self):
        return f"DynamicFunctionDeclaration({self.name}, {self.parameters}, {self.body})"
class FromImportStatement:
    def __init__(self, module_path, symbols, is_wildcard=False):
        self.module_path = module_path
        self.symbols = symbols  # List of symbol names, or None for wildcard
        self.is_wildcard = is_wildcard
    def __repr__(self):
        if self.is_wildcard:
            return f"FromImportStatement({self.module_path}, *)"
        return f"FromImportStatement({self.module_path}, {self.symbols})"
class SimpleImportStatement:
    def __init__(self, module_name, alias = None):
        self.module_name = module_name
        self.alias = alias
    def __repr__(self):
        return f"SimpleImportStatement({self.module_name}, {self.alias})"
class LibcImportStatement:
    def __init__(self, symbols):
        self.symbols = symbols  # List of libc function names to import
    def __repr__(self):
        return f"LibcImportStatement({self.symbols})"
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
class Parameter:
    def __init__(self, name, param_type=None, default_value=None, element_type=None):
        self.name = name
        self.param_type = param_type  # Type annotation like "array", "int", etc.
        self.default_value = default_value
        self.element_type = element_type  # For typed arrays: array<int>, array<Token>, etc.
    def __repr__(self):
        elem_str = f"<{self.element_type}>" if self.element_type else ""
        type_info = f": {self.param_type}{elem_str}" if self.param_type else ""
        default_info = f" = {self.default_value}" if self.default_value else ""
        return f"Parameter({self.name}{type_info}{default_info})"
class BoolLiteral:
    def __init__(self, value: bool):
        self.value = value
    def __repr__(self):
        return f"BoolLiteral({self.value})"

class NullLiteral:
    def __init__(self):
        pass
    def __repr__(self):
        return "NullLiteral()"

class ClassDeclaration:
    def __init__(self, name, fields=None, methods=None, inherits_from=None, line=None, column=None):
        self.name = name
        self.fields = fields or []
        self.methods = methods or []
        self.inherits_from = inherits_from
        # Separate constructor args from regular fields
        self.constructor_args = [f for f in self.fields if f.is_constructor_arg]
        self.regular_fields = [f for f in self.fields if not f.is_constructor_arg]
        self.line = line
        self.column = column
    def __repr__(self):
        inherits_str = f" inherits {self.inherits_from}" if self.inherits_from else ""
        return f"ClassDeclaration({self.name}{inherits_str}, {len(self.constructor_args)} args, {len(self.regular_fields)} fields, {len(self.methods)} methods)"

class FieldDeclaration:
    def __init__(self, name, field_type, initializer=None, is_constructor_arg=False, element_type=None, line=None, column=None):
        self.name = name
        self.type = field_type
        self.initializer = initializer
        self.is_constructor_arg = is_constructor_arg
        self.element_type = element_type  # For typed arrays: array<int>, array<Token>, etc.
        self.line = line
        self.column = column
    def __repr__(self):
        arg_str = " arg" if self.is_constructor_arg else ""
        elem_str = f"<{self.element_type}>" if self.element_type else ""
        init_str = f" = {self.initializer}" if self.initializer else ""
        return f"FieldDeclaration({self.name}: {self.type}{elem_str}{arg_str}{init_str})"

class MethodDeclaration:
    def __init__(self, name, params=None, return_type=None, body=None, is_virtual=False, is_static=False, line=None, column=None):
        self.name = name
        self.params = params or []
        self.return_type = return_type
        self.body = body
        self.is_virtual = is_virtual
        self.is_static = is_static
        self.line = line
        self.column = column
    def __repr__(self):
        virtual = "virtual " if self.is_virtual else ""
        static = "static " if self.is_static else ""
        return f"MethodDeclaration({virtual}{static}{self.name}{': ' + self.return_type if self.return_type else ''})"

class NewExpression:
    def __init__(self, class_name, arguments=None, line=None, column=None):
        self.class_name = class_name
        self.arguments = arguments or []
        self.line = line
        self.column = column
    def __repr__(self):
        return f"NewExpression({self.class_name}, args={self.arguments})"

class ThisExpression:
    def __init__(self, line=None, column=None):
        self.line = line
        self.column = column
    def __repr__(self):
        return f"ThisExpression()"

class MethodCallExpression:
    def __init__(self, object_expr, method_name, arguments=None, line=None, column=None):
        self.object_expr = object_expr
        self.method_name = method_name
        self.arguments = arguments or []
        self.line = line
        self.column = column
    def __repr__(self):
        return f"MethodCallExpression({self.object_expr}.{self.method_name}({self.arguments}))"

class FieldAccessExpression:
    def __init__(self, object_expr, field_name, line=None, column=None):
        self.object_expr = object_expr
        self.field_name = field_name
        self.line = line
        self.column = column
    def __repr__(self):
        return f"FieldAccessExpression({self.object_expr}.{self.field_name})"

class TryStatement:
    def __init__(self, try_block, catch_blocks, line=None, column=None):
        self.try_block = try_block
        self.catch_blocks = catch_blocks
        self.line = line
        self.column = column
    def __repr__(self):
        return f"TryStatement(try={self.try_block}, catches={self.catch_blocks})"

class CatchBlock:
    def __init__(self, exception_type=None, identifier=None, body=None, line=None, column=None):
        self.exception_type = exception_type
        self.identifier = identifier
        self.body = body
        self.line = line
        self.column = column
    def __repr__(self):
        type_str = self.exception_type if self.exception_type else "*"
        return f"CatchBlock({type_str} {self.identifier})"

class ThrowStatement:
    def __init__(self, expression, line=None, column=None):
        self.expression = expression
        self.line = line
        self.column = column
    def __repr__(self):
        return f"ThrowStatement({self.expression})"
