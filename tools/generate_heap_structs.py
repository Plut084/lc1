#!/usr/bin/env python3
"""Generate Slang data structs and descriptor-heap resolvers from a small schema."""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


class SchemaError(ValueError):
    pass


@dataclass(frozen=True)
class Field:
    type_name: str
    name: str
    heap: str | None = None

    @property
    def stored_name(self):
        return self.name + "_index" if self.heap else self.name


@dataclass(frozen=True)
class Struct:
    name: str
    fields: list[Field]


def function_suffix(name):
    name = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()


def is_value_type(name):
    return re.fullmatch(r"(?:float|int|uint)(?:[1-4](?:x[1-4])?)?", name) is not None


class Parser:
    def __init__(self, source):
        # Keep offsets intact for diagnostics, including newlines in comments.
        self.source = re.sub(
            r"//[^\n]*|/\*.*?\*/",
            lambda match: re.sub(r"[^\n]", " ", match.group()),
            source,
            flags=re.DOTALL,
        )
        self.tokens = list(re.finditer(r"[A-Za-z_][A-Za-z_0-9]*|\S", self.source))
        self.position = 0

    def error(self, message):
        offset = (self.tokens[self.position].start()
                  if self.position < len(self.tokens) else len(self.source))
        line = self.source.count("\n", 0, offset) + 1
        column = offset - self.source.rfind("\n", 0, offset)
        raise SchemaError(f"{line}:{column}: {message}")

    def peek(self):
        return (self.tokens[self.position].group()
                if self.position < len(self.tokens) else None)

    def take(self, expected=None):
        value = self.peek()
        if value is None or (expected is not None and value != expected):
            self.error(f"expected {expected or 'a token'}, got {value or 'end of file'}")
        self.position += 1
        return value

    def identifier(self):
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", self.peek() or ""):
            self.error("expected an identifier")
        return self.take()

    def parse(self):
        structs = []
        value_types = set()
        symbols = set()
        while self.peek() is not None:
            self.take("struct")
            name = self.identifier()
            if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", name):
                self.error("struct names must use PascalCase")
            suffix = function_suffix(name)
            new_symbols = {name, name + "Data", "resolve_" + suffix, "load_" + suffix}
            if symbols & new_symbols:
                self.error(f"duplicate or generated name collision for {name}")
            symbols.update(new_symbols)
            self.take("{")
            fields = []
            names = set()
            stored_names = set()
            while self.peek() != "}":
                type_name = self.identifier()
                heap = None
                if type_name in {"SamplerState", "SamplerComparisonState"}:
                    heap = "SamplerDescriptorHeap"
                elif type_name in {
                    "Texture1D", "Texture2D", "Texture3D", "TextureCube",
                    "Texture1DArray", "Texture2DArray", "TextureCubeArray",
                    "RWTexture1D", "RWTexture2D", "RWTexture3D",
                    "ConstantBuffer", "StructuredBuffer", "RWStructuredBuffer",
                }:
                    if "Texture" in type_name and self.peek() != "<":
                        element = "float4"
                    else:
                        self.take("<")
                        element = self.identifier()
                        self.take(">")
                    if not (is_value_type(element) or element in value_types):
                        self.error(f"unknown or resource-containing element type {element}; "
                                   "declare data types first or use their generated Data type")
                    if "Texture" in type_name and not re.fullmatch(
                        r"(?:float|int|uint)[1-4]?", element
                    ):
                        self.error("texture elements must be numeric scalars or vectors")
                    type_name += f"<{element}>"
                    heap = "ResourceDescriptorHeap"
                elif type_name in {"ByteAddressBuffer", "RWByteAddressBuffer"}:
                    heap = "ResourceDescriptorHeap"
                elif not (is_value_type(type_name) or type_name in value_types):
                    self.error(f"unknown or unsupported field type {type_name}")
                field_name = self.identifier()
                if not re.fullmatch(r"[a-z][a-z0-9_]*", field_name):
                    self.error("field names must use snake_case")
                self.take(";")
                field = Field(type_name, field_name, heap)
                if field_name in names or field.stored_name in stored_names:
                    self.error(f"duplicate or generated field collision for {field_name}")
                names.add(field_name)
                stored_names.add(field.stored_name)
                fields.append(field)
            self.take("}")
            self.take(";")
            if not fields:
                self.error("empty structs are unsupported")
            structs.append(Struct(name, fields))
            value_types.add(name + "Data")
            if not any(field.heap for field in fields):
                value_types.add(name)
        if not structs:
            self.error("expected at least one struct")
        return structs


def generate(source):
    structs = Parser(source).parse()
    lines = ["// Generated by generate_heap_structs.py. Do not edit.", "#pragma once", ""]
    for struct in structs:
        lines.append(f"struct {struct.name}Data {{")
        for field in struct.fields:
            lines.append(f"    {'uint' if field.heap else field.type_name} {field.stored_name};")
        lines.extend(["};", "", f"struct {struct.name} {{"])
        for field in struct.fields:
            lines.append(f"    {field.type_name} {field.name};")
        suffix = function_suffix(struct.name)
        lines.extend(["};", "", f"{struct.name} resolve_{suffix}({struct.name}Data data)",
                      "{", f"    {struct.name} result;"])
        for field in struct.fields:
            value = f"data.{field.stored_name}"
            if field.heap:
                value = f"{field.heap}[{value}]"
            lines.append(f"    result.{field.name} = {value};")
        lines.extend(["    return result;", "}", "",
                      f"{struct.name} load_{suffix}(uint index)", "{",
                      f"    ConstantBuffer<{struct.name}Data> data = ResourceDescriptorHeap[index];",
                      f"    return resolve_{suffix}(data);", "}", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.source.resolve() == args.output.resolve():
            raise SchemaError("input and output must be different files")
        output = generate(args.source.read_text(encoding="utf-8"))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    except (OSError, UnicodeError, SchemaError) as error:
        print(f"{args.source}:{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
