/*
  Name: cleanup.c

  Description: Program helps cleanup a folder. It loops through the files passed
  on command line and then deletes then or moves them to configured or manually
  entered destinations. Also allows to view the file to make it easy to decide.

  By Rajgopal Patil

  Platform: MacOS
  Date: 16 Jan 2026
*/
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define MAX_DIRS 100
#define MAX_SUBS 1000
#define BUF_SIZE 1024
#define CACHE_SIZE 15 // Size of the directory cache

#define RED "\033[31m"
#define GREEN "\033[32m"
#define BLUE "\033[34m"
#define BOLD "\033[1m"
#define RESET "\033[0m"


typedef struct {
  char key;
  char *path;
} DirEntry;

// --- Subdirectory Cache Structure ---
typedef struct {
  char base_path[PATH_MAX];
  char subs[MAX_SUBS][PATH_MAX];
  int n_subs;
} SubdirCacheEntry;

static DirEntry dirs[MAX_DIRS];
static int ndirs = 0;
static char last_target[PATH_MAX] = {0};

// Global Cache Storage
static SubdirCacheEntry subdir_cache[CACHE_SIZE] = {0};

// Enum to define the outcome of an action on a file
typedef enum {
  ACTION_CONTINUE_LOOP, // Stay on the current file, ask for input again
  ACTION_NEXT_FILE,     // Go to the next file in the argv list
  ACTION_QUIT_PROGRAM   // Exit the program entirely
} ActionStatus;

char *to_absolute(const char *input) {
  if (!input)
    return NULL;

  // Step 1: expand leading tilde
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

  // Step 2: canonicalize (resolve .., ., symlinks). realpath returns malloc'd
  // string on success.
  char *resolved = realpath(expanded, NULL);
  if (!resolved) {
    // If realpath fails (e.g., path doesn't exist), you can fall back to
    // resolving with cwd:
    if (errno == ENOENT || errno == ENOTDIR) {
      // Build absolute path without resolving symlinks using getcwd + join
      char cwd[PATH_MAX];
      if (expanded[0] != '/') {
        if (!getcwd(cwd, sizeof cwd))
          return NULL;
        char tmp[PATH_MAX];
        if (snprintf(tmp, sizeof tmp, "%s/%s", cwd, expanded) >=
            (int)sizeof tmp)
          return NULL;
        // Normalize .. and . without following symlinks (basic)
        // Use realpath on the parent component if it exists, or return
        // strdup(tmp) as best-effort:
        return strdup(tmp);
      }
    }
    return NULL;
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
    return;
  char line[BUF_SIZE];
  while (fgets(line, sizeof(line), f) && ndirs < MAX_DIRS) {
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++; // initial white space
    if (*p == 0 || *p == '#')
      continue; // commented lines
    if (p[1] == ':') {
      char key = p[0];
      char *path = p + 2;
      while (*path == ' ' || *path == '\t')
        path++;
      char *nl = strchr(path, '\n');
      if (nl)
        *nl = 0;

      char *abs_path = to_absolute(path);
      if (abs_path) {
        dirs[ndirs].key = key;
        dirs[ndirs].path = strdup(abs_path);
        free(abs_path);
        ndirs++;
      } else {
        fprintf(stderr, "Invalid path in config ignored: %s\n", path);
      }
    }
  }
  fclose(f);
}

int list_subdirs(const char *base, char subs[][PATH_MAX]) {

  // 1. Check cache
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (subdir_cache[i].base_path[0] != '\0' &&
        strcmp(subdir_cache[i].base_path, base) == 0) {
      // Cache hit! Copy data and return count immediately.
      int n = subdir_cache[i].n_subs;
      for (int j = 0; j < n; j++) {
        strcpy(subs[j], subdir_cache[i].subs[j]);
      }
      return n;
    }
  }

  // 2. Cache miss: Perform expensive disk operation

  DIR *d = opendir(base);
  if (!d)
    return -1;
  struct dirent *ent;
  int n = 0;
  while ((ent = readdir(d)) && n < MAX_SUBS) {
    if (ent->d_name[0] == '.')
      continue;
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", base, ent->d_name);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
      strcpy(subs[n++], ent->d_name);
    }
  }
  closedir(d);

  // 3. Update cache (Simple circular replacement/eviction)
  static int cache_idx = 0;

  // Store results in the current cache slot
  strncpy(subdir_cache[cache_idx].base_path, base, PATH_MAX);
  subdir_cache[cache_idx].base_path[PATH_MAX - 1] = '\0';
  subdir_cache[cache_idx].n_subs = n;

  for (int i = 0; i < n; i++) {
    strcpy(subdir_cache[cache_idx].subs[i], subs[i]);
  }

  // Move to the next slot (circular)
  cache_idx = (cache_idx + 1) % CACHE_SIZE;


  return n;
}

