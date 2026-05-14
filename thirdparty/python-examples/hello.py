#!/bin/python
print("Hello from MicroPython on SBUnix!")
print("2 + 2 =", 2 + 2)
print("fib(10) =", (lambda n: (lambda f: f(f, n))(lambda f, n: n if n < 2 else f(f, n-1) + f(f, n-2)))(10))
