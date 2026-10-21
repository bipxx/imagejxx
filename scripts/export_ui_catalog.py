"""Export the installed native registry's legacy UI catalog for tooling."""

import json
from pathlib import Path
from imagejxx import operation_catalog


def main():
    catalog = operation_catalog()
    destination = Path(__file__).resolve().parents[1] / "src/imagejxx/ui/commands.json"
    destination.write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(f"Exported {len(catalog)} commands")


if __name__ == "__main__":
    main()
