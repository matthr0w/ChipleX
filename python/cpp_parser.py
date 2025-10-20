import sys
from pathlib import Path

from clang import cindex

from logging import *

HEADER_FILE = Path("include/usercode/UserCode.h")

# Clang arguments
CLANG_ARGS = [
    "-x", "c++",
    "-std=c++17",
    "-Iinclude",
]

def clang_parse(filepath: Path):
    if not filepath.exists():
        log_error("Cpp Parser", f"Header file not found at '{filepath}'")
        
    try:
        index = cindex.Index.create()
        tu = index.parse(
            filepath, 
            args=CLANG_ARGS,
            options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
        )
        
        for diagnostic in tu.diagnostics:
            if diagnostic.severity > 2:
                log_warn("Clang", diagnostic)
    except Exception as e:
        log_error("Cpp Parser", e)

    return tu

def find_annotation_region(source):
    si = source.find("// @PROC_SIM_START")
    ei = source.find("// @PROC_SIM_END")
    if si == -1 or ei == -1:
        log_warn("Cpp Parser", "Annotation tags not found")
        sys.exit(0)
    start_line = source[:si].count("\n") + 1
    end_line = source[:ei].count("\n") + 1
    return start_line, end_line

def analyze_source(tu, source):
    start_line, end_line = find_annotation_region(source)
    source_file = tu.spelling

    declared, used = set(), set()
    struct_fields = set()

    # Collect struct fields
    for node in tu.cursor.get_children():
        for c in node.walk_preorder():
            # Skip if not in header file
            if not c.location.file or c.location.file.name != source_file:
                continue
            if c.kind == cindex.CursorKind.FIELD_DECL:
                struct_fields.add(c.spelling)

    # Collect declared identifiers
    for node in tu.cursor.get_children():
        for c in node.walk_preorder():
            # Skip if not in header file
            if not c.location.file or c.location.file.name != source_file:
                continue
            # Skip if not in annotated region
            if c.extent.start.line < start_line or c.extent.end.line > end_line:
                continue
            if c.kind in (
                cindex.CursorKind.VAR_DECL,
                cindex.CursorKind.PARM_DECL,
                cindex.CursorKind.STRUCT_DECL,
                cindex.CursorKind.CLASS_DECL,
                cindex.CursorKind.TYPEDEF_DECL,
            ):
                if c.spelling:
                    declared.add(c.spelling)

    def is_user_symbol(spelling):
        if spelling in {
            "std",
            "auto", "unsigned", "unsigned int", "int", "float", "double", "char",
            "uint8_t", "uint16_t", "uint32_t", "uint64_t", "size_t",
            "new", "delete",
            "memcpy", "memset",
            "for", "while",
            "if", "else if", "else",
            "return"
        }:
            return False
        return True

    # Collect used identifiers
    tokens = list(tu.get_tokens(extent=tu.cursor.extent))
    region_tokens = [
        t for t in tokens
        if start_line <= t.location.line <= end_line
        and t.location.file
        and t.location.file.name == source_file
        and t.kind.name == "IDENTIFIER"
    ]
    for t in region_tokens:
        if is_user_symbol(t.spelling):
            used.add(t.spelling)

    undeclared = [
        s for s in used
        if s not in declared
        and s not in struct_fields
    ]

    return {
        "region": (start_line, end_line),
        "used": sorted(used),
        "declared": sorted(declared),
        "undeclared": sorted(undeclared),
    }

tu = clang_parse(HEADER_FILE)
source = HEADER_FILE.read_text()

result = analyze_source(tu, source)
log_info("Cpp Parser", f"Region: {result['region']}")
log_info("Cpp Parser", f"Used tokens: {result['used']}")
log_info("Cpp Parser", f"Declared tokens: {result['declared']}")
log_info("Cpp Parser", f"Undeclared tokens: {result['undeclared']}")