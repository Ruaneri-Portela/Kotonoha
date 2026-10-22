"""Read-only audit of School Days ORS movie references against extracted assets."""

import json
import sys
from collections import Counter, defaultdict
from pathlib import Path


def read_ors(path):
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8-sig", errors="replace")


def audit(root):
    ors_files = sorted(p for episode in range(6)
                       for p in (root / f"{episode:02d}").rglob("*.ORS"))
    actual = defaultdict(list)
    for directory in root.rglob("*"):
        if directory.is_file() and directory.suffix.lower() == ".wmv":
            actual[directory.relative_to(root).as_posix().casefold()].append(
                directory.relative_to(root).as_posix()
            )

    references = []
    malformed = []
    for ors in ors_files:
        for line_no, line in enumerate(read_ors(ors).splitlines(), 1):
            kind = next((name for name in ("PlayMovie", "EndRoll")
                         if line.startswith("[" + name + "]=")), None)
            if kind is None:
                continue
            payload = line.split("=", 1)[1].rstrip(";")
            fields = payload.split("\t")
            if len(fields) != (4 if kind == "PlayMovie" else 3):
                malformed.append({"ors": ors.relative_to(root).as_posix(),
                                  "line": line_no, "kind": kind, "field_count": len(fields)})
                continue
            logical = fields[1]
            requested = logical + ".wmv"
            matches = actual.get(requested.casefold(), [])
            status = ("missing" if not matches else
                      "case_collision" if len(matches) > 1 else
                      "exact" if requested == matches[0] else "case_mismatch")
            references.append({"ors": ors.relative_to(root).as_posix(),
                               "line": line_no, "kind": kind,
                               "logical_path": logical, "requested": requested,
                               "matches": matches, "status": status})

    unique = defaultdict(list)
    for item in references:
        unique[item["requested"]].append(item)
    return {
        "source": "assets/00..05 ORS; all WMV under assets; read-only",
        "ors_files": len(ors_files),
        "reference_count": len(references),
        "by_kind": dict(Counter(x["kind"] for x in references)),
        "by_status": dict(Counter(x["status"] for x in references)),
        "unique_requested_paths": len(unique),
        "duplicate_reference_paths": sum(len(v) > 1 for v in unique.values()),
        "malformed_movie_lines": malformed,
        "references": references,
    }


if __name__ == "__main__":
    assets = Path(sys.argv[1] if len(sys.argv) > 1 else "assets")
    output = Path(sys.argv[2] if len(sys.argv) > 2 else
                  "docs/school-days-runtime/G2_5_MOVIE_ASSET_AUDIT.json")
    result = audit(assets)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items()
                      if key != "references"}, ensure_ascii=False))
