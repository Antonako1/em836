# CLI Invocation, File Ingestion & Virtual Terminal Commands — em836

**em836** supports four ways to ingest, compile, and debug x86 programs:
1. **Desktop Command-Line Invocation** (`em836.exe <file>`)
2. **Window Drag & Drop**
3. **In-GUI Code Editor & Examples Dropdown**
4. **Virtual Terminal Interactive CLI Commands**

---

## 1. Desktop Command-Line Invocation

Pass any supported source or binary file as the first argument when launching `em836.exe`:

```powershell
# Launch interactive debugger with default example (examples/graphics_demo.as) in the editor
.\out\build\x64-debug\em836.exe

# Assemble and immediately run an AstraC Assembly (.as) program
.\out\build\x64-debug\em836.exe examples/graphics_demo.as

# Compile and immediately run an Astra C (.ac) program
.\out\build\x64-debug\em836.exe examples/astrac_c_demo.ac

# Load and run a pre-assembled raw binary (.bin) or ACFH executable (.exe)
.\out\build\x64-debug\em836.exe examples/graphics_demo.BIN
```

### Supported File Formats

| Extension | Type | Ingestion Pipeline |
| :--- | :--- | :--- |
| `.as`, `.asm` | [AstraC Assembly](https://antonako.net/astrac/docs/as.php) | Assembled via `AstraC.exe asm <file>`, loaded into RAM at `0x0`, and opened in the Code Editor |
| `.ac` | [Astra C Source](https://antonako.net/astrac/docs/ac.php) | Compiled via `AstraC.exe comp <file>`, loaded into RAM at `0x0`, and opened in the Code Editor |
| `.bin`, `.BIN` | Raw Flat Binary | Loaded directly at `0x00000000` with `EIP = 0x00000000` (unless ACFH magic is present) |
| `.exe` | [ACFH Executable](https://antonako.net/astrac/docs/fileheader.php) | Parses 108-byte `AC_FILE_HEADER`, loads payload from `code_offset`, sets `EIP = entry_point_offset` |

---

## 2. Drag & Drop Ingestion

Drop any `.as`, `.asm`, `.ac`, `.bin`, or `.exe` file from Windows Explorer directly onto the **em836** window:
1. If the file is `.as`, `.asm`, or `.ac`, its source text is loaded into the **Code Editor** and invoked through `AstraC.exe`.
2. Unicorn's Translation Block (TB) and TLB caches are invalidated (`ReinitUnicorn`, `uc_ctl_flush_tb`, `uc_ctl_flush_tlb`) and guest RAM is cleared.
3. The new binary payload is loaded into RAM and the **Disassembly** window automatically centers on the new `EIP`.

---

## 3. Virtual Terminal Interactive CLI Commands

The **Console & Output $\to$ Virtual Terminal** input bar serves two purposes:
- **Emulator Control Commands**: Recognized CLI commands control the VM directly.
- **Guest Standard Input (`SYS_READ_CHAR`)**: Any unrecognized text is pushed (followed by `\n`) into the thread-safe guest input FIFO queue for [`SYS_READ_CHAR` (`0x12`)](SYSCALLS.md#sys_read_char-0x12).

### Built-in Console Commands

| Command | Syntax / Arguments | Description |
| :--- | :--- | :--- |
| `help` | `help` | Prints the list of available Virtual Terminal commands. |
| `run` | `run` | Starts or resumes asynchronous CPU execution (resets & restarts if Halted). |
| `pause` | `pause` | Pauses CPU execution immediately. |
| `step` | `step` | Executes a single x86 instruction and refreshes register/disassembly snapshots. |
| `reset` | `reset` | Resets CPU registers, zeroes RAM, restores the cached binary payload, and flushes JIT caches. |
| `cls` / `clear` | `cls` | Clears both the Virtual Terminal log and the VGA display screen. |
| `load` | `load <filepath>` | Compiles (if `.as`/`.ac`) and loads `<filepath>` into guest RAM. |
| `speed` | `speed <hz>` | Sets CPU clock frequency in Hz (`0` = Unlimited, `10`, `100`, `1000`, `10000`, `100000`, `1000000`). |
| `mode` | `mode <text\|gfx>` | Switches the VGA display between 80×25 Text Mode (`text`) and 640×480 Graphics Mode (`gfx`). |
| `mem` | `mem <hex_addr>` | Dumps 16 bytes of guest physical RAM at `<hex_addr>` in hexadecimal. |
| `reg` | `reg` | Prints a formatted snapshot of `EAX`–`EDI`, `EIP`, `ESP`, `EBP`, and `EFLAGS`. |
| `bp` | `bp <hex_addr>` | Toggles an execution breakpoint at `<hex_addr>`. |

### Console Command Examples
```text
> load examples/heap_sbrk_demo.as
> bp 00000018
> run
> reg
> mem 00200000
> step
```