void print_two_cols(char subs[][PATH_MAX], int n) {
  printf("\n");
  int cols = (n > 20) ? 2 : 1;
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
  if (n < 0)
    return -1;
  if (n == 0) {
    strcpy(outpath, base);
    return 0;
  }
  print_two_cols(subs, n);
  printf("Select subdir number (enter for base): ");
  char buf[BUF_SIZE];
  if (!fgets(buf, sizeof(buf), stdin))
    return -1;
  if (buf[0] == '\n') {
    strcpy(outpath, base);
    return 0;
  }
  int choice = atoi(buf);
  if (choice < 1 || choice > n)
    return -1;
  snprintf(outpath, PATH_MAX, "%s/%s", base, subs[choice - 1]);
  return 0;
}

void do_quickview(const char *file) {
  pid_t pid = fork();
  if (pid == 0) {
    execlp("qlmanage", "qlmanage", "-p", file, (char *)NULL);
    _exit(1);
  } else if (pid > 0) {
    waitpid(pid, NULL, 0);
  }
}


static char *expand_tilde(const char *path) {
  if (!path)
    return NULL;
  if (path[0] != '~')
    return strdup(path);

  /* Only handle ~ and ~/... */
  if (path[1] == '/' || path[1] == '\0') {
    const char *home = getenv("HOME");
    if (!home)
      return NULL;
    size_t need = strlen(home) + strlen(path); /* path includes leading '~' */
    char *out = malloc(need);
    if (!out)
      return NULL;
    /* skip the ~ */
    snprintf(out, need, "%s%s", home, path + 1);
    return out;
  }

  /* For other forms like ~user/... just return a copy (no expansion) */
  return strdup(path);
}

/* A generator used by readline to produce path completions.
   It returns successive matches on each call with increasing state. */
