"""A helper module for the import test."""
print("helper_mod loading, __name__ =", __name__)
VALUE = 42
_private = "hidden"
counter = 0
def bump():
    global counter
    counter += 1
    return counter
class Thing:
    kind = "thing"
    def __repr__(self): return "Thing()"
if __name__ == "__main__":
    print("not printed on import")
