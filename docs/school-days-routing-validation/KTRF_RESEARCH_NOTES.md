# KTRF Research Notes

Provisional name: `KTRF — Kotonoha Routing Format`.

Provisional extension: `.ktroute`.

Provisional magic: `KTRF`.

## Rule

KTRF must not replace the current generated C++ router until the current router has been frozen and certified by this validation lab.

## Intended pipeline

    recovered route model
        |
        +--> generated C++ router   (current oracle)
        |
        +--> ktroute compiler
                 |
                 v
          schooldays.ktroute

## Planned sections

- header/version
- section directory
- string table
- nodes / SceneKeys
- transitions
- conditions
- effects
- choice / feeling handlers
- ending metadata
- optional debug/source metadata
- integrity checks

## Semantic requirement

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

Until then, KTRF is research and the generated C++ router remains the executable oracle.