static char *path_completion_generator(const char *text, int state) {
  static DIR *dirp = NULL;
  static char *dirpart = NULL;
  static char *pattern = NULL;
  static size_t pattern_len = 0;
  struct dirent *entry;

  if (state == 0) {
    /* initialize search */
    free(dirpart);
    free(pattern);
    dirpart = NULL;
    pattern = NULL;
    pattern_len = 0;

    /* split text into directory portion and pattern */
    const char *slash = strrchr(text, '/');
    if (slash) {
      size_t dirlen = slash - text;
      char tmp[PATH_MAX];
      if (dirlen == 0) {
        /* a leading slash: root directory */
        strcpy(tmp, "/");
      } else {
        if (dirlen >= sizeof(tmp))
          dirlen = sizeof(tmp) - 1;
        strncpy(tmp, text, dirlen);
        tmp[dirlen] = '\0';
      }
      char *expanded = expand_tilde(tmp);
      if (!expanded)
        return NULL;
      dirpart = strdup(expanded);
      free(expanded);

      pattern = strdup(slash + 1);
    } else {
      dirpart = strdup("."); /* current dir */
      pattern = strdup(text);
    }
    pattern_len = strlen(pattern);

    if (dirpart == NULL || pattern == NULL) {
      free(dirpart);
      free(pattern);
      dirpart = pattern = NULL;
      return NULL;
    }

    /* open directory */
    if (dirp) {
      closedir(dirp);
      dirp = NULL;
    }
    dirp = opendir(dirpart);
    if (!dirp) {
      free(dirpart);
      free(pattern);
      dirpart = pattern = NULL;
      return NULL;
    }
  }

  /* iterate entries */
  while ((entry = readdir(dirp)) != NULL) {
    const char *name = entry->d_name;

    /* Skip '.' and '..' */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    /* If pattern doesn't start with a dot, skip hidden files */
    if (pattern_len == 0) {
      /* empty pattern: show everything except hidden unless explicitly requested */
    } else {
      if (pattern[0] != '.' && name[0] == '.')
        continue;
    }

    if (pattern_len > 0) {
      if (strncmp(name, pattern, pattern_len) != 0)
        continue;
    }

    /* Build displayed completion. If dirpart is ".", show only the name;
       otherwise show dirpart/name. */
    char candidate[PATH_MAX * 2];
    if (strcmp(dirpart, ".") == 0) {
      snprintf(candidate, sizeof(candidate), "%s", name);
    } else if (strcmp(dirpart, "/") == 0) {
      /* root directory special-case to avoid double slashes */
      snprintf(candidate, sizeof(candidate), "/%s", name);
    } else {
      snprintf(candidate, sizeof(candidate), "%s/%s", dirpart, name);
    }

    /* Determine if candidate is a directory and append a trailing slash if so.
       Use stat on the path (not the possibly relative candidate when dirpart=="."). */
    char statpath[PATH_MAX * 2];
    if (strcmp(dirpart, ".") == 0) {
      snprintf(statpath, sizeof(statpath), "%s", name);
    } else {
      snprintf(statpath, sizeof(statpath), "%s/%s", dirpart, name);
    }
    struct stat st;
    int isdir = (stat(statpath, &st) == 0 && S_ISDIR(st.st_mode));

    size_t outlen = strlen(candidate) + (isdir ? 2 : 1); /* +1 for NUL, +1 for '/' if dir */
    char *out = malloc(outlen);
    if (!out) {
      /* on allocation failure, clean up and abort */
      closedir(dirp);
      dirp = NULL;
      free(dirpart);
      free(pattern);
      dirpart = pattern = NULL;
      return NULL;
    }
    if (isdir) {
      snprintf(out, outlen, "%s/", candidate);
    } else {
      snprintf(out, outlen, "%s", candidate);
    }

    return out; /* readline will free this string */
  }

  /* No more matches: cleanup */
  if (dirp) {
    closedir(dirp);
    dirp = NULL;
  }
  free(dirpart);
  free(pattern);
  dirpart = pattern = NULL;
  return NULL;
}



/* This function will be called by Readline when the user hits <Tab>.
   It simply defers to Readline's built-in filename completer. */
static char **my_completion(const char *text, int start, int end) {
  /* Tell readline we are handling completion ourselves, so it
     won't try default completion on our behalf. */
  rl_attempted_completion_over = 1;

  /* Ask readline to call its filename completer: */
  /* return rl_completion_matches(text, rl_filename_completion_function); */
  return rl_completion_matches(
      text, path_completion_generator);
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

  /* newt.c_lflag &= ~(ICANON | ECHO);        // disable canonical mode & echo
   */
  newt.c_lflag &= ~(ICANON); // disable canonical mode
  newt.c_cc[VMIN] = 1;       // minimum number of chars
  newt.c_cc[VTIME] = 0;      // no timeout

  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  return oldt; // return old settings for later restore
}

/* Restore the terminal to its previous state */
static void restore_mode(const struct termios *oldt) {
  tcsetattr(STDIN_FILENO, TCSANOW, oldt);
}

/* --- Move Queue: backgrounds Cross-Filesystem Moves without threads.
   Only one Move is ever In-Flight; the rest wait in a FIFO list. Progress is
   tracked via non-blocking waitpid(WNOHANG) polling from the main loop. --- */

typedef struct MoveQueueItem {
  char src[PATH_MAX];
  char dst[PATH_MAX];
  struct MoveQueueItem *next;
} MoveQueueItem;

static MoveQueueItem *move_queue_head = NULL;
static MoveQueueItem *move_queue_tail = NULL;

