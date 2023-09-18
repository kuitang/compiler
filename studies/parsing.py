"""
E  -> A
A  -> A + M
   |  A - M
M  -> P
   |  M * P
   |  M / P
P  -> 0-9
   |  ( E )

right recursion:
A  -> M A'
A' -> + M A'
   |  - M A'
   |  e
M  -> P M'
M' -> * P M'
   |  / P M'
"""
import itertools
import pprint

class Program:
    def __init__(self, s):
        self.buf = s
        self.pos = 0

    def peek(self):
        if self.pos == len(self.buf):
            return 0  # EOF
        return self.buf[self.pos]
        
    def consume(self):
        self.pos += 1

    def eof(self):
        return self.pos == len(self.buf)


class SSAVisitor:
    def __init__(self):
        self.insts = []

    def _get_temp(self):
        num = len(self.insts) + 1
        return f't{num}'

    def _emit(self, inst):
        t = self._get_temp()
        self.insts.append(f'{t} = {inst}')
        return t

    def visit_primary_literal(self, num):
        return self._emit(f'load immediate ${num}')

    def visit_add2(self, op, left, right):
        if op == '+':
            inst = 'add'
        elif op == '-':
            inst = 'sub'
        else:
            raise Exception('Unreachable!')
        return self._emit(f'{inst} {left}, {right}')

    def visit_mul2(self, op, left, right):
        if op == '*':
            inst = 'mul'
        elif op == '/':
            inst = 'div'
        else:
            raise Excepton('Unreachable!')
        return self._emit(f'{inst} {left}, {right}')

    def write(self):
        return '\n'.join(self.insts)


class ASTVisitor:
    def visit_primary_literal(self, num):
        return {'_type': 'immediate', 'value': num}

    def visit_add2(self, op, left, right):
        return {'_type': 'additive_expression', 'op': op, 'left': left, 'right': right}
     
    def visit_mul2(self, op, left, right):
        return {'_type': 'multiplicative_expression', 'op': op, 'left': left, 'right': right}

    def write(self):
        return ''

    def __repr__(self):
        return self.__class__.__name__

class StackMachineVisitor:
    def __init__(self):
        self.insts = []

    def visit_add2(self, op, _, __):
        if op == '+':
            self.insts.append('add')
        elif op == '-':
            self.insts.append('sub')
        else:
            raise Exception('Unreachable!')

    def visit_mul2(self, op, _, __):
        if op == '*':
            self.insts.append('mul')
        elif op == '/':
            self.insts.append('div')
        else:
            raise Exception('Unreachable!')

    def visit_primary_literal(self, num):
        self.insts.append(f'push immediate ${num}')

    def write(self):
        return '\n'.join(self.insts)


class InterpreterVisitor:
    def __init__(self):
        self.stack = []

    def visit_add2(self, op, _, __):
        rhs = self.stack.pop()
        lhs = self.stack.pop()
        if op == '+':
            self.stack.append(lhs + rhs)
        elif op == '-':
            self.stack.append(lhs - rhs)
        else:
            raise Exception('Unreachable!')

    def visit_mul2(self, op, _, __):
        rhs = self.stack.pop()
        lhs = self.stack.pop()
        if op == '*':
            self.stack.append(lhs * rhs)
        elif op == '/':
            self.stack.append(lhs / rhs)
        else:
            raise Exception('Unreachable!')

    def visit_primary_literal(self, num):
        self.stack.append(num)

    def write(self):
        ret = self.stack.pop()
        assert len(self.stack) == 0
        return ret
        

def parse_expr(program, visitor):
    return parse_add(program, visitor)
    # if program.peek() != 'EOF':
    #     raise SyntaxError('Leftover input')


def parse_add(program, visitor):
    left = parse_mult(program, visitor)
    right = None

    while True:
        tok = program.peek()
        if tok not in ('+', '-'):
            break
        program.consume()
        right = parse_mult(program, visitor)
        left = visitor.visit_add2(tok, left, right)

    return left

def parse_mult(program, visitor):
    left = parse_primary(program, visitor)
    right = None

    while True:
        tok = program.peek()
        if tok not in ('*', '/'):
            break
        program.consume()
        right = parse_primary(program, visitor)
        left = visitor.visit_mul2(tok, left, right)

    return left

def parse_primary(program, visitor):
    if program.peek() == '(':
        program.consume()
        ret = parse_expr(program, visitor)
        # breakpoint()
        if program.peek() != ')':
            raise SyntaxError('Unclosed parentheses')
        program.consume()
        return ret

    # Else we have a literal
    num = int(program.peek())
    program.consume()
    return visitor.visit_primary_literal(num)

program_strings = [
    '1+2+3',
    '1/2+3*4',
    '9/3/2',
    '(1+2)/3',
]

visitor_classes = [
    SSAVisitor,
    StackMachineVisitor,
    ASTVisitor,
    InterpreterVisitor,
]

for program_string, visitor_class in itertools.product(program_strings, visitor_classes):
    print('==========================================')
    print(f'{program_string} visited by {visitor_class.__name__}')
    print('==========================================')
    program = Program(program_string)
    visitor = visitor_class()
    t = parse_expr(program, visitor)
    print('>>> Final t')
    pprint.pprint(t)
    print('>>> Output')
    print(visitor.write())

    
    
