#!/bin/python
import sys, os

print("sys.platform:", sys.platform)
print("sys.version:", sys.version)
print("cwd:", os.getcwd())

# basic types
assert isinstance(42, int)
assert isinstance("hi", str)
assert isinstance([1,2], list)
assert isinstance({}, dict)

# list comprehension
squares = [x*x for x in range(10)]
assert squares == [0, 1, 4, 9, 16, 25, 36, 49, 64, 81]

# dict
d = {}
for i in range(26):
    d[chr(65+i)] = i
assert d['Z'] == 25

# generators
def fib():
    a, b = 0, 1
    while True:
        yield a
        a, b = b, a + b

g = fib()
first20 = [next(g) for _ in range(20)]
assert first20[-1] == 4181

# exceptions
try:
    x = 1 // 0
except ZeroDivisionError:
    pass

# classes
class Vec:
    def __init__(self, x, y):
        self.x, self.y = x, y
    def __add__(self, o):
        return Vec(self.x + o.x, self.y + o.y)
    def __repr__(self):
        return "Vec({}, {})".format(self.x, self.y)

assert repr(Vec(1,2) + Vec(3,4)) == "Vec(4, 6)"

print("all tests passed")
