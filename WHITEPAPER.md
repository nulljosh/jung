# Jung: A Programming Language for the Collective Unconscious

**Joshua Trommel**
February 2026

---

## Abstract

Jung is a dynamically typed, tree-walking interpreted programming language implemented in approximately 4,100 lines of C99 with zero external dependencies. Its defining characteristic is a complete keyword vocabulary derived from Carl Jung's analytical psychology: variables are declared with `perceive`, functions are defined with `dream`, classes are declared with `archetype`, and error handling uses `confront`/`embrace`/`reject`. Every Jungian keyword has a standard alias (`let`, `fn`, `class`, `try`/`catch`/`throw`), so the two vocabularies are interchangeable within a single source file.

The project demonstrates that a coherent metaphor can serve as the organizing principle for an entire language's surface syntax without sacrificing semantic clarity or implementation tractability. The resulting language is not a novelty act: it ships a full expression system, closures, first-class functions, reference-counting values, a class system with method dispatch, string interpolation, file I/O, and a standard library of array and string operations.

Jung belongs to the same educational interpreter tradition as Robert Nystrom's Lox and Thorsten Ball's Monkey. Like those languages, it is small enough to read completely in an afternoon and substantial enough to write real programs in. Unlike those languages, it uses its keyword vocabulary as an argument about programming languages as cultural artifacts rather than purely technical specifications.

---

## 1. Introduction

Programming languages are cultural objects. The names a language chooses for its constructs encode assumptions about what computation is, who performs it, and what frame of mind is appropriate for the work. `let` implies permission. `var` implies variability. `def` implies definition. These are not neutral choices: they shape how programmers think about what their code is doing.

Most language designers ignore this dimension entirely, defaulting to ALGOL-era vocabulary (`if`, `while`, `for`, `return`) that has been copied so many times it carries no meaning beyond syntactic function. The handful of languages that have deviated from convention -- Logo's `to`, Inform 7's English prose, HyperTalk's near-natural-language syntax -- demonstrate that keyword choice can meaningfully alter the programmer's relationship to the code. They also demonstrate that the idea rarely survives contact with professional software development, where convention wins.

Jung takes a different position. It treats the keyword vocabulary as an opportunity to embed a consistent interpretive framework in the language itself, without changing the underlying semantics. The framework is Carl Jung's analytical psychology, which turns out to map onto computation with surprising fidelity.

The core correspondences are intuitive once stated. A variable is something a program has become aware of -- `perceive`. A function is a generative process that produces something from implicit state -- `dream`. A class is a universal pattern that specific instances instantiate -- `archetype`. Returning a value from a function is making something explicit that was previously implicit -- `manifest`. Printing to standard output is expressing an inner state externally -- `project`. Null is the unknown, the not-yet-conscious -- `unconscious`. Error handling maps almost perfectly: `confront` (try) is the act of engaging with something difficult, `embrace` (catch) is integrating what you find, and `reject` (throw) is the refusal to integrate, escalating the conflict upward.

These are not arbitrary word substitutions. Each Jungian term carries conceptual weight that illuminates the programming construct it names. A `dream` in Jungian psychology is not random noise but a message from the unconscious that requires interpretation -- a function call is precisely the invocation of a named process that transforms inputs into an output through logic you may not fully hold in mind at the moment of calling. An `archetype` is a universal pattern that manifests in specific forms across cultures and individuals -- a class is a template that manifests in specific object instances. The metaphor is load-bearing.

This paper describes the design and implementation of Jung v1.0.0.

---

## 2. Language Design

### 2.1 Keyword Philosophy

Jung maintains a complete dual-vocabulary system. Every Jungian keyword has a standard alias; both work in any context and can be mixed freely within a file. This design choice was deliberate: the language should be usable by someone who reads no documentation, and it should not force the user to choose an allegiance.

The complete mapping with rationale:

| Jungian | Standard | Semantic Rationale |
|---------|----------|-------------------|
| `perceive` | `let` | Consciousness recognizes something that was previously implicit. Variable declaration is the act of bringing a value into named awareness. |
| `dream` | `fn` | In Jungian psychology, dreams are the unconscious producing structured meaning. A function is a named process that generates output from internal logic. `individuation` is a second alias for `dream` -- the overarching journey of becoming, appropriate for a program's main entry function. |
| `archetype` | `class` | An archetype is a universal template instantiated in culturally specific forms. A class is a universal template instantiated in specific objects. The correspondence is exact. |
| `emerge` | `new` | Objects do not appear fully formed; they emerge from the archetype through the constructor. |
| `Self` | `this` | In Jungian psychology, the Self is the unified totality of the psyche -- the organizing center. Using `Self` rather than the conventional `this` (or Pythonic `self`) emphasizes that an object's identity is a coherent whole, not merely a bag of properties. |
| `project` | `print` | Psychological projection is the act of externalizing an inner state -- attributing to the outer world something that originates internally. Printing is exactly this: rendering internal state into external output. |
| `manifest` | `return` | To manifest something is to make it real and visible. A return statement makes the function's result explicit to the caller. |
| `unconscious` | `null` | The unconscious in Jungian thought is not nothing but the not-yet-known. Null is not an error state but an absence of explicit value -- something that exists but has not been named. |
| `integrate` | `import` | Integration is the process of absorbing and assimilating external material into the psyche. Importing a module absorbs external definitions into the current scope. |
| `confront` | `try` | To confront something is to face it directly, acknowledging that it may resist. A try block is the decision to engage with code that might fail. |
| `embrace` | `catch` | To embrace is to accept and hold. A catch block accepts the exception and holds it in a named variable, integrating it rather than letting it propagate. |
| `reject` | `throw` | Rejection is the refusal to integrate -- pushing something away rather than dealing with it. Throwing an exception is precisely this: refusing to handle an error condition locally and escalating it. |

The `individuation` alias for `fn` deserves additional comment. In Jungian psychology, individuation is the lifelong process of becoming a whole, integrated self. Using it as the keyword for function definition suggests that every function is a small act of becoming -- a unit of intentional transformation. It also reads well as a program entry point: `individuation main()` has a different weight than `fn main()`.

### 2.2 Type System

Jung is dynamically typed with five primitive value types and two composite types:

- **null** (`unconscious`) -- the absence of a value. Represented as a zero-sized type with no heap allocation.
- **bool** -- true/false. Stored as an integer field in the value union.
- **number** -- all numeric values are IEEE 754 doubles. The language applies integer division semantics when both operands have no fractional component: `10 / 3` returns `3`, not `3.3333`. This avoids the common beginner surprise of implicit float promotion while preserving exact floating-point arithmetic when either operand is fractional.
- **string** -- heap-allocated, null-terminated character arrays with an explicit length field. Strings are immutable; all operations that modify a string produce a new one. The `+` operator is overloaded for string concatenation and performs automatic coercion of non-string operands via `val_to_string`.
- **function** -- first-class values. User-defined functions and builtins are both representable as values and can be stored in variables, passed as arguments, and returned from functions.
- **array** -- dynamically resizable arrays backed by a heap-allocated `Value` array with a count and capacity. Arrays are reference types: assignment copies the reference, not the contents.
- **object** -- hash-table-backed key-value stores. Used for both user-constructed objects and class instances. Objects are reference types.

Coercion is minimal. The `+` operator triggers string coercion when either operand is a string. Truthiness evaluation treats `false`, `0`, `""`, `[]`, and `unconscious` as falsy; all other values are truthy. No other implicit coercions exist: adding a number to an object is a runtime error.

Type introspection is available via the `type()` builtin, which returns a string: `"null"`, `"bool"`, `"number"`, `"string"`, `"array"`, `"object"`, or `"function"`.

### 2.3 Control Flow

Jung implements the standard imperative control flow constructs: `if`/`else if`/`else`, `while`, and `for`-`in`. The `for`-`in` construct iterates over arrays and range objects:

```
for i in range(10) {
    project i
}

for item in myArray {
    project item
}
```

`break` and `continue` are supported within loops. Ternary expressions use the `?` / `:` operators: `x > 0 ? "positive" : "non-positive"`.

Short-circuit evaluation is implemented for `and` and `or`. The `and` operator returns `false` immediately if the left operand is falsy; `or` returns the left operand directly if it is truthy (not a boolean coercion of it). This matches the semantics of Python and JavaScript rather than strict boolean-only evaluation.

Error handling deserves its own section. The `confront`/`embrace`/`reject` (try/catch/throw) system supports:

- User-defined exceptions via `reject "message"` -- throw any string as an exception.
- Runtime errors inside a try block are automatically routed through the exception mechanism rather than terminating the process.
- The catch variable receives the exception message as a string.
- Nested try blocks work correctly: an exception thrown inside a catch block propagates to the enclosing try, not back to the same catch (a correctness subtlety discussed in section 3.4).

### 2.4 Object Model

Classes are declared with `archetype` (or `class`). A class body contains method definitions using `fn`. The constructor method is named `init` and is called automatically when the object is instantiated with `emerge` (or `new`).

