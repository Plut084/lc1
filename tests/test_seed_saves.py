"""Check that repeated seed installation preserves player progress and edits."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--script", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="lc1-seed-test-") as scratch:
        source = Path(scratch) / "source"
        destination = Path(scratch) / "destination"
        (source / "01").mkdir(parents=True)
        (source / "01" / "characters.yaml").write_text("seed", encoding="utf-8")
        # A checkpoint accidentally present in a source tree is never installed as seed.
        (source / "01" / "checkpoint.yaml").write_text("unwanted", encoding="utf-8")

        def install():
            subprocess.run(
                [args.cmake, f"-DSOURCE_DIR={source}", f"-DDESTINATION_DIR={destination}",
                 "-P", args.script], check=True,
            )

        install()
        installed = destination / "01" / "characters.yaml"
        assert installed.read_text(encoding="utf-8") == "seed"
        checkpoint = destination / "01" / "checkpoint.yaml"
        assert not checkpoint.exists()
        installed.write_text("player edits", encoding="utf-8")
        checkpoint.write_text("player progress", encoding="utf-8")
        (source / "01" / "characters.yaml").write_text("new seed", encoding="utf-8")
        (source / "01" / "session.yaml").write_text("missing file", encoding="utf-8")
        install()
        assert installed.read_text(encoding="utf-8") == "player edits"
        assert checkpoint.read_text(encoding="utf-8") == "player progress"
        assert (destination / "01" / "session.yaml").read_text(encoding="utf-8") == "missing file"


if __name__ == "__main__":
    main()
