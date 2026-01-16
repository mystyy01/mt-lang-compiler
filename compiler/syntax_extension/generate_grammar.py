#!/usr/bin/env python3
"""
Generates mtc.tmLanguage.json from the tokenizer.py KEYWORDS list.
Run this script whenever you add new keywords to the language.
"""

import json
import re
import os
from pathlib import Path

# Path to the tokenizer relative to this script
SCRIPT_DIR = Path(__file__).parent
TOKENIZER_PATH = SCRIPT_DIR.parent / "tokenizer.py"
OUTPUT_PATH = SCRIPT_DIR / "syntaxes" / "mtc.tmLanguage.json"

# Categorize keywords for better semantic highlighting
# Update these lists when you add new keywords
TYPES = {"int", "float", "void", "array", "string", "bool"}
CONTROL_FLOW = {"if", "else", "for", "while", "return", "in"}
IMPORTS = {"from", "use", "as"}
CONSTANTS = {"true", "false"}
CLASS_KEYWORDS = {"func", "class", "new", "this", "static", "virtual"}
# Everything else will be treated as builtins/other keywords


def read_keywords_from_tokenizer():
    """Parse KEYWORDS list from tokenizer.py"""
    with open(TOKENIZER_PATH, "r") as f:
        content = f.read()

    # Find the KEYWORDS = [...] line
    match = re.search(r'KEYWORDS\s*=\s*\[(.*?)\]', content, re.DOTALL)
    if not match:
        raise ValueError("Could not find KEYWORDS list in tokenizer.py")

    keywords_str = match.group(1)
    # Extract all quoted strings
    keywords = re.findall(r'["\'](\w+)["\']', keywords_str)
    return list(set(keywords))  # Remove duplicates


def categorize_keywords(keywords):
    """Split keywords into categories for semantic highlighting"""
    types = []
    control = []
    imports = []
    constants = []
    class_kw = []
    builtins = []

    for kw in keywords:
        if kw in TYPES:
            types.append(kw)
        elif kw in CONTROL_FLOW:
            control.append(kw)
        elif kw in IMPORTS:
            imports.append(kw)
        elif kw in CONSTANTS:
            constants.append(kw)
        elif kw in CLASS_KEYWORDS:
            class_kw.append(kw)
        else:
            builtins.append(kw)

    return {
        "types": sorted(types),
        "control": sorted(control),
        "imports": sorted(imports),
        "constants": sorted(constants),
        "class": sorted(class_kw),
        "builtins": sorted(builtins),
    }


def generate_grammar(categorized):
    """Generate the TextMate grammar JSON"""

    def make_keyword_pattern(words, scope):
        """Create a pattern that matches any of the given words"""
        if not words:
            return None
        pattern = "\\b(" + "|".join(re.escape(w) for w in words) + ")\\b"
        return {
            "match": pattern,
            "name": scope
        }

    patterns = []

    # Comments
    patterns.append({
        "name": "comment.line.double-slash.mtc",
        "match": "//.*$"
    })

    # Strings (double and single quoted)
    patterns.append({
        "name": "string.quoted.double.mtc",
        "begin": "\"",
        "end": "\"",
        "patterns": [
            {
                "name": "constant.character.escape.mtc",
                "match": "\\\\."
            }
        ]
    })
    patterns.append({
        "name": "string.quoted.single.mtc",
        "begin": "'",
        "end": "'",
        "patterns": [
            {
                "name": "constant.character.escape.mtc",
                "match": "\\\\."
            }
        ]
    })

    # Numbers (integers and floats, including scientific notation)
    patterns.append({
        "name": "constant.numeric.float.mtc",
        "match": "\\b-?[0-9]+\\.[0-9]+([eE][+-]?[0-9]+)?\\b"
    })
    patterns.append({
        "name": "constant.numeric.integer.mtc",
        "match": "\\b-?[0-9]+([eE][+-]?[0-9]+)?\\b"
    })

    # Boolean constants
    if categorized["constants"]:
        patterns.append(make_keyword_pattern(
            categorized["constants"],
            "constant.language.boolean.mtc"
        ))

    # Type keywords
    if categorized["types"]:
        patterns.append(make_keyword_pattern(
            categorized["types"],
            "storage.type.mtc"
        ))

    # Control flow
    if categorized["control"]:
        patterns.append(make_keyword_pattern(
            categorized["control"],
            "keyword.control.mtc"
        ))

    # Import keywords
    if categorized["imports"]:
        patterns.append(make_keyword_pattern(
            categorized["imports"],
            "keyword.control.import.mtc"
        ))

    # Class/function keywords
    if categorized["class"]:
        patterns.append(make_keyword_pattern(
            categorized["class"],
            "keyword.other.mtc"
        ))

    # Built-in functions
    if categorized["builtins"]:
        patterns.append(make_keyword_pattern(
            categorized["builtins"],
            "support.function.builtin.mtc"
        ))

    # Function calls (identifier followed by parenthesis)
    patterns.append({
        "match": "\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()",
        "captures": {
            "1": {"name": "entity.name.function.mtc"}
        }
    })

    # Class names (after 'new' keyword or as type before variable name)
    patterns.append({
        "match": "\\b(new)\\s+([A-Z][a-zA-Z0-9_]*)",
        "captures": {
            "1": {"name": "keyword.other.mtc"},
            "2": {"name": "entity.name.class.mtc"}
        }
    })

    # Class definitions
    patterns.append({
        "match": "\\b(class)\\s+([A-Z][a-zA-Z0-9_]*)",
        "captures": {
            "1": {"name": "keyword.other.mtc"},
            "2": {"name": "entity.name.class.mtc"}
        }
    })

    # Type annotations (capitalized identifier as type)
    patterns.append({
        "match": "\\b([A-Z][a-zA-Z0-9_]*)\\s+([a-z_][a-zA-Z0-9_]*)\\s*=",
        "captures": {
            "1": {"name": "entity.name.class.mtc"},
            "2": {"name": "variable.other.mtc"}
        }
    })

    # Operators
    patterns.append({
        "name": "keyword.operator.mtc",
        "match": "==|!=|<=|>=|&&|\\|\\||[+\\-*/=<>!]"
    })

    # Remove None entries
    patterns = [p for p in patterns if p is not None]

    grammar = {
        "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
        "name": "MTC",
        "scopeName": "source.mtc",
        "patterns": patterns
    }

    return grammar


def main():
    print(f"Reading keywords from: {TOKENIZER_PATH}")
    keywords = read_keywords_from_tokenizer()
    print(f"Found {len(keywords)} keywords: {', '.join(sorted(keywords))}")

    categorized = categorize_keywords(keywords)
    print("\nCategorized keywords:")
    for category, words in categorized.items():
        if words:
            print(f"  {category}: {', '.join(words)}")

    grammar = generate_grammar(categorized)

    # Ensure output directory exists
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)

    with open(OUTPUT_PATH, "w") as f:
        json.dump(grammar, f, indent=2)

    print(f"\nGenerated grammar at: {OUTPUT_PATH}")
    print("\nTo update VS Code, reload the window (Ctrl+Shift+P -> 'Reload Window')")


if __name__ == "__main__":
    main()