```
archetype Persona {
    fn init(name, role) {
        Self.name = name
        Self.role = role
    }

    fn present() {
        project Self.name + " presents as: " + Self.role
    }
}

perceive mask = emerge Persona("Joshua", "Software Engineer")
mask.present()
```

Internally, a class is stored as an object value whose keys are method names and whose values are function values. When `emerge ClassName(args)` is evaluated, the interpreter looks up the class in a dedicated class table, creates a new object, sets the interpreter's `this_obj` pointer to that object, calls the `init` method if it exists, then returns the object. Method calls use the `__method_NAME` convention internally: `foo.bar()` is desugared by the parser to a call to `__method_bar` with the object as the first argument, which the interpreter resolves against the object's class.

Property access and assignment use dot notation. Compound assignment operators (`+=`, `-=`, `*=`, `/=`) work on both local variables and object properties.

Inheritance is not implemented. Jung has a single-level class system: archetypes do not extend other archetypes.

---

## 3. Implementation

### 3.1 Architecture

Jung is implemented as a classic three-stage pipeline:

```
Source text -> [Lexer] -> Token stream -> [Parser] -> AST -> [Interpreter] -> Output
```

Supporting modules handle values (`value.c`), hash tables (`table.c`), and the standard library (`builtins.c`). The entry point (`main.c`) handles file reading, REPL management, and command-line argument dispatch.

Line counts per module:

| Module | Lines |
|--------|-------|
| `interpreter.c` | 1069 |
| `parser.c` | 1025 |
| `builtins.c` | 672 |
| `value.c` | 293 |
| `lexer.c` | 398 |
| `table.c` | 142 |
| `main.c` | 113 |
| Headers | 447 |
| **Total** | **4159** |

The lexer and parser together account for roughly a third of the implementation; the interpreter and builtins account for the other two thirds. This distribution is typical for tree-walking interpreters where evaluation logic is substantially more complex than parsing.

The REPL (invoked with no arguments) initializes a persistent interpreter instance and loops over input lines. Single-expression statements are evaluated and their results printed if non-null; multi-statement inputs and statement nodes are executed without printing.

### 3.2 Memory Management

Jung uses a hybrid memory strategy. The `Value` struct carries a `refcount` field, but reference counting is not fully automated -- values must be explicitly freed with `val_free()` at the call sites that own them. This is closer to manual memory management with a reference-counted ownership discipline than to automatic garbage collection.

Each value type has a distinct free strategy:
- Null, bool, number: no heap allocation, `val_free` is a no-op.
- String: frees the character buffer.
- Array: recursively frees each element, then frees the backing array.
- Object: decrements the table's reference count; when it reaches zero, frees all entries and the table itself.
- Function: function definitions are owned by the interpreter's function table and are not freed when a function value is freed.

The `val_copy` function produces a deep copy of simple types (null, bool, number, string) and an aliased reference for aggregate types (array, object) -- meaning that object assignment is shallow. This is consistent with how JavaScript and Python handle object references.

### 3.3 Scoping

The interpreter maintains a scope stack implemented as a fixed-size array of `Scope` structs, each containing a `Table` (hash table). The stack depth is bounded at compile time by `MAX_SCOPES`. Variable lookup walks the stack from the current depth toward the root, then falls back to the globals table.

The `interp_set_var` function distinguishes between assignment to an existing variable (found anywhere in the scope chain) and definition of a new variable in the current scope. `interp_def_var` always defines in the current scope, regardless of whether a name already exists in an outer scope. Function parameters are defined with `interp_def_var` so they shadow outer variables correctly.

Closures are not implemented as explicit closure objects capturing a free-variable environment. Functions defined with `dream` capture a reference to the defining scope chain implicitly through the interpreter's scope stack at call time. This works for recursion and for functions that call other functions, but does not produce true lexical closures: a function returned from another function and called later will not retain access to the outer function's local variables after that function returns. This is a known limitation.

The `for`-`in` loop creates a new scope for the loop body. The loop variable is defined in that scope. `break` and `continue` set flag fields on the interpreter struct; the loop execution code checks these flags after each iteration.

### 3.4 Error Handling

Exception propagation uses `setjmp`/`longjmp`. The interpreter maintains a `try_stack` array of `jmp_buf` values and a `try_depth` counter. When a `confront` block is entered, the current `jmp_buf` is initialized with `setjmp` and `try_depth` is incremented. If `longjmp` fires (from `throw_exception`), execution resumes in the else branch of `setjmp`, which unwinds the scope stack to the saved depth and executes the `embrace` body.

One correctness subtlety required explicit handling: if an exception is thrown inside an `embrace` (catch) block, it must propagate to the enclosing try, not loop back to the same catch. This is handled by decrementing `try_depth` before executing the catch body, not after. The code comment at line 930 of `interpreter.c` documents this explicitly:

