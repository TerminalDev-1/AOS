# How the developer felt on discovering that operating systems can use languages other than assembly

During work on AOS, the developer learned that its bootloader, kernel,
desktop and built-in applications were mostly written in Rust. He had
previously thought that an operating system needed to be written mainly
in assembly.

His first reaction was surprise and frustration. He asked which parts used
Rust and said the discovery made him feel dumb, even though he considered
himself a seasoned vibe coder. He also asked whether using Rust for an
operating system was legitimate.

We clarified that CPUs execute machine instructions. Assembly and compiled
languages such as Rust and C can all produce those instructions. Rust can
run without a host operating system, and AOS compiles its Rust source into
native x86-64 code. Assembly remains useful for interrupt entry points,
register handling, hardware access and execution transitions. AOS already
uses it for some of those tasks.

The clarification was that this was a new piece of knowledge, not a reason
to judge his ability. His association between operating systems and assembly
was understandable; the missing detail was that an OS can combine languages.
The Mac-like appearance was a desktop design choice, independent of Rust.

After the explanation, he decided that a complete assembly rewrite was not
needed and asked to continue the Windows-style desktop work while retaining
Rust. This note records his stated reaction and decision; it does not infer
any further feelings or conclusions.
