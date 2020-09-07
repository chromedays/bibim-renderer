from pathlib import Path
import difflib
from common import capture_output_from_command


def main():
    passed = True
    for pattern in ["*.h", "*.c", "*.hpp", "*.cpp"]:
        for filename in Path("src").glob(pattern):
            print(f"Comparing {filename}")
            original = None
            with open(str(filename), mode="r") as f:
                original = f.read()
            formatted = capture_output_from_command(["clang-format", str(filename)])
            diff = list(
                difflib.unified_diff(
                    original.split("\n"), formatted.split("\n"), "original", "formatted"
                )
            )
            if len(diff) == 0:
                print("Passed")
            else:
                passed = False
                print("Failed")
                for text in diff:
                    print(text.strip("\n"))

    if not passed:
        print(f"clang-format test failed.")
        exit(1)


if __name__ == "__main__":
    main()