static pid_t inflight_pid = -1;
static char inflight_src[PATH_MAX];
static char inflight_dst[PATH_MAX];

/* Returns 1 on success, 0 if the Move could not be queued (e.g. malloc
   failure) — callers must treat 0 as a failure to report, not a success. */
static int move_queue_push(const char *src, const char *dst) {
  MoveQueueItem *item = malloc(sizeof(MoveQueueItem));
  if (!item) {
    perror("malloc");
    return 0;
  }
  strncpy(item->src, src, PATH_MAX - 1);
  item->src[PATH_MAX - 1] = '\0';
  strncpy(item->dst, dst, PATH_MAX - 1);
  item->dst[PATH_MAX - 1] = '\0';
  item->next = NULL;

  if (move_queue_tail)
    move_queue_tail->next = item;
  else
    move_queue_head = item;
  move_queue_tail = item;
  return 1;
}

/* Fork+exec the next queued Move if none is currently In-Flight. Loops
   (rather than recursing) past any Moves that fail to fork, so a run of
   fork() failures can't grow the call stack with queue depth. */
static void move_queue_start_next(void) {
  while (inflight_pid == -1 && move_queue_head) {
    MoveQueueItem *item = move_queue_head;
    move_queue_head = item->next;
    if (!move_queue_head)
      move_queue_tail = NULL;

    strcpy(inflight_src, item->src);
    strcpy(inflight_dst, item->dst);
    free(item);

    printf(BLUE "\nMoving %s\n" RESET, inflight_src);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      printf(RED "Move failed: %s\n" RESET, inflight_src);
      fflush(stdout);
      continue; /* try the next queued item */
    }

    if (pid == 0) { /* child */
      execlp("mv", "mv", "-f", inflight_src, inflight_dst, (char *)NULL);
      perror("execlp");
      _exit(3);
    }

    inflight_pid = pid; /* parent: track, do not wait */
  }
}

/* Reports the outcome of the In-Flight Move and starts the next queued one,
   if any. Shared by the non-blocking poll and the blocking drain. */
static void move_queue_finish_inflight(int status) {
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    printf(GREEN "\nMoved %s to %s\n" RESET, inflight_src, inflight_dst);
  else
    printf(RED "\nMove failed: %s (left in place)\n" RESET, inflight_src);
  fflush(stdout);

  inflight_pid = -1;
  move_queue_start_next();
}

/* Non-blocking check of the In-Flight Move. Safe to call often (e.g. every
   spin of the select()-based input loop); it is a no-op unless something
   just finished. */
static void move_queue_poll(void) {
  if (inflight_pid == -1)
    return;

  int status;
  pid_t r = waitpid(inflight_pid, &status, WNOHANG);
  if (r == 0)
    return; /* still running */
  if (r < 0) {
    if (errno == EINTR)
      return;
    perror("waitpid");
    inflight_pid = -1;
    move_queue_start_next();
    return;
  }

  move_queue_finish_inflight(status);
}

/* Block until the Move Queue (including the In-Flight Move) is empty. Called
   before the program exits so a Cross-Filesystem Move never gets truncated
   at the destination. */
static void move_queue_drain(void) {
  if (inflight_pid == -1 && !move_queue_head)
    return;

  printf(BLUE "\nDraining Move Queue...\n" RESET);
  fflush(stdout);

  move_queue_start_next();
  while (inflight_pid != -1) {
    int status;
    pid_t r = waitpid(inflight_pid, &status, 0);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      perror("waitpid");
      inflight_pid = -1;
      move_queue_start_next();
      continue;
    }
    move_queue_finish_inflight(status);
  }
}

typedef enum {
  MOVE_LOCAL_OK,    /* Local Move: rename() succeeded immediately */
  MOVE_LOCAL_ERROR, /* rename() failed for a reason other than EXDEV, or the
                        Move could not be placed on the Move Queue */
  MOVE_QUEUED       /* Cross-Filesystem Move: placed on the Move Queue */
} MoveOutcome;

