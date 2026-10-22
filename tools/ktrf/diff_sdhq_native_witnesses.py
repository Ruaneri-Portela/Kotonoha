#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, re, subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence

@dataclass(frozen=True)
class Step:
    route:int; scene:int; choice:int; transition:int
@dataclass(frozen=True)
class Witness:
    name:str; steps:tuple[Step,...]

def witnesses(path:Path)->list[Witness]:
    text=path.read_text(encoding="utf-8")
    blocks=re.compile(r"static\s+const\s+Step\s+(kEnding\d+)\[\]\s*=\s*\{(.*?)\n\};",re.S)
    rows=re.compile(r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}")
    out=[Witness(m.group(1),tuple(Step(*(int(x) for x in r.groups())) for r in rows.finditer(m.group(2)))) for m in blocks.finditer(text)]
    if len(out)!=22: raise RuntimeError(f"expected 22 witnesses, found {len(out)}")
    return out

def payload(w:Witness)->str:
    return "RESET\n"+"".join(f"STEP {s.route} {s.scene} {s.choice} {s.transition}\n" for s in w.steps)

def run(exe:Path,w:Witness,artifact:Path|None=None)->list[dict[str,Any]]:
    cmd=[str(exe)]+([str(artifact)] if artifact else [])
    p=subprocess.run(cmd,input=payload(w),text=True,capture_output=True,check=False)
    if p.returncode: raise RuntimeError(f"trace failed {w.name}: {' '.join(cmd)}\n{p.stdout}\n{p.stderr}")
    out=[json.loads(line) for line in p.stdout.splitlines() if line.strip()]
    if len(out)!=len(w.steps): raise RuntimeError(f"row count mismatch {w.name}: {len(out)} != {len(w.steps)}")
    return out

def fail(w:str,i:int,k:str,a:Any,b:Any)->None:
    raise AssertionError(f"{w} step {i}: {k} divergence\n  C++ oracle: {a!r}\n  native KTRF: {b!r}")

def catalog(doc:Mapping[str,Any],name:str)->list[Mapping[str,Any]]:
    rows=[x for x in doc.get(name,[]) if isinstance(x,Mapping) and isinstance(x.get("id"),str)]
    return sorted(rows,key=lambda x:str(x["id"]).encode("utf-8"))

def meta(row:Mapping[str,Any],key:str,default:Any=None)->Any:
    m=row.get("metadata",{})
    return m.get(key,default) if isinstance(m,Mapping) else default

class MappingModel:
    def __init__(self,doc:Mapping[str,Any]):
        self.vars=catalog(doc,"variables"); self.nodes=catalog(doc,"nodes")
        self.trans=catalog(doc,"transitions"); self.endings=catalog(doc,"endings"); self.hooks=catalog(doc,"external_hooks")
        self.var_index={str(v["id"]):i for i,v in enumerate(self.vars)}
        self.choice=self.var_index["sdhq:var:internal:choice_result"]
        self.cb34=self.var_index["sdhq:var:internal:callback34"]
        self.route=next(i for i,v in enumerate(self.vars) if meta(v,"source_symbol")=="ROUTE" and meta(v,"source_storage")=="session")
        self.scene=next(i for i,v in enumerate(self.vars) if meta(v,"source_symbol")=="SCENE" and meta(v,"source_storage")=="session")
    def node_scene(self,index:int)->str:
        if index==0xFFFFFFFF:return ""
        return str(meta(self.nodes[index],"scene_key",""))
    def transition_source_id(self,index:int)->int:
        value=meta(self.trans[index],"source_transition_id")
        if not isinstance(value,int): raise AssertionError("transition lacks source_transition_id")
        return value
    def ending_codes(self,indices:Sequence[int])->list[int]:
        return [int(self.endings[i]["code"]) for i in indices]
    def hook_names(self,indices:Sequence[int])->list[str]:
        out=[]
        for i in indices:
            symbol=str(self.hooks[i]["symbol"])
            out.append(symbol.removeprefix("overflow.sdhq:"))
        return out

def compare(w:Witness,o:Sequence[Mapping[str,Any]],n:Sequence[Mapping[str,Any]],m:MappingModel)->int:
    prev_endings=0
    for i,(a,b) in enumerate(zip(o,n)):
        vals=b.get("variables",[])
        if not isinstance(vals,list) or len(vals)!=len(m.vars): fail(w.name,i,"VARS length",len(m.vars),len(vals) if isinstance(vals,list) else None)
        expected={
            "transition":m.transition_source_id(int(b["transition_index"])),
            "kind":"terminal" if int(b["status"])==3 else "advanced",
            "destination":m.node_scene(int(b["destination_node_index"])),
            "current_scene":m.node_scene(int(b["current_node_index"])),
            "route":int(vals[m.route]),"scene":int(vals[m.scene]),
            "choice_result":int(vals[m.choice]),"callback34":int(vals[m.cb34]),
            "endings":m.ending_codes([int(x) for x in b.get("endings",[])]),
            "callbacks":m.hook_names([int(x) for x in b.get("hooks",[])]),
        }
        expected["ending_id"]=expected["endings"][-1] if len(expected["endings"])>prev_endings else -1
        prev_endings=len(expected["endings"])
        for k,v in expected.items():
            if a.get(k)!=v: fail(w.name,i,k,a.get(k),v)
        for vi,row in enumerate(m.vars):
            symbol=meta(row,"source_symbol"); storage=meta(row,"source_storage")
            if not isinstance(symbol,str) or storage not in {"session","global"}: continue
            amap=a.get(storage,{})
            if not isinstance(amap,Mapping): fail(w.name,i,storage,"mapping",type(amap).__name__)
            av=int(amap.get(symbol,0)); nv=int(vals[vi])
            if av!=nv: fail(w.name,i,f"{storage} {symbol}",av,nv)
    return len(w.steps)

def main(argv:Sequence[str]|None=None)->int:
    p=argparse.ArgumentParser()
    p.add_argument("--ir",type=Path,required=True); p.add_argument("--oracle-exe",type=Path,required=True)
    p.add_argument("--native-exe",type=Path,required=True); p.add_argument("--artifact",type=Path,required=True)
    p.add_argument("--witness-source",type=Path,default=Path("tests/SchoolDaysFullRouterTest.cpp")); a=p.parse_args(argv)
    for x in (a.ir,a.oracle_exe,a.native_exe,a.artifact,a.witness_source):
        if not x.resolve().exists(): raise SystemExit(f"required path not found: {x.resolve()}")
    doc=json.loads(a.ir.read_text(encoding="utf-8-sig")); model=MappingModel(doc); total=0
    for w in witnesses(a.witness_source.resolve()):
        total+=compare(w,run(a.oracle_exe.resolve(),w),run(a.native_exe.resolve(),w,a.artifact.resolve()),model)
        print(f"PASS {w.name}: {len(w.steps)} step(s)")
    print(f"\n=== KTRF native / School Days oracle differential ===\nwitnesses=22\nsteps={total}\ndivergences=0\nNATIVE DIFFERENTIAL PASS")
    return 0
if __name__=="__main__": raise SystemExit(main())
