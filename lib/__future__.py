"""Names that "from __future__ import ..." may ask for. All but annotations are
always on. The compiler reads the import itself; this module only supplies the names."""

all_feature_names = ["nested_scopes", "generators", "division", "absolute_import", "with_statement",
                     "print_function", "unicode_literals", "barry_as_FLUFL", "generator_stop", "annotations"]


class _Feature:
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return "_Feature(" + self.name + ")"


nested_scopes = _Feature("nested_scopes")
generators = _Feature("generators")
division = _Feature("division")
absolute_import = _Feature("absolute_import")
with_statement = _Feature("with_statement")
print_function = _Feature("print_function")
unicode_literals = _Feature("unicode_literals")
barry_as_FLUFL = _Feature("barry_as_FLUFL")
generator_stop = _Feature("generator_stop")
annotations = _Feature("annotations")