static MoveOutcome rename_or_queue(const char *src, const char *dst) {
  if (rename(src, dst) == 0) {
    printf(GREEN "\nMoved to %s\n" RESET, dst);
    return MOVE_LOCAL_OK;
  }
  if (errno != EXDEV) {
    perror("rename");
    return MOVE_LOCAL_ERROR;
  }

  if (!move_queue_push(src, dst)) {
    printf(RED "Move failed: could not queue %s\n" RESET, src);
    return MOVE_LOCAL_ERROR;
  }
  move_queue_start_next();
  return MOVE_QUEUED;
}

void display_menu(const char *file) {
  printf("\n File: " BOLD "%s" RESET "\n", file);
  printf(BOLD "q)" RESET "Quit  d) Delete  v) Quickview  i) Ignore  m) Move  p) "
         /* "PrevDest c) Custom path\n"); */
         "PrevDest c) Custom path  x) Run external program\n");

  printf("Choose: ");
  fflush(stdout);
}

ActionStatus cleanup_delete_file(const char *filepath) {
  if (remove(filepath) == 0) {
    printf(GREEN "\nDeleted: %s\n" RESET, filepath);
    return ACTION_NEXT_FILE;
  } else {
    perror(RED "Error deleting file" RESET);
    return ACTION_CONTINUE_LOOP; // Stay on the same file if deletion fails
  }
}

ActionStatus cleanup_move_to_prev_dest(const char *file) {
  if (last_target[0] == 0) {
    printf(RED "No previous target.\n" RESET);
    return ACTION_CONTINUE_LOOP;
  }

  char dest[PATH_MAX];
  snprintf(dest, PATH_MAX, "%s/%s", last_target,
           strrchr(file, '/') ? strrchr(file, '/') + 1 : file);

  return rename_or_queue(file, dest) != MOVE_LOCAL_ERROR ? ACTION_NEXT_FILE
                                                          : ACTION_CONTINUE_LOOP;
}

ActionStatus cleanup_move_to_configured_dest(const char *file,
                                             struct termios *original_termios) {

  if (ndirs == 0) {
    printf("No dirs in config.\n");
    return ACTION_CONTINUE_LOOP;
  }
  printf("\n");

  char path_copy[300];

  for (int j = 0; j < ndirs; j++) {
    /* strcpy(path_copy, dirs[j].path); */
    snprintf(path_copy, sizeof(path_copy), "%s", dirs[j].path);
    /* printf("%c) %s\n", dirs[j].key, dirs[j].path); */
    printf("%c) " BOLD "%-20s" RESET "\t %s\n", dirs[j].key,
           basename(path_copy), dirs[j].path);
  }
  printf("Select dir key: ");
  fflush(stdout);

  /* if (!fgets(buf,sizeof(buf),stdin)) return 0; */
  /* char key = buf[0]; */
  char key = getchar();

  int found_idx = -1;
  for (int j = 0; j < ndirs; j++)
    if (dirs[j].key == key) {
      found_idx = j;
      break;
    }

  if (found_idx < 0) {
    printf(RED "Invalid key '%c.\n" RESET, key);
    return ACTION_CONTINUE_LOOP;
  }

  char target[PATH_MAX];

  // Restore terminal for the fgets input
  fflush(stdout);
  /* restore_mode(&oldt); */
  restore_mode(original_termios);

  if (choose_subdir(dirs[found_idx].path, target) < 0) {
    printf(RED "Subdir select error.\n" RESET);
    /* oldt = enable_raw_mode(); */
    enable_raw_mode();
    return ACTION_CONTINUE_LOOP;
  }

  // Return to raw mode for single char inputs
  enable_raw_mode();

  char dest[PATH_MAX];
  snprintf(dest, PATH_MAX, "%s/%s", target,
           strrchr(file, '/') ? strrchr(file, '/') + 1 : file);
  /* if (rename(file, dest)==0) { */
  /*     printf("Moved to %s\n", dest); */
  /*     strcpy(last_target, target); */
  /* } else if (errno != EXDEV) { */
  /*       perror("rename"); */
  /* } else { */
  /*   /\* Different filesystems, use external mv command *\/ */
  /*   run_external_mv(file, dest); */
  /* } */
  if (rename_or_queue(file, dest) != MOVE_LOCAL_ERROR) {
    strcpy(last_target, target);
    return ACTION_NEXT_FILE;
  } else {
    return ACTION_CONTINUE_LOOP;
  }
}