```c
/* Decrement try_depth BEFORE executing the catch body so that
 * any exception thrown from within the catch block propagates
 * to the enclosing try, not back to this same try (infinite loop). */
it->try_depth--;
```

Runtime errors (division by zero, undefined variable, type mismatches) are handled by `runtime_error()`, which checks `try_depth`. If the error occurs inside a try block, it is converted to an exception string and routed through `throw_exception`. If it occurs outside any try block, it prints to stderr and calls `exit(1)`.

Uncaught exceptions at the top level print the message to stderr and exit with status 1. There is no stack trace.

---

## 4. Examples

### 4.1 Hello World and Basic Functions

The canonical entry point demonstrates the Jungian vocabulary at its simplest:

```
dream greet(name) {
    project "Hello, " + name
}

greet("World")
project "The journey of individuation begins."
```

`dream` defines a named function. `project` outputs to stdout. The string concatenation uses `+` with automatic coercion. This reads almost as a short piece of prose: the program dreams a greeting into existence, greets the world, and announces the beginning of a journey.

### 4.2 Classes and Archetypes

The archetype example from `examples/archetypes.jung` demonstrates class definition and the dual-vocabulary system working together:

```
archetype Shadow {
    fn init(name) {
        Self.name = name
        Self.repressed = []
    }

    fn repress(memory) {
        Self.repressed.push(memory)
        project Self.name + " represses: " + memory
    }

    fn face() {
        project Self.name + " confronts " + str(len(Self.repressed)) + " repressed memories"
        for memory in Self.repressed {
            project "  - " + memory
        }
    }
}

perceive shadow = emerge Shadow("The Unconscious")
shadow.repress("childhood fear")
shadow.repress("unspoken anger")
shadow.face()
```

The class body uses standard `fn` for methods while the instantiation uses Jungian `perceive` and `emerge`. Both vocabularies coexist without conflict. The `Self.repressed` array grows dynamically via the `push` method. The `for`-`in` loop iterates the array directly.

### 4.3 Error Handling

The error handling example makes the `confront`/`embrace`/`reject` metaphor concrete:

```
dream divide(a, b) {
    if b == 0 {
        reject "Cannot divide by the void"
    }
    manifest a / b
}

confront {
    project divide(10, 2)
    project divide(10, 0)
} embrace (e) {
    project "Shadow encountered: " + e
}
```

`reject` throws a string exception. The `confront` block attempts both divisions; the second triggers the exception. The `embrace` block receives the message in `e` and handles it. Execution continues normally after the confront/embrace block -- the program does not terminate.

### 4.4 Recursive Functions and Data Structures

The `individuation.jung` example shows recursion, integer division, and the FizzBuzz variant that maps the Jungian vocabulary onto a classic algorithm:

```
dream fibonacci(n) {
    if n <= 1 {
        manifest n
    }
    manifest fibonacci(n - 1) + fibonacci(n - 2)
}

for i in range(10) {
    project "fib(" + str(i) + ") = " + str(fibonacci(i))
}
```

Recursion works because the interpreter looks up function names in the interpreter's function table, which is populated before any calls are made. `manifest` without an expression at the end of the recursive base case returns the number directly. The `range` builtin produces an iterable sequence.

---

## 5. Evaluation

### 5.1 What Works

The dual-vocabulary system works. Mixing Jungian and standard keywords in the same file produces no conflicts, and the interchangeability means a user can adopt as much of the metaphor as they find useful. The example programs are readable both as Jung programs and as programs in a conventional dynamically typed language.

The implementation is complete enough to write non-trivial programs. The class system, first-class functions, compound assignment, string interpolation, file I/O, and the standard library cover most of what a small scripting task requires. The test suite covers eight categories: basics, classes, control flow, errors, functions, Jungian keywords, arrays/objects, and builtins.

Integer division by default is a sensible choice for a scripting language aimed at problems where exact integer arithmetic matters and floating-point results are surprising. The implementation (checking `floor(l) == l && floor(r) == r` at the division site in `interpreter.c:238`) is correct and adds no meaningful overhead.

The REPL is functional. It initializes a persistent interpreter so that definitions made in one line are available in subsequent lines, which is the behavior users expect.

### 5.2 Limitations

Closures are not true lexical closures. A function that captures variables from an enclosing scope and is returned to be called later will not retain those variables. This is a significant omission for a language that wants to support functional programming patterns.

There is no inheritance. The class system provides a single-level template mechanism. An `archetype` cannot extend another `archetype`. Composition via object properties is possible but requires more boilerplate than inheritance would.

