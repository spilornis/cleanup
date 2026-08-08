#include <criterion/criterion.h>
#include <errno.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
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

/* Lets tests force rename() to fail with EXDEV, to exercise the
   Cross-Filesystem Move path (Move Queue) without needing two real
   filesystems. Captured before the `rename` macro below redirects calls
   made from within cleanup.c itself. */
static int force_exdev = 0;
static int real_rename(const char *oldpath, const char *newpath) {
  return rename(oldpath, newpath);
}
#define rename test_rename
static int test_rename(const char *oldpath, const char *newpath) {
  if (force_exdev) {
    errno = EXDEV;
    return -1;
  }
  return real_rename(oldpath, newpath);
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

Test(cleanup, rename_or_queue_local_move_is_immediate) {
  force_exdev = 0;

  char tmp_template[] = "/tmp/cleanup-test-XXXXXX";
  char *base = mkdtemp(tmp_template);
  cr_assert_not_null(base);

  char file[PATH_MAX];
  char dest[PATH_MAX];
  snprintf(file, sizeof(file), "%s/file.txt", base);
  snprintf(dest, sizeof(dest), "%s/moved.txt", base);
  write_file(file);

  MoveOutcome outcome = rename_or_queue(file, dest);

  cr_assert_eq(outcome, MOVE_LOCAL_OK);
  cr_assert_eq(access(dest, F_OK), 0);
  cr_assert_null(move_queue_head);
  cr_assert_eq(inflight_pid, -1);
}

Test(cleanup, rename_or_queue_backgrounds_cross_filesystem_move) {
  char tmp_template[] = "/tmp/cleanup-test-XXXXXX";
  char *base = mkdtemp(tmp_template);
  cr_assert_not_null(base);

  char file[PATH_MAX];
  char dest[PATH_MAX];
  snprintf(file, sizeof(file), "%s/file.txt", base);
  snprintf(dest, sizeof(dest), "%s/moved.txt", base);
  write_file(file);

  force_exdev = 1;
  MoveOutcome outcome = rename_or_queue(file, dest);
  force_exdev = 0;

  /* Queued, not yet moved: an EXDEV Move is optimistically advanced past
     before the background mv has necessarily completed. */
  cr_assert_eq(outcome, MOVE_QUEUED);

  move_queue_drain();

  cr_assert_eq(inflight_pid, -1);
  cr_assert_null(move_queue_head);
  cr_assert_eq(access(dest, F_OK), 0);
  cr_assert_neq(access(file, F_OK), 0); /* source is gone after the move */
}

Test(cleanup, move_queue_drains_multiple_moves_one_at_a_time) {
  force_exdev = 0;

  char tmp_template[] = "/tmp/cleanup-test-XXXXXX";
  char *base = mkdtemp(tmp_template);
  cr_assert_not_null(base);

  char file1[PATH_MAX], file2[PATH_MAX];
  char dest1[PATH_MAX], dest2[PATH_MAX];
  snprintf(file1, sizeof(file1), "%s/file1.txt", base);
  snprintf(file2, sizeof(file2), "%s/file2.txt", base);
  snprintf(dest1, sizeof(dest1), "%s/moved1.txt", base);
  snprintf(dest2, sizeof(dest2), "%s/moved2.txt", base);
  write_file(file1);
  write_file(file2);

  move_queue_push(file1, dest1);
  move_queue_push(file2, dest2);

  /* Only one Move may ever be In-Flight at a time. */
  move_queue_start_next();
  cr_assert_neq(inflight_pid, -1);
  cr_assert_not_null(move_queue_head);

  move_queue_drain();

  cr_assert_eq(inflight_pid, -1);
  cr_assert_null(move_queue_head);
  cr_assert_eq(access(dest1, F_OK), 0);
  cr_assert_eq(access(dest2, F_OK), 0);
}
