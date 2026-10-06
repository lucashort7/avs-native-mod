# AVS native GDScript dispatcher candidate

Historical static-analysis note. Its no-injection statements describe that investigation stage. A later approved runtime capture is documented in ../validation/first-runtime-capture.md. The entry proved hookable; symbol identity and game object layouts remain unconfirmed.

## Scope and method

Read-only inspection of the installed AVS03Pro.exe using its PE section table, exception directory, RIP-relative LEA references and objdump disassembly. Compared the observed control flow with the upstream Godot gdscript_vm.cpp and gdscript_function.h retrieved during the investigation. The upstream master revision is a comparison source, not a verified match to the exact AVS engine build. No debugger attach, DLL injection or process memory write occurred.

## Native evidence

- Preferred PE image base: 0x140000000. This is not a measured runtime module base.
- The executable contains modules/gdscript/gdscript_vm.cpp at RVA 0x5112BD0.
- Three decoded RIP-relative LEA instructions reference that exact string: RVAs 0x58A87B, 0x58A901 and 0x58B878. Their exception-directory ranges are 0x589FD0..0x58A94F and 0x58AD20..0x58BD28. Proximity is supporting context, not sufficient function identification.
- The historical copy-caller RVA 0x590F28 falls within the current executable's exception-directory function range 0x58C370..0x59BBDC. This does not prove that the historical capture used the identical executable build.
- At RVA 0x58C370 the prologue saves registers and anchors RBP at RSP+0x80. This agrees with the earlier observation that RBP belongs to a stack frame, not a run owner.
- The function tests a pointer at incoming RDX+0x2D8, sets a call-error output to zero, increments a thread-local counter and checks a 0x800 recursion threshold.
- Its optional-state branch reads stack, instruction pointer, line, instance and default-argument-like fields, then clears the state stack-size field. Its other branch compares argument counts and allocates a stack containing 0x18-sized value slots.
- Upstream GDScriptFunction::call exhibits the same sequence: code pointer check, successful call-error initialization, thread-local call-depth guard, resumed CallState restoration with stack ownership handling, or argument validation and new VM stack allocation.

## Interpretation

RVA 0x58C370 is a strong static candidate for GDScriptFunction::call, and it contains the previously captured copy caller. This is a structural source-to-binary match, not symbol or runtime confirmation. Do not yet install a detour or assume that RCX is the function-object pointer: the Variant return can introduce a hidden return-storage argument, and incoming RDX is the object-like pointer actually used in the observed prologue.

## Remaining proof

Confirm the runtime module base and executable identity, validate the candidate entry with a controlled debugger capture, identify the GDScriptFunction name/source and instance representations, and verify the full native ABI. Only then filter a native hook by the intended script and method. Account for resumed await states and for signals emitted before a transition completes.