Error messages at the parse level exit immediately with a message but no recovery. The REPL cannot recover from parse errors in a single line -- the interpreter exits rather than printing an error and prompting again. This makes the REPL significantly less useful for exploratory programming.

There is no module system. The `integrate`/`import` keyword is defined syntactically but does not implement file-based module loading in v1.0.0. Programs cannot be split across files.

Performance characteristics are those of a tree-walking interpreter with no compilation, no bytecode, no register allocation, and no caching. Every expression evaluation traverses the AST node tree. For programs that do significant computation -- the Fibonacci example is slow for `n > 30` -- this is a real constraint.

### 5.3 Comparison to Similar Languages

Lox (from Crafting Interpreters) and Monkey (from Writing an Interpreter in Go) are the most direct comparisons. All three are dynamically typed tree-walking interpreters implemented as teaching vehicles.

Lox is more syntactically complete: it includes inheritance (`super`), and its error recovery is more robust. Monkey demonstrates a cleaner separation between parsing and evaluation via explicit AST types for every expression form. Jung is closer to feature parity with Lox while being implemented in C rather than Java or Go -- the manual memory management adds complexity that GC-backed implementations do not face.

The Jungian keyword system is Jung's distinguishing characteristic. It is not present in Lox or Monkey, and it is not purely cosmetic: it forces a rethinking of why each construct exists, which turns out to be a productive design exercise.

---

## 6. Future Work

**Bytecode VM.** The `CLAUDE.md` file describes jung as targeting a "Bytecode VM" -- this is the planned v2.0 architecture, derived from the jot language engine. A bytecode VM would address the performance limitations of tree-walking evaluation. The C99 inference engine developed for the jore project (`~/Documents/Code/jore`) demonstrates that a compact bytecode format with mmap-based loading is achievable in this codebase's style.

**True closures.** The most impactful semantic improvement would be capturing free variables at function definition time rather than relying on the interpreter's live scope stack. This requires a closure object type that holds a reference to a captured environment.

**Module system.** Implementing `integrate`/`import` as file-based module loading would make it possible to build programs across multiple files. A straightforward implementation would evaluate the imported file in a fresh scope and return its globals as an object.

**Inheritance.** Adding `extends` semantics to `archetype` would complete the class system. A Jungian keyword for inheritance is available: `individuation` could serve, given that the individuation process involves inheriting and integrating material from prior psychological stages.

**Pattern matching.** A `match` construct analogous to Rust's `match` or Haskell's case expressions would improve control flow expressiveness. A Jungian framing is possible: `interpret` could serve as the keyword, given that interpretation is the act of reading structure into raw material.

**Standard library expansion.** The current builtins cover arrays, strings, math, file I/O, and type introspection. A map/filter/reduce suite, regular expressions, and basic networking would make Jung more practically useful.

**Error recovery.** The parser should be able to synchronize after an error rather than exiting. This is primarily a quality-of-life improvement for REPL usage.

---

## 7. Conclusion

Jung demonstrates that a programming language's keyword vocabulary is a design dimension that deserves intentional consideration. The Jungian metaphors -- perceive, dream, archetype, manifest, project, confront, embrace, reject -- are not arbitrary replacements for their conventional equivalents. Each carries conceptual weight that illuminates the programming construct it names, and the system as a whole constitutes a coherent interpretive framework embedded in the language's surface syntax.

The implementation is complete enough to take seriously: 4,159 lines of C99, zero external dependencies, eight test suites, a functioning REPL, and a class system, closures-adjacent function model, and exception handling that handles nesting correctly. The known limitations -- no true closures, no inheritance, no module system, tree-walking performance -- are the expected limitations of a v1.0.0 teaching language, not architectural flaws.

The broader claim of the project is modest but defensible: programming languages are cultural artifacts, and the choice of names for constructs is not a neutral technical decision. Jung makes that claim explicit by building an entire language around a single coherent cultural framework and showing that the result is usable, readable, and internally consistent.

---

*"Until you make the unconscious conscious, it will direct your life and you will call it fate."*
-- Carl Jung

---

## References

- Nystrom, Robert. *Crafting Interpreters*. Genever Benning, 2021. https://craftinginterpreters.com/
- Ball, Thorsten. *Writing an Interpreter in Go*. Self-published, 2016.
- Jung, Carl G. *The Archetypes and the Collective Unconscious*. Collected Works Vol. 9i. Princeton University Press, 1969.
- Jung, Carl G. *Psychological Types*. Collected Works Vol. 6. Princeton University Press, 1971.
- ISO/IEC 9899:2011. *Programming Languages -- C*. International Organization for Standardization, 2011.
