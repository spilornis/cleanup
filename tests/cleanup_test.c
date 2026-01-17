#include <criterion/criterion.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/* Rename main and override readline/add_history/termios for test control. */
#define main cleanup_main
#define readline test_readline
#define add_history test_add_history
#define tcgetattr test_tcgetattr
#define tcsetattr test_tcsetattr

static const char *test_readline_value = NULL;
static int test_readline_calls = 0;

static char *test_readline(const char *prompt) {
  (void)prompt;
  if (test_readline_calls++ > 0 || !test_readline_value)
    return NULL;
  return strdup(test_readline_value);
}

static int test_add_history(const char *line) {
  (void)line;
  return 0;
}

static int test_tcgetattr(int fd, struct termios *termios_p) {
  (void)fd;
  memset(termios_p, 0, sizeof(*termios_p));
  return 0;
}

static int test_tcsetattr(int fd, int opt, const struct termios *termios_p) {
  (void)fd;
  (void)opt;
  (void)termios_p;
  return 0;
}

#include "../cleanup.c"

#undef main

static void write_file(const char *path) {
  FILE *f = fopen(path, "w");
  if (f) {
    fputs("x", f);
    fclose(f);
  }
}

Test(cleanup, prevdest_uses_directory_from_custom_path) {
  char tmp_template[] = "/tmp/cleanup-test-XXXXXX";
  char *base = mkdtemp(tmp_template);
  cr_assert_not_null(base);

  char destdir[PATH_MAX];
  snprintf(destdir, sizeof(destdir), "%s/dest", base);
  cr_assert_eq(mkdir(destdir, 0700), 0);

  char file1[PATH_MAX];
  char file2[PATH_MAX];
  snprintf(file1, sizeof(file1), "%s/file1.txt", base);
  snprintf(file2, sizeof(file2), "%s/file2.txt", base);
  write_file(file1);
  write_file(file2);

  last_target[0] = '\0';
  test_readline_value = destdir;
  test_readline_calls = 0;

  struct termios dummy = {0};
  ActionStatus moved = cleanup_move_to_custom_path(file1, &dummy);
  cr_assert_eq(moved, ACTION_NEXT_FILE);

  ActionStatus moved2 = cleanup_move_to_prev_dest(file2);
  cr_assert_eq(moved2, ACTION_NEXT_FILE);

  char file2_dest[PATH_MAX];
  snprintf(file2_dest, sizeof(file2_dest), "%s/file2.txt", destdir);
  cr_assert_eq(access(file2_dest, F_OK), 0);
}
