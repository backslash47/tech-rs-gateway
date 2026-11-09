"""Positive and negative fixtures for the source-level API documentation checker."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
AVAILABLE = all(
    importlib.util.find_spec(name)
    for name in ("tree_sitter", "tree_sitter_cpp", "tree_sitter_javascript")
)
if AVAILABLE:
    spec = importlib.util.spec_from_file_location(
        "documentation_check", ROOT / "tools/check_docs.py"
    )
    check = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = check
    spec.loader.exec_module(check)


@unittest.skipUnless(AVAILABLE, "Install pinned documentation parsers from requirements-dev.txt")
class DocumentationTests(unittest.TestCase):
    def file(self, name, content):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / name
        path.write_text(content)
        return path

    def test_cpp_public_scope_and_default_initializer(self):
        path = self.file(
            "api.h",
            """
/** @brief An API. */
class Example {
public:
    /** @brief Construct a reader.
     * @param reader Optional callback.
     */
    explicit Example(std::function<size_t()> reader = {});
    /** @brief Read an input.
     * @param value Input number.
     * @return The result.
     */
    int read(int value);
private:
    int internal(int undocumented);
};
""",
        )
        records = check.cpp_apis(path)
        self.assertEqual(
            [r.name for r in records], ["Example", "Example::Example", "Example::read"]
        )
        self.assertTrue(all(not check.problems(r) for r in records))

    def test_missing_comment_and_wrong_parameter_are_rejected(self):
        path = self.file(
            "api.h",
            """
int missing(int value);
/** @brief Reads a value.
 * @param typo Wrong parameter.
 */
int read(int value);
""",
        )
        first, second = check.cpp_apis(path)
        self.assertIn("missing API documentation", check.problems(first))
        self.assertEqual(
            set(check.problems(second)),
            {"missing parameter value", "unknown parameter typo", "missing return contract"},
        )

    def test_invalid_cpp_is_not_silently_skipped(self):
        path = self.file("broken.h", "class Broken { public: void broken(")
        with self.assertRaises(ValueError):
            check.cpp_apis(path)

    def test_trailing_field_comment_does_not_document_next_method(self):
        path = self.file(
            "api.h",
            """
/** @brief Public fields. */
struct Example {
    int field; ///< Field description.
    void missing();
};
""",
        )
        method = check.cpp_apis(path)[1]
        self.assertIn("missing API documentation", check.problems(method))

    def test_javascript_arrow_defaults_and_nested_callbacks(self):
        path = self.file(
            "api.js",
            """
/** Build a value.
 * @param {number} value Input number.
 * @returns {number} Output number.
 */
const create = (value = 1) => value;
/** Bind internal behavior.
 * @returns {void} No result.
 */
function bind() { const internal = () => 1; }
""",
        )
        records = check.js_apis(path)
        self.assertEqual([r.name for r in records], ["create", "bind"])
        self.assertTrue(all(not check.problems(r) for r in records))

    def test_python_google_sections_and_typos(self):
        path = self.file(
            "api.py",
            '''
def read(value):
    """Read an input.

    Args:
        value: Input data.
    Returns:
        An owned result.
    Raises:
        ValueError: Invalid data.
    """
    return value
''',
        )
        record = check.python_apis(path)[0]
        self.assertEqual(check.problems(record), [])
        record.doc = record.doc.replace("value: Input", "typo: Input")
        self.assertEqual(
            set(check.problems(record)), {"missing parameter value", "unknown parameter typo"}
        )

    def test_actual_project_has_no_documentation_gaps(self):
        errors = [
            f"{api.name}: {error}" for api in check.inventory(ROOT) for error in check.problems(api)
        ]
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
