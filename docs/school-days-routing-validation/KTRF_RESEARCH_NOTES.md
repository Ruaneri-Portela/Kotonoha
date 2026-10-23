# KTRF Research Notes

> Historical note: this file records the pre-freeze KTRF planning stage.
> The active specification now lives under `docs/ktrf/` on the KTRF design branch.
> The old provisional extension `.ktroute` is superseded by `.ktnroute`.

Provisional name: `KTRF — Kotonoha Routing Format`.

Current extension: `.ktnroute`.

Provisional binary magic: `KTRF`.

## Rule

KTRF must not replace the certified generated C++ router until differential verification against the frozen School Days oracle produces zero semantic divergences.

## Intended pipeline

    recovered route model
        |
        +--> generated C++ router   (frozen oracle)
        |
        +--> canonical Routing IR
                 |
                 +--> semantic validator
                 |
                 +--> future KTRF compiler
                          |
                          v
                 school-days-hq.ktnroute

## Active specification

See:

- `docs/ktrf/README.md`
- `docs/ktrf/SPECIFICATION.md`
- `docs/ktrf/DATA_MODEL.md`
- `docs/ktrf/ROUTING_IR.md`
- `docs/ktrf/VERSIONING_EXTENSIONS.md`
- `schemas/ktrf-routing-ir.schema.json`

## Semantic requirement retained from research

The format must preserve delayed routing:

    SetSELECT
      -> choice result
      -> feeling/state effect
      -> timeline continues
      -> Next
      -> route predicate evaluation
      -> transition

A choice handler and a route transition are not the same operation.

## Acceptance criterion

KTRF is accepted only after differential verification against the certified generated C++ router produces zero semantic divergences across the chosen exhaustive state space.

Until then, the generated C++ router remains the executable School Days oracle.
