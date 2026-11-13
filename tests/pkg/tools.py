import helper_mod
def double(x): return x * 2
def bump_twice(): helper_mod.bump(); return helper_mod.bump()
__all__ = ["double"]
