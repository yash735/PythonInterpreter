import sys
import json

# Variable class to hold mutable values
class Variable:
    def __init__(self, value):
        self.value = value

# Initial environment with basic operations and bindings
global_environment = {
    "x": Variable(10),
    "v": Variable(5),
    "i": Variable(1),
    "add": lambda a, b: a + b,
    "sub": lambda a, b: a - b,
    "mul": lambda a, b: a * b,
    "div": lambda a, b: a // b if b != 0 else ValueError("Error: Division by zero"),
    "mod": lambda a, b: a % b if b != 0 else ValueError("Error: Division by zero"),
    "eq": lambda a, b: a == b,
    "true": True,
    "false": False,
    "zero?": lambda n: n == 0,
    "print": lambda *args: print(" ".join(map(str, args))),
}

# Evaluate expressions with lexical scoping and mutable variables
def evaluate_expression(expression, env=None):
    if env is None:
        env = global_environment

    # Integer
    if isinstance(expression, int):
        return expression

    # String
    elif isinstance(expression, str):
        return expression

    # Identifier lookup
    elif isinstance(expression, dict) and "Identifier" in expression:
        identifier = expression["Identifier"]
        if identifier in env:
            value = env[identifier]
            if isinstance(value, Variable):
                return value.value
            else:
                return value  # For functions and constants
        else:
            raise ValueError(f"Unbound identifier: {identifier}")

    # Function application
    elif isinstance(expression, dict) and "Application" in expression:
        func_expr = expression["Application"]
        func = evaluate_expression(func_expr[0], env)
        args = [evaluate_expression(arg, env) for arg in func_expr[1:]]
        if callable(func):
            return func(*args)
        else:
            raise ValueError(f"Unbound function or invalid function call: {func}")

    # Conditional
    elif isinstance(expression, dict) and "Cond" in expression:
        for clause in expression["Cond"]:
            test = clause["Clause"][0]
            consequent = clause["Clause"][1]
            test_result = evaluate_expression(test, env)
            if not isinstance(test_result, bool):
                raise TypeError(f"Condition {test_result} is not a boolean")
            if test_result:
                return evaluate_expression(consequent, env)
        raise ValueError("No clause evaluated to true")

    # Block of expressions
    elif isinstance(expression, dict) and "Block" in expression:
        block_result = None
        # Copy the environment, but keep the same Variable objects
        local_env = env.copy()
        for expr in expression["Block"]:
            block_result = evaluate_expression(expr, local_env)
        return block_result

    # Let expressions (Scoped bindings)
    elif isinstance(expression, dict) and "Let" in expression:
        let_expr = expression["Let"]
        var_name = let_expr[0]["Identifier"]
        var_value = evaluate_expression(let_expr[1], env)
        # Create a new Variable object for the new binding
        local_env = env.copy()
        local_env[var_name] = Variable(var_value)
        return evaluate_expression(let_expr[2], local_env)

    # Assignment expressions
    elif isinstance(expression, dict) and "Assignment" in expression:
        assignment_expr = expression["Assignment"]
        var_name = assignment_expr[0]["Identifier"]
        var_value = evaluate_expression(assignment_expr[1], env)
        if var_name in env:
            variable = env[var_name]
            if isinstance(variable, Variable):
                variable.value = var_value  # Update the variable's value
                return var_value
            else:
                raise ValueError(f"Cannot assign to non-variable: {var_name}")
        else:
            raise ValueError(f"Unbound identifier: {var_name}")

    # Lambda expressions with lexical scope
    elif isinstance(expression, dict) and "Lambda" in expression:
        parameters = expression["Lambda"][0]["Parameters"]
        body = expression["Lambda"][1]["Block"]
        closure_env = env.copy()  # Capture the lexical environment
        return lambda *args: evaluate_lambda(parameters, body, args, closure_env)

    else:
        raise ValueError(f"Invalid expression: {expression}")

# Evaluate lambda expressions with lexical scope
def evaluate_lambda(parameters, body, args, closure_env):
    if len(parameters) != len(args):
        raise ValueError(f"Expected {len(parameters)} arguments, got {len(args)}")
    # Create a new environment for the function call
    local_env = closure_env.copy()
    for param, arg in zip(parameters, args):
        param_name = param["Identifier"]
        local_env[param_name] = Variable(arg)
    result = None
    for expr in body:
        result = evaluate_expression(expr, local_env)
    return result

# Read input and evaluate
def read_input():
    try:
        # Parse JSON input
        parsed_input = json.loads(sys.stdin.read().strip())
        result = evaluate_expression(parsed_input)
        print(result)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON input: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    read_input()
