"""CPU-only schema validation and real Slang compilation of generated resource access."""

import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools/generate_heap_structs.py"
spec = importlib.util.spec_from_file_location("heap_generator", GENERATOR)
generator = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = generator
spec.loader.exec_module(generator)
SLANGC = None


class GeneratorTests(unittest.TestCase):
    def test_resource_heap_selection_and_order(self):
        output = generator.generate("""
            /* storage order matters */
            struct Material {
                float3 color; // a value, not a descriptor
                Texture2D<float4> texture;
                float roughness;
                SamplerState sampler;
            };
        """)
        self.assertIn("float3 color;\n    uint texture_index;\n    float roughness;\n"
                      "    uint sampler_index;", output)
        self.assertIn("result.texture = ResourceDescriptorHeap[data.texture_index];", output)
        self.assertIn("result.sampler = SamplerDescriptorHeap[data.sampler_index];", output)
        self.assertIn("result.roughness = data.roughness;", output)

    def test_default_texture_element(self):
        implicit = generator.generate("struct Material { Texture2D texture; };")
        explicit = generator.generate("struct Material { Texture2D<float4> texture; };")
        self.assertEqual(implicit, explicit)

    def test_reject_invalid_schemas(self):
        sources = [
            "", "/* unterminated", "struct Empty {};",
            "struct A { float x[2]; };",
            "struct A { float x = 1; };",
            "struct A { float x, y; };",
            "struct A { Missing x; };",
            "struct A { float x; float x; };",
            "struct A { Texture2D<float4> x; uint x_index; };",
            "struct A { uint x_index; Texture2D<float4> x; };",
            "struct A { float x; }; struct AData { float y; };",
            "struct AData { float x; }; struct A { float y; };",
            "struct A { Texture2D<float4> x; }; struct B { A a; };",
            "struct A { Texture2D<float4> x; }; struct B { ConstantBuffer<A> a; };",
            "struct A { Texture2D<float4x4> x; };",
            "struct A { float x; }; #include \"other.slang\"",
        ]
        for source in sources:
            with self.subTest(source=source), self.assertRaises(generator.SchemaError):
                generator.generate(source)

    def test_diagnostic_location(self):
        with self.assertRaisesRegex(generator.SchemaError, r"2:.*expected ;"):
            generator.generate("// comment\nstruct A { float value[2]; };")

    def test_cli_preserves_output_on_invalid_input(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid.heap"
            output = Path(directory) / "generated.slang"
            source.write_text("struct A { Unknown x; };", encoding="utf-8")
            output.write_text("existing output", encoding="utf-8")
            result = subprocess.run([sys.executable, str(GENERATOR), str(source),
                                     "-o", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("unsupported field type", result.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), "existing output")
            result = subprocess.run([sys.executable, str(GENERATOR), str(source),
                                     "-o", str(source)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("must be different", result.stderr)

    def test_slang_compilation(self):
        if SLANGC is None:
            self.skipTest("pass --slangc to compile the generated shader")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "heap-material.slang"
            subprocess.run([sys.executable, str(GENERATOR),
                            str(ROOT / "tests/shaders/heap-material.heap"), "-o", str(output)],
                           check=True, capture_output=True, text=True)
            result = subprocess.run([
                SLANGC, str(ROOT / "tests/shaders/heap-material-test.slang"),
                "-I", directory, "-target", "spirv", "-profile", "spirv_1_4",
                "-capability", "spvDescriptorHeapEXT", "-emit-spirv-directly",
                "-O0", "-o", str(Path(directory) / "test.spv"),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((Path(directory) / "test.spv").is_file())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--slangc")
    args, remaining = parser.parse_known_args()
    SLANGC = args.slangc
    unittest.main(argv=[sys.argv[0], *remaining])
