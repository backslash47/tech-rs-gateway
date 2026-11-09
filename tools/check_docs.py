#!/usr/bin/env python3
"""Check API documentation contracts without checking generated or third-party code."""

import ast
from dataclasses import dataclass
from pathlib import Path
import re

from tree_sitter import Language, Parser
import tree_sitter_cpp
import tree_sitter_javascript

ROOT = Path(__file__).resolve().parents[1]
GENERATED = {"dynamic_menu_assets.h", "label_dictionary_data.h", "build_info.h"}


@dataclass
class Api:
    """A public declaration and its adjacent documentation, with source location."""

    path: Path
    line: int
    name: str
    parameters: list[str]
    returns: bool
    doc: str
    language: str


def descendants(node):
    """Yield an AST node and all descendants in source order, including syntax tokens.

    Args:
        node: Tree-sitter root for the traversal.
    Yields:
        Tree-sitter nodes, including the supplied root.
    """
    yield node
    for child in node.children:
        yield from descendants(child)


def adjacent_doc(node):
    """Find an immediately preceding Doxygen/JSDoc comment block.

    Args:
        node: Declaration node; trailing field comments are excluded.
    Returns:
        The comment text, or an empty string if no API comment is attached.
    """
    previous = node.prev_named_sibling
    comments = []
    while previous is not None and previous.type == "comment":
        text = previous.text.decode()
        before = previous.prev_named_sibling
        if before is not None and before.end_point.row == previous.start_point.row:
            break
        if not (text.startswith("/**") or text.startswith("///")):
            break
        comments.insert(0, text)
        previous = previous.prev_named_sibling
    return "\n".join(comments)


def cpp_apis(path):
    """Collect public types, functions and methods from one C++ header.

    Args:
        path: Project header path, excluding generated assets.
    Returns:
        API records; private/protected members and forward declarations are omitted.
    Raises:
        ValueError: The header cannot be parsed as C++.
    """
    tree = Parser(Language(tree_sitter_cpp.language())).parse(path.read_bytes())
    # tree-sitter-cpp 0.23.4 inserts a missing type in a valid default `= {}`.
    # Permit only that precise grammar quirk; other parse failures fail the audit.
    errors = [n for n in descendants(tree.root_node) if n.type == "ERROR" or n.is_missing]
    errors = [
        n
        for n in errors
        if not (
            n.is_missing
            and n.type == "type_identifier"
            and n.parent.type == "compound_literal_expression"
            and n.parent.text.strip() == b"{}"
        )
    ]
    if errors:
        raise ValueError(f"Cannot parse {path}")
    records = []

    def walk(container, scope, public=True):
        for node in container.named_children:
            if node.type == "access_specifier":
                public = node.text == b"public"
                continue
            if node.type == "namespace_definition":
                walk(node.child_by_field_name("body"), scope)
                continue
            wrapped = node
            if node.type in ("declaration", "field_declaration"):
                types = [
                    c
                    for c in node.named_children
                    if c.type in ("class_specifier", "struct_specifier", "enum_specifier")
                ]
                if types:
                    wrapped = types[0]
            if wrapped.type in ("class_specifier", "struct_specifier", "enum_specifier"):
                body = wrapped.child_by_field_name("body")
                name = wrapped.child_by_field_name("name")
                if not public or body is None or name is None:
                    continue
                qualified = scope + name.text.decode()
                records.append(
                    Api(
                        path,
                        node.start_point.row + 1,
                        qualified,
                        [],
                        False,
                        adjacent_doc(node),
                        "cpp",
                    )
                )
                if wrapped.type != "enum_specifier":
                    walk(body, qualified + "::", wrapped.type == "struct_specifier")
                continue
            if not public or node.type not in (
                "declaration",
                "field_declaration",
                "function_definition",
            ):
                continue
            declarator = node.child_by_field_name("declarator")
            if declarator is None:
                continue
            function = next(
                (n for n in descendants(declarator) if n.type == "function_declarator"), None
            )
            if function is None:
                continue
            name = function.child_by_field_name("declarator")
            if name is None:
                continue
            parameters = []
            for parameter in function.child_by_field_name("parameters").named_children:
                declaration = parameter.child_by_field_name("declarator")
                if declaration is not None:
                    identifiers = [
                        n
                        for n in descendants(declaration)
                        if n.type in ("identifier", "field_identifier")
                    ]
                    if identifiers:
                        parameters.append(identifiers[-1].text.decode())
            result = node.child_by_field_name("type")
            returns = result is not None and result.text != b"void"
            records.append(
                Api(
                    path,
                    node.start_point.row + 1,
                    scope + name.text.decode(),
                    parameters,
                    returns,
                    adjacent_doc(node),
                    "cpp",
                )
            )

    walk(tree.root_node, "")
    return records


