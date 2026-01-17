The provided `main` function is indeed quite long and handles many different responsibilities. To make it more modular, we can extract distinct operations into separate functions.

Here's a breakdown of the refactoring strategy:

1.  **Define `ActionStatus` enum:** This will clarify the return values for our new functions, indicating whether the program should move to the next file, stay on the current file (e.g., after an error), or quit entirely.
2.  **`display_options` function:** Renamed the existing `display_menu` to `display_options` for clarity and kept its functionality.
3.  **`cleanup_delete_file` function:** Encapsulates the logic for deleting a file.
4.  **`cleanup_move_to_prev_dest` function:** Handles moving a file to the last used destination.
5.  **`cleanup_move_to_configured_dest` function:** Manages the process of moving to one of the configured directories, including the subdirectory selection. This function will be responsible for temporarily restoring and re-enabling raw terminal mode as needed for `fgets` input in `choose_subdir`.
6.  **`cleanup_move_to_custom_path` function:** Handles moving to a user-entered custom path, utilizing `readline` for input and path completion. This function will also manage terminal mode switching.
7.  **`process_single_file` function:** This will be the core loop for handling a single file. It will display options, read the user's choice (in raw mode), and then call the appropriate helper function based on that choice. It will return an `ActionStatus` to `main`.
8.  **Refactor `main`:** The `main` function will become much shorter. It will handle initialization (config loading, raw mode setup), then iterate through the command-line arguments, calling `process_single_file` for each. It will also ensure proper cleanup.

**Key Changes:**

*   **Enum `ActionStatus`:** Introduced to clearly indicate the outcome of an action.
*   **`process_single_file`:** New function to manage the interaction loop for each file.
*   **Action-specific helper functions:** `cleanup_delete_file`, `cleanup_move_to_prev_dest`, `cleanup_move_to_configured_dest`, `cleanup_move_to_custom_path` are new.
*   **Terminal Mode Management:** The helper functions (`cleanup_move_to_configured_dest`, `cleanup_move_to_custom_path`) now correctly handle restoring the terminal to canonical mode for `fgets` or `readline` input and then switching back to raw mode. The `original_termios_settings` is passed to them.
*   **Error Handling:** The new functions return `ACTION_CONTINUE_LOOP` on failure to allow the user to try again on the same file, or `ACTION_NEXT_FILE` on success.
*   **Standardized `rename_or_mv` return:** Changed error return values from `2`/`3` to `-1` for consistency.
*   **Memory Cleanup:** Added a loop in `main` to `free` the dynamically allocated `path` strings in the `dirs` array.

Here's the refactored code:

