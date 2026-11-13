import os
import json
import sys

def generate_manifest(asset_dir, output_json):
    if not os.path.exists(asset_dir):
        print(f"Warning: Asset directory {asset_dir} does not exist. Creating empty manifest.")
        os.makedirs(os.path.dirname(output_json), exist_ok=True)
        with open(output_json, "w", encoding="utf-8") as f:
            json.dump({"files": []}, f, indent=2)
        return

    files = []
    for root, dirs, filenames in os.walk(asset_dir):
        for filename in filenames:
            if filename == ".gitignore" or filename == "README.txt" or filename == "manifest.json":
                continue
            full_path = os.path.join(root, filename)
            rel_path = os.path.relpath(full_path, asset_dir)
            rel_path = rel_path.replace("\\", "/")
            files.append(rel_path)

    os.makedirs(os.path.dirname(output_json), exist_ok=True)
    manifest = {
        "files": files
    }

    with open(output_json, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print(f"[Kotonoha] Manifest generated at {output_json} with {len(files)} files.")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 generate_manifest.py <asset_dir> <output_json>")
        sys.exit(1)
    generate_manifest(sys.argv[1], sys.argv[2])