ActionStatus cleanup_move_to_custom_path(const char *file,
                                         struct termios *original_termios) {

  /* manual directory entry */
  char destdir[PATH_MAX];
  /* printf("Enter destination directory: "); */
  /* if (!fgets(destdir, sizeof(destdir), stdin))  */
  /* break; */

  char *readline_str;

  restore_mode(original_termios);

  /* Install our completion function */
  rl_attempted_completion_function = my_completion;

  /* Loop reading lines until EOF (Ctrl-D) */
  while ((readline_str = readline("Path> ")) != NULL) {
    if (readline_str == NULL) {
      printf("\n");
      break;
    }
    if (*readline_str) {
      /* non-empty: add to history */
      add_history(readline_str);
    }
    printf("You entered: %s\n", readline_str);
    char *abs_path = to_absolute(readline_str);
    if (!abs_path) {
      printf("Invalid path!\n");
      free(readline_str);
      /* free(abs_path); */
      continue;
    }

    /* strcpy(destdir, abs_path); */
    collapse_dot_slash(abs_path, destdir);
    free(readline_str);
    free(abs_path);
    break;
  }

  enable_raw_mode(); // Re-enable raw mode

  /* strip newline */
  destdir[strcspn(destdir, "\r\n")] = '\0';
  /* verify it exists and is a directory */
  struct stat st;
  if (stat(destdir, &st) < 0) {
    perror("stat");
    return ACTION_CONTINUE_LOOP;
  }
  if (!S_ISDIR(st.st_mode)) {
    fprintf(stderr, "%s is not a directory\n", destdir);
    return ACTION_CONTINUE_LOOP;
  }
  /* build the destination path */
  const char *fname = strrchr(file, '/');
  fname = fname ? fname + 1 : file;
  char dest[PATH_MAX];
  snprintf(dest, sizeof(dest), "%s/%s", destdir, fname);
  /* if (rename(file, dest) == 0) { */
  /*   printf("Moved to %s\n", dest); */
  /*   strcpy(last_target, destdir); */
  /* } else { */
  /*   perror("rename"); */
  /* } */

  if (rename_or_queue(file, dest) != MOVE_LOCAL_ERROR) {
    strcpy(last_target, destdir);
    return ACTION_NEXT_FILE;
  } else
    return ACTION_CONTINUE_LOOP;
}


ActionStatus cleanup_run_external(const char *file, struct termios *original_termios) {
  /* Restore normal terminal for user input */
  restore_mode(original_termios);

  /* Read a command line from the user */
  char *line = readline("Run> ");
  if (!line) {
    /* EOF / Ctrl-D */
    enable_raw_mode();
    return ACTION_CONTINUE_LOOP;
  }
  if (*line)
    add_history(line);

  /* Tokenize (simple whitespace split). Build argv for execvp. */
  char *saveptr = NULL;
  char *tok;
  char *copy = line;
  char *argv_exec[64];
  int argc_exec = 0;
  int used_placeholder = 0;

  while ((tok = strtok_r(copy, " \t", &saveptr)) != NULL && argc_exec < (int)(sizeof(argv_exec)/sizeof(argv_exec[0]) - 1)) {
    copy = NULL;
    char *p = strstr(tok, "{}");
    if (p) {
      /* replace {} inside this token with the filename */
      used_placeholder = 1;
      size_t newlen = strlen(tok) - 2 + strlen(file) + 1;
      char *repl = malloc(newlen);
      if (!repl)
        break;
      size_t prefix = p - tok;
      memcpy(repl, tok, prefix);
      strcpy(repl + prefix, file);
      strcpy(repl + prefix + strlen(file), p + 2);
      argv_exec[argc_exec++] = repl;
    } else {
      argv_exec[argc_exec++] = strdup(tok);
    }
  }

  /* If no {} seen, append the filename as last argument */
  if (!used_placeholder && argc_exec < (int)(sizeof(argv_exec)/sizeof(argv_exec[0]) - 1)) {
    argv_exec[argc_exec++] = strdup(file);
  }
  argv_exec[argc_exec] = NULL;

  free(line);

  /* Execute the program */
  if (argc_exec == 0) {
    enable_raw_mode();
    return ACTION_CONTINUE_LOOP;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    /* cleanup */
    for (int i = 0; i < argc_exec; i++)
      free(argv_exec[i]);
    enable_raw_mode();
    return ACTION_CONTINUE_LOOP;
  }

  if (pid == 0) {
    /* child: exec */
    execvp(argv_exec[0], argv_exec);
    /* if execvp fails */
    perror("execvp");
    _exit(127);
  }

  /* parent: wait and report status */
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    perror("waitpid");
  } else {
    if (WIFEXITED(status)) {
      printf("Process exited with status %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("Process killed by signal %d\n", WTERMSIG(status));
    }
  }

  /* cleanup argv memory */
  for (int i = 0; i < argc_exec; i++)
    free(argv_exec[i]);

  /* Return to raw mode before returning */
  enable_raw_mode();
  return ACTION_CONTINUE_LOOP;
}


