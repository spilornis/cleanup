Below is a small test‐suite that you can drop into your project and use to verify most of the behaviors of your cleanup(1) program.  We use two approaches:

  1. A set of BATS (Bash Automated Testing System) tests for exercising the *interactive CLI paths* (delete, ignore, move via config, usage, bad option, etc.).
  2. A tiny C “unit‐test” for the two path‐manipulation routines `to_absolute()` and `collapse_dot_slash()`.

––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––  
1) BATS tests (CLI)  
––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––  
Save this as `tests/cleanup.bats`.  You’ll need BATS installed (on macOS you can do `brew install bats-core`).

```bash
#!/usr/bin/env bats

load 'test_helper/bats-support/load'
load 'test_helper/bats-assert/load'

# Path to the compiled binary
CLEANUP=./cleanup

setup() {
  # create a temporary HOME so we do not clobber the real one
  export HOME="$BATS_TEST_DIRNAME/home"
  rm -rf "$HOME"
  mkdir -p "$HOME/.config"

  # Build a small config file under HOME
  cat > "$HOME/.config/cleanup.conf" <<EOF
# sample config
a: ${BATS_TEST_DIRNAME}/dirA
b: ${BATS_TEST_DIRNAME}/dirB
EOF

  # Create the target config dirs and some subdirs
  mkdir -p "${BATS_TEST_DIRNAME}/dirA/sub1"
  mkdir -p "${BATS_TEST_DIRNAME}/dirA/sub2"
  mkdir -p "${BATS_TEST_DIRNAME}/dirB"      # no subdirs

  # Create a working temp dir
  export WORKDIR="$BATS_TEST_DIRNAME/wd"
  rm -rf "$WORKDIR"
  mkdir -p "$WORKDIR"
  cd "$WORKDIR"
}

teardown() {
  # Clean up
  rm -rf "$HOME" "$WORKDIR"
}

@test "no arguments prints usage and exits 1" {
  run "$CLEANUP"
  assert_equal "$status" 1
  assert_output --partial "Usage: cleanup file1"
}

@test "delete option (2) actually removes the file" {
  touch foo.txt
  run bash -c "printf '2\n' | $CLEANUP foo.txt"
  assert_success
  [ ! -f foo.txt ]
}

@test "ignore option (4) keeps the file" {
  touch keep.me
  run bash -c "printf '4\n' | $CLEANUP keep.me"
  assert_success
  [ -f keep.me ]
}

@test "bad option prompts again and can quit" {
  touch x
  # first send '9' (bad), then send '1' (quit)
  run bash -c "printf '9\n1\n' | $CLEANUP x"
  assert_success
  # Should have printed "Bad option" at least once
  [[ "${output}" == *"Bad option"* ]]
}

@test "move via config key 'a', then choose subdir #2" {
  # create a file in WD
  echo hello > file1.txt
  # The sequence is:
  #   5   -> Move
  #   a   -> select config key 'a'
  #   2   -> subdir #2 (which is 'sub2')
  run bash -c "printf '5\na\n2\n' | $CLEANUP file1.txt"
  assert_success
  # file1.txt should now live in dirA/sub2/
  [ -f "${BATS_TEST_DIRNAME}/dirA/sub2/file1.txt" ]
}

@test "move via config key 'b' (no subdirs) does base dir" {
  echo data > f2.bin
  #   5 -> move
  #   b -> config key 'b'
  #   <enter> (no numbered subdirs -> empty list) => base
  run bash -c "printf '5\nb\n\n' | $CLEANUP f2.bin"
  assert_success
  [ -f "${BATS_TEST_DIRNAME}/dirB/f2.bin" ]
}
```

Make sure you give execute permission:

```bash
chmod +x tests/cleanup.bats
```

Then from your project root:

```bash
bats tests/cleanup.bats
```

––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––  
2) Small C unit‐test for to_absolute() and collapse_dot_slash()  
––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––  

Create `test_utils.c` alongside your `cleanup.c` (or in a `tests/` folder):

```c
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Declarations from cleanup.c */
extern char *to_absolute(const char *input);
extern void collapse_dot_slash(const char *src, char *dst);

int main(void) {
    char *r;
    char buf[PATH_MAX];

    /* Test collapse_dot_slash */
    collapse_dot_slash("/a/./b/./c", buf);
    assert(strcmp(buf, "/a/b/c") == 0);

    collapse_dot_slash("/./d/e", buf);
    /* note: collapse only works on "/./" not at start */
    assert(strcmp(buf, "/./d/e") == 0);

    /* For to_absolute tests we need a known cwd and HOME */
    /* set HOME to /tmp just for testing tilde expansion */
    setenv("HOME", "/tmp", 1);

    r = to_absolute("~/foo/bar");
    assert(r != NULL);
    /* must start with /tmp/foo/bar */
    assert(strstr(r, "/tmp/foo/bar") != NULL);
    free(r);

    /* Relative non‐existent: should still build an absolute path under cwd */
    /* getcwd() will be called inside */
    r = to_absolute("nonexistent/path");
    assert(r != NULL);
    /* it should *not* start with '~' */
    assert(r[0] == '/');
    free(r);

    printf("test_utils: all unit‐tests passed\n");
    return 0;
}
```

Compile it together with your `cleanup.c` (or at least link in the two functions you need):

```bash
gcc -std=c11 -I. cleanup.c test_utils.c -o test_utils
```

Then run:

```bash
./test_utils
```

You should see:

```
test_utils: all unit‐tests passed
```

––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––  

With these two suites in place you exercise:

• The “happy‐path” command‐line behaviors (delete, ignore, move via config, bad option, usage).  
• The directory‐listing & choose‐subdir logic (you selected a numbered subdir).  
• The two path‐manipulation functions in isolation.  

You can extend the BATS scripts to cover prev‐dest (6) or even the custom “7 – manual path” branch if you install an `expect`-style driver or stub out `readline()`. But the above should get you started on full‐coverage.