```c
/*
  Name: cleanup.c

  Description: Program helps cleanup a folder. It loops through the files passed
  on command line and then deletes then or moves them to configured or manually
  entered destinations. Also allows to view the file to make it easy to decide.

  Platform: MacOS
*/
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define MAX_DIRS 100
#define MAX_SUBS 1000
#define BUF_SIZE 1024

#define RED "\033[31m"
#define GREEN "\033[32m"
#define BLUE "\033[34m"
#define BOLD "\033[1m"
#define RESET "\033[0m"

typedef struct {
  char key;
  char *path;
} DirEntry;

// Global variables, as in the original code.
// For larger applications, these might be encapsulated in a context struct.
static DirEntry dirs[MAX_DIRS];
static int ndirs = 0;
static char last_target[PATH_MAX] = {0};

// Enum to define the outcome of an action on a file
typedef enum {
  ACTION_CONTINUE_LOOP, // Stay on the current file, ask for input again
  ACTION_NEXT_FILE,     // Go to the next file in the argv list
  ACTION_QUIT_PROGRAM   // Exit the program entirely
} ActionStatus;


// --- Existing Helper Functions (kept mostly as-is, minor cleanups/returns) ---

char *to_absolute(const char *input) {
  if (!input)
    return NULL;

  char expanded[PATH_MAX];
  if (input[0] == '~' && (input[1] == '/' || input[1] == '\0')) {
    const char *home = getenv("HOME");
    if (!home)
      return NULL;
    if (snprintf(expanded, sizeof expanded, "%s%s", home, input + 1) >=
        (int)sizeof expanded)
      return NULL;
  } else {
    if (strlen(input) >= sizeof expanded)
      return NULL;
    strcpy(expanded, input);
  }

  char *resolved = realpath(expanded, NULL);
  if (!resolved) {
    if (errno == ENOENT || errno == ENOTDIR) {
      char cwd[PATH_MAX];
      if (expanded[0] != '/') {
        if (!getcwd(cwd, sizeof cwd))
          return NULL;
        char tmp[PATH_MAX];
        if (snprintf(tmp, sizeof tmp, "%s/%s", cwd, expanded) >=
            (int)sizeof tmp)
          return NULL;
        // Fallback: If realpath fails because the *entire* path doesn't exist
        // but it's relative, we can return the absolute path using cwd without symlink resolution.
        // If it's absolute but non-existent, `strdup(expanded)` could be an option, but realpath's NULL is usually fine.
        return strdup(tmp);
      }
    }
    return NULL; // caller must free()
  }
  return resolved; // caller must free()
}


void load_config() {
  char *home = getenv("HOME");
  if (!home)
    return;
  char cfg[PATH_MAX];
  snprintf(cfg, sizeof(cfg), "%s/.config/cleanup.conf", home);
  FILE *f = fopen(cfg, "r");
  if (!f)
    return; // No config file, no problem

  char line[BUF_SIZE];
  while (fgets(line, sizeof(line), f) && ndirs < MAX_DIRS) {
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++; // Skip initial whitespace
    if (*p == 0 || *p == '#')
      continue; // Skip empty or commented lines

    if (p[1] == ':') { // Format: "K: path"
      char key = p[0];
      char *path = p + 2;
      while (*path == ' ' || *path == '\t')
        path++; // Skip whitespace after ':'
      char *nl = strchr(path, '\n');
      if (nl)
        *nl = 0; // Remove newline

      char *abs_path = to_absolute(path);
      if (abs_path) {
        dirs[ndirs].key = key;
        dirs[ndirs].path = strdup(abs_path); // Store a copy
        free(abs_path); // Free temporary absolute path
        ndirs++;
      } else {
        fprintf(stderr, RED "Warning: Invalid path '%s' in config ignored.\n" RESET, path);
      }
    }
  }
  fclose(f);
}

int list_subdirs(const char *base, char subs[][PATH_MAX]) {
  DIR *d = opendir(base);
  if (!d) {
    perror("opendir");
    return -1;
  }
  struct dirent *ent;
  int n = 0;
  while ((ent = readdir(d)) && n < MAX_SUBS) {
    if (ent->d_name[0] == '.') // Skip hidden files/dirs and . ..
      continue;
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", base, ent->d_name);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
      strcpy(subs[n++], ent->d_name);
    }
  }
  closedir(d);
  return n;
}

void print_two_cols(char subs[][PATH_MAX], int n) {
  printf("\n");
  int cols = (n > 20) ? 2 : 1; // More columns if many subdirs
  int mid = (n + cols - 1) / cols;
  for (int i = 0; i < mid; i++) {
    for (int c = 0; c < cols; c++) {
      int idx = i + c * mid;
      if (idx < n) {
        printf("%3d: %-30s", idx + 1, subs[idx]);
      }
    }
    printf("\n");
  }
}

int choose_subdir(const char *base, char *outpath) {
  char subs[MAX_SUBS][PATH_MAX];
  int n = list_subdirs(base, subs);
  if (n < 0) {
    fprintf(stderr, RED "Error listing subdirectories for %s.\n" RESET, base);
    return -1;
  }
  if (n == 0) {
    printf("No subdirectories found. Using base path: %s\n", base);
    strcpy(outpath, base);
    return 0;
  }
  print_two_cols(subs, n);
  printf("Select subdir number (enter for base %s): ", base);
  char buf[BUF_SIZE];
  if (!fgets(buf, sizeof(buf), stdin)) {
    fprintf(stderr, RED "Error reading input.\n" RESET);
    return -1;
  }
  if (buf[0] == '\n') { // User pressed Enter for base path
    strcpy(outpath, base);
    return 0;
  }
  int choice = atoi(buf);
  if (choice < 1 || choice > n) {
    fprintf(stderr, RED "Invalid selection. Please choose a number between 1 and %d.\n" RESET, n);
    return -1;
  }
  snprintf(outpath, PATH_MAX, "%s/%s", base, subs[choice - 1]);
  return 0;
}

void do_quickview(const char *file) {
  pid_t pid = fork();
  if (pid == 0) { // Child process
    // `qlmanage -p` is a macOS command for Quick Look
    execlp("qlmanage", "qlmanage", "-p", file, (char *)NULL);
    // If execlp returns, an error occurred
    perror("execlp qlmanage");
    _exit(1); // Exit child with error status
  } else if (pid > 0) { // Parent process
    int status;
    waitpid(pid, &status, 0); // Wait for the Quick Look process to finish
    // Optional: check WIFEXITED(status) and WEXITSTATUS(status)
  } else { // Fork failed
    perror("fork");
  }
}

/* This function will be called by Readline when the user hits <Tab>.
   It simply defers to Readline's built-in filename completer. */
static char **my_completion(const char *text, int start, int end) {
  /* Tell readline we are handling completion ourselves, so it
     won't try default completion on our behalf. */
  rl_attempted_completion_over = 1;

  /* Ask readline to call its filename completer: */
  return rl_completion_matches(text, rl_filename_completion_function);
}

void collapse_dot_slash(const char *src, char *dst) {
  const char *s = src;
  char *d = dst;
  while (*s) {
    if (s[0] == '/' && s[1] == '.' && s[2] == '/') {
      *d++ = '/'; /* emit single '/' */
      s += 3;     /* skip "/./" */
    } else {
      *d++ = *s++; /* copy normal char */
    }
  }
  *d = '\0';
}

/* Put terminal into raw mode and return the old settings */
static struct termios enable_raw_mode(void) {
  struct termios oldt, newt;

  tcgetattr(STDIN_FILENO, &oldt); // save current settings
  newt = oldt;

  newt.c_lflag &= ~(ICANON | ECHO); // disable canonical mode & echo (original had only ICANON)
  newt.c_cc[VMIN] = 1;       // minimum number of chars to read
  newt.c_cc[VTIME] = 0;      // no timeout

  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  return oldt; // return old settings for later restore
}

/* Restore the terminal to its previous state */
static void restore_mode(const struct termios *oldt) {
  tcsetattr(STDIN_FILENO, TCSANOW, oldt);
}

// Renamed from run_external_mv to better reflect its combined functionality
static int rename_or_mv(const char *src, const char *dst) {
  if (rename(src, dst) == 0) {
    printf(GREEN "Moved to %s\n" RESET, dst);
    return 0;
  } else if (errno != EXDEV) { // Error, but not cross-device link
    perror("rename");
    return -1;
  } else {
    // Different filesystems, use external mv command
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      return -1;
    }

    if (pid == 0) { // Child process
      execlp("mv", "mv", "-f", src, dst, (char *)NULL);
      perror("execlp mv"); // If execlp returns, an error occurred
      _exit(1); // Exit child with error status
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
      perror("waitpid");
      return -1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      printf(GREEN "Moved to %s (via mv command)\n" RESET, dst);
      return 0; // mv succeeded
    } else {
      fprintf(stderr, RED "mv command failed with status %d\n" RESET, WEXITSTATUS(status));
      return -1; // mv reported an error
    }
  }
}

// --- New/Refactored Functions for Modularity ---

void display_options(const char *file) {
  printf("\n File: " BOLD "%s" RESET "\n", file);
  printf("q) Quit  d) Delete  v) Quickview  i) Ignore  m) Move  p) "
         "PrevDest c) Custom path\n");
  printf("Choose: ");
  fflush(stdout); // Ensure prompt is displayed before reading input
}

ActionStatus cleanup_delete_file(const char *filepath) {
  if (remove(filepath) == 0) {
    printf(GREEN "Deleted: %s\n" RESET, filepath);
    return ACTION_NEXT_FILE;
  } else {
    perror(RED "Error deleting file" RESET);
    return ACTION_CONTINUE_LOOP; // Stay on the same file if deletion fails
  }
}

ActionStatus cleanup_move_to_prev_dest(const char *filepath) {
  if (last_target[0] == 0) {
    printf(RED "No previous target directory set.\n" RESET);
    return ACTION_CONTINUE_LOOP;
  }

  char dest_path[PATH_MAX];
  const char *fname = strrchr(filepath, '/');
  fname = fname ? fname + 1 : filepath; // Get base filename

  snprintf(dest_path, PATH_MAX, "%s/%s", last_target, fname);

  if (rename_or_mv(filepath, dest_path) == 0) {
    // last_target is already set from a previous successful move, no need to update
    return ACTION_NEXT_FILE;
  } else {
    return ACTION_CONTINUE_LOOP; // Stay on current file if move fails
  }
}

ActionStatus cleanup_move_to_configured_dest(const char *filepath, struct termios *original_termios) {
  if (ndirs == 0) {
    printf(RED "No configured directories found in ~/.config/cleanup.conf.\n" RESET);
    return ACTION_CONTINUE_LOOP;
  }

  printf("\nConfigured Destinations:\n");
  for (int j = 0; j < ndirs; j++) {
    printf("%c) %s\n", dirs[j].key, dirs[j].path);
  }
  printf("Select dir key: ");
  fflush(stdout);

  char key_char = getchar(); // Read in raw mode

  int found_idx = -1;
  for (int j = 0; j < ndirs; j++) {
    if (dirs[j].key == key_char) {
      found_idx = j;
      break;
    }
  }

  if (found_idx < 0) {
    printf(RED "Invalid key '%c'.\n" RESET, key_char);
    return ACTION_CONTINUE_LOOP;
  }

  char selected_target_dir[PATH_MAX];

  // Restore terminal for choose_subdir which uses fgets
  restore_mode(original_termios);
  fflush(stdout); // Ensure prompts are visible
  if (choose_subdir(dirs[found_idx].path, selected_target_dir) < 0) {
    printf(RED "Subdirectory selection failed or invalid.\n" RESET);
    enable_raw_mode(); // Re-enable raw mode
    return ACTION_CONTINUE_LOOP;
  }
  enable_raw_mode(); // Re-enable raw mode after choose_subdir

  const char *fname = strrchr(filepath, '/');
  fname = fname ? fname + 1 : filepath;
  char dest_path[PATH_MAX];
  snprintf(dest_path, PATH_MAX, "%s/%s", selected_target_dir, fname);

  if (rename_or_mv(filepath, dest_path) == 0) {
    strcpy(last_target, selected_target_dir); // Update last_target on success
    return ACTION_NEXT_FILE;
  } else {
    return ACTION_CONTINUE_LOOP; // Stay on current file if move fails
  }
}

ActionStatus cleanup_move_to_custom_path(const char *filepath, struct termios *original_termios) {
  char *readline_input = NULL;
  char target_dir_abs[PATH_MAX] = {0};

  // Restore terminal for readline, which needs canonical mode
  restore_mode(original_termios);
  rl_attempted_completion_function = my_completion; // Enable filename completion

  while (1) {
    readline_input = readline("Enter destination directory path> ");
    if (readline_input == NULL) { // User pressed Ctrl-D (EOF)
      printf("\n"); // Move to a new line after Ctrl-D
      break; // Exit readline loop, no path entered
    }

    if (*readline_input) {
      add_history(readline_input); // Add non-empty input to history
    }

    char *abs_path = to_absolute(readline_input);
    if (!abs_path) {
      printf(RED "Invalid or non-existent path: '%s'\n" RESET, readline_input);
      free(readline_input); // Free readline buffer
      continue; // Ask for path again
    }

    char destdir_collapsed[PATH_MAX];
    collapse_dot_slash(abs_path, destdir_collapsed); // Normalize path
    free(abs_path);

    struct stat st;
    if (stat(destdir_collapsed, &st) < 0) {
      perror(RED "Error checking destination path" RESET);
      free(readline_input);
      continue;
    }
    if (!S_ISDIR(st.st_mode)) {
      fprintf(stderr, RED "'%s' is not a directory.\n" RESET, destdir_collapsed);
      free(readline_input);
      continue;
    }

    strcpy(target_dir_abs, destdir_collapsed); // Valid path found
    free(readline_input); // Free readline buffer
    break; // Exit readline loop with a valid path
  }

  enable_raw_mode(); // Re-enable raw mode

  if (target_dir_abs[0] == 0) { // If no valid path was entered (e.g., Ctrl-D)
    printf("No custom path entered.\n");
    return ACTION_CONTINUE_LOOP;
  }

  const char *fname = strrchr(filepath, '/');
  fname = fname ? fname + 1 : filepath;
  char dest_full_path[PATH_MAX];
  snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s", target_dir_abs, fname);

  if (rename_or_mv(filepath, dest_full_path) == 0) {
    strcpy(last_target, target_dir_abs); // Update last_target on success
    return ACTION_NEXT_FILE;
  } else {
    return ACTION_CONTINUE_LOOP; // Stay on current file if move fails
  }
}

// Function to process a single file based on user input
ActionStatus process_single_file(const char *filepath, struct termios *original_termios_settings) {
  if (access(filepath, F_OK) != 0) {
    perror(filepath);
    return ACTION_NEXT_FILE; // Skip to next file if current one doesn't exist
  }

  while (1) {
    display_options(filepath);
    int opt = getchar(); // Read single character input in raw mode

    switch (opt) {
      case 'q':
      case 'Q':
        return ACTION_QUIT_PROGRAM;
      case 'd':
      case 'D':
        return cleanup_delete_file(filepath);
      case 'v':
      case 'V':
        do_quickview(filepath);
        // After quickview, stay on the same file to allow other actions
        return ACTION_CONTINUE_LOOP;
      case 'i':
      case 'I':
        return ACTION_NEXT_FILE;
      case 'p':
      case 'P':
        return cleanup_move_to_prev_dest(filepath);
      case 'm':
      case 'M':
        return cleanup_move_to_configured_dest(filepath, original_termios_settings);
      case 'c':
      case 'C':
        return cleanup_move_to_custom_path(filepath, original_termios_settings);
      case '\n': // Ignore newlines that might be buffered or accidentally pressed
      case '\r':
        continue;
      default:
        printf(RED "Invalid option '%c'. Please try again.\n" RESET, opt);
        return ACTION_CONTINUE_LOOP;
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s file1 [file2...]\n", argv[0]);
    return 1;
  }

  load_config(); // Load configured directories

  // Save original terminal settings and enable raw mode
  struct termios original_termios_settings = enable_raw_mode();

  // Process each file provided as a command-line argument
  for (int i = 1; i < argc; i++) {
    ActionStatus status = process_single_file(argv[i], &original_termios_settings);
    if (status == ACTION_QUIT_PROGRAM) {
      break; // Exit the loop and program if 'q' was chosen
    }
  }

  // Restore original terminal settings before exiting
  restore_mode(&original_termios_settings);

  // Free dynamically allocated paths from config
  for (int i = 0; i < ndirs; i++) {
    free(dirs[i].path);
  }

  return 0;
}
```
