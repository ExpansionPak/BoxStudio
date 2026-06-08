# Decomp Modules

Decomp-specific code belongs under this folder so editor, renderer, and platform
code do not need to know every game-specific rule.

Current layout:

```text
decomps/
  sm64/
    hackersm64/     HackerSM64 workspace validation and level-script rules.
```

When adding another decomp base, keep its project detection, asset layout,
script-writing compatibility checks, and game-specific model rules in its own
subfolder. Shared editor systems should call a narrow interface from here rather
than reaching into platform code.
