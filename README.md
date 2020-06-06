
## 6502 Assembler

* ACME like syntax
* Aims to be on par with Kickassembler in automation/scriptability,
  but with less complexity
* Unit tests through internal emulator
* Open and easily extendable source

See [INTRO.md](INTRO.md) for a tutorial / introduction.

### Invoking

`badass -i <source> -Dname=val ...`

### Example

```asm
    VRAM_LO = $9f00
    VRAM_HI = $9f01
    !macro SetVRAM(a) {
        .lo = a&0xff;
        .hi = a>>8;
        lda #.lo
        sta VRAM_LO
        !if .lo != .hi { lda #.hi }
        sta VRAM_HI
    }

    !section "main", $1000
start:

    ; Clear 1K
    ldx #0
-   !rept 4 { sta $1000+i*256,x }
    dex
    bne -

    SetVRAM(0x1234)
    ldx #end - text
-   lda start,x
    sta $1000,x
    dex
    bne -
    rts

text:
    !byte "Text to copy", 0
end:
```

See [asm/example.asm](asm/example.asm) for a full example.

### Labels

Labels either end with ':' or have to start at the first column.

A label starting with a dot ('.') is a _local_ label and treated
as _lastLabel.label_, where _lastLabel_ is the closest previous
_non local_ label.

A label can be also called '+' or '-'

Referencing '+' will mean the closest _following_ '+' label.
Referencing '-' will mean the closest _previous_ '-' label.

Repeated '-' or '+' signs means to skip further.

### Memory Layout

It is assumed that the normal use case is that you want to
produce a single "product" from a main source, that will include other
sources. There is no linking step since it is not very useful today.

The default mode then is to start with a _Target_ which defines the
available memory, assemble all source code into sections, where each
section can recide in a different part of the targets memory, and
finally write the entire image as a file.

Other common use cases is that some sections are written as separate
files, for manual loading by the main (startable) image.

This is compilcated a bit by the various funky layouts of different
targets, such as cartridge images.

### Unit Tests

Any statements inside a `!test` block is executed inside an internal
emulator.

Before running the test code, all sections are placed in the right
places in memory.

The code is then assembled at the current org/PC and excuted.
Results are saved in tests.<name>

The assembled changes are then rolled back.


### Meta commands

```
!section <name>, <address>

!byte <value> [, <value> ]*

!if <value> { <statements> }

!test <name> { <statements> }

!assert <expression> [, <message> ]
```

### Basic Operation in Detail

The source is parsed top to bottom. Included files are inserted
at the point of inclusion.

Symbols are defined through labels, symbol assignments or indirectly
through meta commands such as "!section" or "!test".

An assignment may not change an existing symbol.

If an expression references an undefined symbol, it is assumed to
be zero and flagged as "undefined".

If an existing symbol gets assigned a new value, it is also flagged
as "undefined", since we need to make another pass to make sure all
it's previous references are resolved to the correct value.
This happens when code size changes, as subsequent labels are moved
to new locations.

When all source code is parsed, the undefined list is checked;

  * If it is empty we are done.
  * If all entries now also exist in the symbol table, we clear
    the undefined list and make another pass.
  * Otherwise we have an error, and report the undefined symbols.

A _branch_ instruction to an undefined or incorrect label may
temporarily be too long. This error must be postponed until all
parsing is done.

Macros use normal symbols as arguments. These symbols are only set
during macro evaluation. If a macro argument "shadows" a global
symbol a warning is issue.

Macros affect the symbol table, so you can set symbols from macros.
If you don't want to pollute the symbol table, used "."-symbols, they
will be local to the macro.