def js_apis(path):
    """Collect named top-level JavaScript functions, including arrow helpers.

    Args:
        path: Handwritten browser JavaScript file.
    Returns:
        API records. Nested callbacks and event-handler assignments are internal.
    Raises:
        ValueError: The source cannot be parsed as JavaScript.
    """
    tree = Parser(Language(tree_sitter_javascript.language())).parse(path.read_bytes())
    if tree.root_node.has_error:
        raise ValueError(f"Cannot parse {path}")
    records = []
    for node in tree.root_node.named_children:
        functions = []
        if node.type == "function_declaration":
            functions.append((node.child_by_field_name("name"), node))
        elif node.type in ("lexical_declaration", "variable_declaration"):
            for variable in node.named_children:
                value = variable.child_by_field_name("value")
                if value is not None and value.type in ("arrow_function", "function_expression"):
                    functions.append((variable.child_by_field_name("name"), value))
        for name, function in functions:
            params = function.child_by_field_name("parameters")
            if params is None:
                params = function.child_by_field_name("parameter")
            parameters = []
            if params is not None:
                entries = params.named_children if params.type == "formal_parameters" else [params]
                for entry in entries:
                    binding = (
                        entry.child_by_field_name("left")
                        if entry.type == "assignment_pattern"
                        else entry
                    )
                    parameters.append(binding.text.decode())
            records.append(
                Api(
                    path,
                    node.start_point.row + 1,
                    name.text.decode(),
                    parameters,
                    True,
                    adjacent_doc(node),
                    "js",
                )
            )
    return records


def python_apis(path):
    """Collect module-level Python functions and classes from developer tools.

    Args:
        path: Python tool source path.
    Returns:
        API records using Google-style docstrings for parameter descriptions.
    """
    tree = ast.parse(path.read_text())
    records = []
    for node in tree.body:
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            continue
        params = (
            []
            if isinstance(node, ast.ClassDef)
            else [a.arg for a in (*node.args.posonlyargs, *node.args.args, *node.args.kwonlyargs)]
        )
        returns = not isinstance(node, ast.ClassDef) and any(
            isinstance(n, ast.Return) and n.value is not None for n in ast.walk(node)
        )
        records.append(
            Api(
                path,
                node.lineno,
                node.name,
                params,
                returns,
                ast.get_docstring(node) or "",
                "python",
            )
        )
    return records


def problems(api):
    """Check that a declaration's documentation names its actual parameters.

    Args:
        api: Parsed API record with its attached comment or docstring.
    Returns:
        A list of missing/incorrect contract elements; semantic accuracy needs review.
    """
    if not api.doc.strip():
        return ["missing API documentation"]
    if api.language == "python":
        args = re.search(r"(?:^|\n)Args:\n(.*?)(?=\n[A-Za-z][^\n]*:\n|\Z)", api.doc, re.DOTALL)
        documented = re.findall(
            r"^[ \t]*(\w+)[ \t]*(?:\([^\n]*\))?:[ \t]+\S",
            args.group(1) if args else "",
            re.MULTILINE,
        )
        result_tag = "Returns:" in api.doc or "Yields:" in api.doc
    else:
        documented = re.findall(r"@param(?:\[[^\]]+\])?\s+(?:\{[^\n]+?\}\s+)?(\w+)\b", api.doc)
        result_tag = bool(re.search(r"@returns?\b", api.doc))
    errors = [f"missing parameter {name}" for name in api.parameters if name not in documented]
    errors += [f"unknown parameter {name}" for name in documented if name not in api.parameters]
    if api.returns and not result_tag:
        errors.append("missing return contract")
    return errors


def inventory(root=ROOT):
    """Collect the project's handwritten public/API documentation surface.

    Args:
        root: Repository root; dependency directories and generated files are excluded.
    Returns:
        API records for C++ headers, browser functions and Python tools.
    """
    records = []
    for path in sorted((root / "components/tech_rs_gateway").glob("*.h")):
        if path.name not in GENERATED:
            records.extend(cpp_apis(path))
    for path in sorted((root / "web").glob("*.js")):
        records.extend(js_apis(path))
    for path in sorted((root / "tools").glob("*.py")):
        records.extend(python_apis(path))
    return records


def main():
    """Report API documentation gaps and return a nonzero status for failures.

    Returns:
        Zero when every checked declaration has the required contract; one otherwise.
    """
    records = inventory()
    failed = 0
    for api in records:
        for problem in problems(api):
            failed += 1
            print(f"{api.path.relative_to(ROOT)}:{api.line} {api.name}: {problem}")
    counts = {
        lang: sum(api.language == lang for api in records) for lang in ("cpp", "js", "python")
    }
    print(f"Checked {len(records)} declarations {counts}; {failed} documentation issues")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
