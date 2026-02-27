# jung

**A programming language for the collective unconscious.**

Jung is a bytecode VM language built on Jungian archetypes. Based on the jot C99 engine, it maps programming concepts to analytical psychology. Variables become symbols. Functions become dreams. The entry point is individuation.

## Keywords

| Jung | Traditional | Concept |
|------|------------|---------|
| `archetype` | `class` | Define a pattern |
| `shadow` | `private` | Hidden state |
| `persona` | `public` | Exposed interface |
| `anima` | `async` | The other within |
| `animus` | `await` | Integration point |
| `collective` | `static` | Shared across all |
| `individuation` | `main` | Entry point -- the journey begins |
| `project` | `print` | Express to the outside |
| `repress` | `delete` | Push to the unconscious |
| `integrate` | `import` | Absorb external wisdom |
| `complex` | `struct` | A cluster of associations |
| `dream` | `function` | A message from the unconscious |
| `manifest` | `return` | Make conscious |
| `unconscious` | `null` | The unknown |
| `Self` | `this` | The unified whole |

## Architecture

Bytecode VM with mark-sweep GC. Compiles Jung source to bytecode, executes on a stack-based VM. ~3000 LOC of C99, zero dependencies.

## Build

```
make && ./jung examples/hello.jung
```

## Example

```
// hello.jung

dream greet(name) {
    project "Hello, " + name;
}

greet("World");
project "The journey begins.";
```

## Status

Phase 1: Scaffold. Keywords mapped but not yet wired. The individuation process takes time.

---

*"Until you make the unconscious conscious, it will direct your life and you will call it fate."* -- Carl Jung