/* Waits for a single character on stdin without blocking Move Queue status
   updates: polls the In-Flight Move on each select() timeout so completions
   print live instead of waiting for the next keypress. */
static int wait_for_input_char(void) {
  for (;;) {
    move_queue_poll();

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};

    int rv = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (rv < 0) {
      if (errno == EINTR)
        continue;
      perror("select");
      return EOF;
    }
    if (rv > 0 && FD_ISSET(STDIN_FILENO, &fds))
      return getchar();
    /* timeout: loop back around and poll the queue again */
  }
}

/* Drains the Move Queue and restores the terminal before exiting, whether
   the user quit explicitly or the file list ran out. */
static int drain_and_exit(struct termios *oldt) {
  move_queue_drain();
  restore_mode(oldt);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s file1 [file2...]\n", argv[0]);
    return 1;
  }

  load_config();

  struct termios oldt = enable_raw_mode();

  /* Loop through all files */
  for (int i = 1; i < argc; i++) {
    char *file = argv[i];
    if (access(file, F_OK) != 0) {
      perror(file);
      continue;
    }
    while (1) {
      display_menu(file);
      /* char buf[BUF_SIZE]; */
      /* if (!fgets(buf,sizeof(buf),stdin)) return 0; */
      /* int opt = atoi(buf); */

      int opt;
      opt = wait_for_input_char();

      /* if (opt==1) return 0; */
      if (opt == 113) // input: 'q'uit
        return drain_and_exit(&oldt);
      /* else if (opt==2) { */
      else if (opt == 100) { // input: 'd'elete
        if (remove(file) == 0)
          printf(GREEN "\nDeleted.\n" RESET);
        else
          perror("Delete");
        break;
        /* } else if (opt==3) { */
      } else if (opt == 118) { // input: 'v'iew
        do_quickview(file);
        /* } else if (opt==4) { */
      } else if (opt == 105) { // input: 'i'gnore
        break;

        /* } else if (opt==6) { */
      } else if (opt == 112) { // input: 'p'revdest
        if (cleanup_move_to_prev_dest(file) == ACTION_NEXT_FILE)
          break;
        else
          continue;

        /* } else if (opt==5) { */
      } else if (opt == 109) { // input: 'm'ove
        if (cleanup_move_to_configured_dest(file, &oldt) == ACTION_NEXT_FILE)
          break;
        else
          continue;

        /* } else if (opt == 7) { */
      } else if (opt == 99) { // input: 'c'ustom path
        if (cleanup_move_to_custom_path(file, &oldt) == ACTION_NEXT_FILE)
          break;
        else
          continue;
      } else if (opt == 120) { // input: 'x' run external command
        if (cleanup_run_external(file, &oldt) == ACTION_NEXT_FILE)
          break;
        else
          continue;

      } else {
        printf("Bad option.\n");
      }

    }
  }

  return drain_and_exit(&oldt);
}
