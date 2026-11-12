# PM — C/C++ project maker and `libpm`

PM is a small C99 command-line project generator with a reusable static
library. It creates C or C++ projects with Make, CMake, POSIX shell,
`configure`, or Autotools build files. It uses the system C library and the
normal system compiler drivers: `cc` for C and `c++` for C++.

## 1. Build and install PM

Requirements: a POSIX-like system, a C99 compiler, and `make`. Optional
project build systems (`cmake`, `autoreconf`) are only needed for projects
generated with those backends.

```sh
# Get the source, then enter it
git clone https://github.com/krzysztofMarciniak/pm

cd pm

# Build the CLI and static library
make

# Run the test suite
make test

# Install system-wide (default prefix: /usr/local)
sudo make install

# Or install just for your user
make install PREFIX="$HOME/.local"

# Remove an installation
sudo make uninstall
```

The build creates `build/pm` and `build/libpm.a`. Installation places the
command in `PREFIX/bin`, the library in `PREFIX/lib`, the public header in
`PREFIX/include/pm`, the built-in pack in `PREFIX/share/pm`, and pkg-config
metadata in `PREFIX/lib/pkgconfig/pm.pc`. Set `DESTDIR` to stage a package:

```sh
make install DESTDIR="$PWD/pkgroot" PREFIX=/usr
```

Add the chosen `bin` directory to `PATH` if it is not already there:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

## 2. Use the `pm` command

Run `pm` interactively to answer the project questions. For repeatable builds,
pass options and `--yes` (`-y`) to accept defaults for everything omitted.

### Copy-and-run examples

```sh
# Simple C executable, Makefile backend
pm -y --name=hello-c --lang=c --std=c99 --kind=exe --build=make hello-c
cd hello-c && make && ./build/hello-c

# C library plus a tiny example executable
pm -y --name=mathkit --lang=c --kind=both --build=cmake mathkit
cd mathkit && cmake -S . -B build && cmake --build build
./build/mathkit

# C++17 executable with CMake
pm -y --name=hello-cpp --lang=c++ --std=c++17 --kind=exe --build=cmake hello-cpp
cd hello-cpp && cmake -S . -B build && cmake --build build
./build/hello-cpp

# Static library only, POSIX shell build (no make required)
pm -y --name=mylib --kind=lib --build=sh mylib
cd mylib && sh build.sh

# Choose a license, author, and test framework
pm -y --name=widget --author="Ada Lovelace" --license=Apache-2.0 \
  --test=Unity --kind=both --build=make widget
cd widget && make test

# Hand-written configure script, then build
pm -y --name=portable --build=configure portable
cd portable && ./configure && make

# Autotools project
pm -y --name=autoproject --build=autoconf autoproject
cd autoproject && ./bootstrap && ./configure && make
```

Useful discovery and inspection commands:

```sh
pm --help                 # all options
pm --version              # installed version
pm --list-compilers       # system C and C++ compiler drivers found
pm --list-templates       # built-in template section names
pm --dump-templates       # export the complete active pack
```

Main options:

| Option | Values / purpose |
|---|---|
| `--name` | Project name |
| `--author` | Copyright/license author |
| `--lang` | `c` or `c++` |
| `--std` | C: `c99`, `c11`, `c17`, `c23`; C++: `c++11`, `c++14`, `c++17`, `c++20`, `c++23` |
| `--kind` | `exe`, `lib`, or `both` |
| `--build` | `make`, `cmake`, `sh`, `configure`, or `autoconf` |
| `--cc` | Compiler command or executable path; defaults to `cc` / `c++` |
| `--license` | SPDX-style license ID listed below |
| `--test` | C: `none`, `Unity`, `cmocka`, `criterion`; C++: `none`, `Catch2`, `GoogleTest`, `doctest` |
| `--ci` / `--no-ci` | Include or omit the GitHub Actions workflow |
| `--lint` / `--no-lint` | Include or omit formatting/lint helper files |
| `--template FILE` | Load a custom template pack (same as `PM_TEMPLATE=FILE`) |

PM discovers `cc` for C and `c++` for C++. Other compatible compiler drivers
can be selected explicitly with `--cc=clang` or `--cc=/path/to/compiler`. A
compiler in `/usr/local/bin` takes precedence over `$PATH`; set `PM_PREFIX` to
change the prefix searched first.

### Licenses

Supported IDs: `MIT`, `Apache-2.0`, `GPL-2.0-only`, `GPL-3.0`,
`GPL-3.0-only`, `LGPL-2.1-only`, `LGPL-3.0-only`, `BSD-2-Clause`,
`BSD-3-Clause`, `ISC`, `MPL-2.0`, `CC0-1.0`, `Unlicense`, `Zlib`, and `None`.
`GPL-3.0` is retained as a compatibility alias; prefer `GPL-3.0-only` for new
projects. `None` omits the `LICENSE` file.

## 3. Use `libpm` from C

The public API is in `<pm/pm.h>`. `libpm` is installed as a static library
named `libpm.a`. Link it with the platform C runtime and include the install
prefix's include and library directories.

### Minimal working program

This example creates a project directory named `generated-hello`:

```c
#include <stdio.h>
#include <string.h>
#include <pm/pm.h>

int main(void)
{
    pm_ctx *pm = pm_new();
    pm_config cfg;
    int rc;

    if (!pm) {
        fputs("libpm: out of memory\n", stderr);
        return 1;
    }

    pm_config_default(&cfg);
    snprintf(cfg.name, sizeof cfg.name, "%s", "generated-hello");
    snprintf(cfg.author, sizeof cfg.author, "%s", "Example Author");
    snprintf(cfg.lang, sizeof cfg.lang, "%s", "c");
    snprintf(cfg.std, sizeof cfg.std, "%s", "c11");
    snprintf(cfg.kind, sizeof cfg.kind, "%s", "both");
    snprintf(cfg.build, sizeof cfg.build, "%s", "make");
    snprintf(cfg.license, sizeof cfg.license, "%s", "MIT");

    rc = pm_generate(pm, &cfg, "generated-hello");
    if (rc != 0)
        fprintf(stderr, "libpm: %s\n", pm_error(pm));

    pm_free(pm);
    return rc == 0 ? 0 : 1;
}
```

Compile it from the PM source checkout:

```sh
cc -std=c99 -Wall -Wextra -Iinclude example.c build/libpm.a -o example
./example
```

After installation under `/usr/local`, use:

```sh
cc -std=c99 -Wall -Wextra example.c \
  -I/usr/local/include -L/usr/local/lib -lpm -o example
```

If `pkg-config` can find `pm.pc`, the shorter form is:

```sh
cc example.c $(pkg-config --cflags --libs pm) -o example
```

For a user-local installation, set `PKG_CONFIG_PATH` accordingly:

```sh
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH"
```

### Core API workflow

- `pm_new()` creates a context, loads the built-in pack, and then loads the
  optional `PM_TEMPLATE` pack from the environment.
- `pm_config_default(&cfg)` fills sensible defaults.
- Set fields in `pm_config` (`name`, `lang`, `std`, `kind`, `build`, `license`,
  `author`, `cc`, and so on).
- `pm_config_apply(ctx, &cfg)` validates the configuration and fills derived
  template variables without writing files.
- `pm_generate(ctx, &cfg, "directory")` applies the config and writes the
  project files.
- `pm_error(ctx)` returns the most recent diagnostic. `pm_free(ctx)` releases
  the context.

You can also work with templates directly: `pm_set()` sets a substitution
variable, `pm_get()` reads it, `pm_expand()` expands a string (the returned
buffer is owned by the caller and must be `free()`d), `pm_render()` writes one
named template to a path, and `pm_dump_pack()` writes the active pack to a
`FILE *`.

### Template pack: override an existing file

A pack is a UTF-8 text file made of named sections. Each section starts with
`%%% ` followed by the template name. The section body continues until the
next section header. Names correspond to generated project paths, such as
`main.c`, `Makefile`, `README.md`, or `gitignore`.

Start with a complete copy of the built-in pack, then edit only the sections
you want to replace. Existing pack sections not repeated in your custom pack
remain unchanged:

```sh
# Export the built-in pack to a file
pm --dump-templates > my.pack

# Edit the main.c section in my.pack, then generate with it
$EDITOR my.pack
PM_TEMPLATE="$PWD/my.pack" pm -y --name=custom-app custom-app
```

Or keep one replacement per file in a directory. Directory packs override
known built-in template names:

```sh
mkdir -p my-pack
cat > my-pack/main.c <<'EOF'
#include <stdio.h>
int main(void)
{
    puts("Hello from my custom pack!");
    return 0;
}
EOF

PM_TEMPLATE="$PWD/my-pack" pm -y --name=custom-app custom-app
```

### Add a brand-new generated file

To add a new output path (rather than replace an existing built-in template),
use a pack file and add a new section. `pm_render()` can render any section
name loaded from a pack; `pm_generate()` emits the standard known project files.
For the CLI generator to emit a new path automatically, add the section name
to the generator's output list in `src/gen.c` as well.

Example: add a `config/example.ini` file to the generated project:

```sh
cat > extra.pack <<'EOF'
%%% config/example.ini
[app]
name = @@NAME@@
version = 1.0
EOF
```

Loading an extra section does not make the CLI emit it automatically: the CLI
writes its known output list. To render the additional section from an
application using `libpm`:

```c
if (pm_load_pack(ctx, "extra.pack") != 0) {
    fprintf(stderr, "load pack: %s\n", pm_error(ctx));
    return 1;
}
if (pm_set(ctx, "NAME", "sample") != 0 ||
    pm_render(ctx, "config/example.ini", "sample/config/example.ini", 0) != 0) {
    fprintf(stderr, "render pack: %s\n", pm_error(ctx));
    return 1;
}
```

For a distributable pack, keep the `.pack` file beside your application or
install it in your application's data directory, and pass its explicit path
to `pm_load_pack()` or `PM_TEMPLATE`. Pack files can define new section names;
directory packs are intended for overriding built-in names.

### Template syntax

- `@@NAME@@` substitutes a variable set by PM or by `pm_set()`.
- `@@if FEATURE@@ ... @@end@@` includes a block when `FEATURE` is non-empty.
- `@@else@@` selects the alternative branch.
- `@@if !FEATURE@@` tests for an empty/unset variable.
- Conditions can be nested.
- Unknown substitution tokens are left unchanged, which is useful for
  placeholders intended for a later tool.

## 4. Tests and troubleshooting

Run the full project test suite with:

```sh
make test
```

It runs public-header library unit tests and creates temporary C/C++ projects
across the supported build backends. It also checks install layouts, bundled
license output, test-framework wiring, custom pack overrides, missing-pack
errors, and dump/reload round trips. Optional backend tests are skipped when
the corresponding external tool is not installed.

If a generated build cannot find `cc` or `c++`, install your system's C/C++
build toolchain and make sure the driver is on `PATH`. For example, on Alpine:

```sh
apk add build-base
```

On Debian/Ubuntu:

```sh
sudo apt install build-essential
```

## 5. Old Version
(C-CPP-template-repository)[https://github.com/KrzysztofMarciniak/C-CPP-template-repository]
