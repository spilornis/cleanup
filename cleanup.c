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

static DirEntry dirs[MAX_DIRS];
static int ndirs = 0;
static char last_target[PATH_MAX] = {0};

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

static int rename_or_mv(const char *src, const char *dst) {

  if (rename(src, dst) == 0) {
    printf(GREEN "Moved to %s\n" RESET, dst);
    return 0;
    /* strcpy(last_target, target); */
  } else if (errno != EXDEV) {
    perror("rename");
    return -1;
  } else {
    /* Different filesystems, use external mv command */

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      return 3;
    }

    if (pid == 0) { /* child */
      /* Replace the child process with /bin/mv */
      execlp("mv", "mv", "-f", src, dst, (char *)NULL);
      /* If execlp returns, an error occurred */
      perror("execlp");
      _exit(3);
    }

    /* parent – wait for child */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
      perror("waitpid");
      return 3;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      printf(GREEN "Moved to %s\n" RESET, dst);
      return 0; /* mv succeeded */
    }
    else
      return 2; /* mv reported an error */
  }
  return 0;
}

void display_menu(const char *file) {
  printf("\n File: " BOLD "%s" RESET "\n", file);
  printf("q) Quit  d) Delete  v) Quickview  i) Ignore  m) Move  p) "
         "PrevDest c) Custom path\n");
  printf("Choose: ");
  fflush(stdout);
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


int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s file1 [file2...]\n", argv[0]);
    return 1;
  }

  load_config();

  struct termios oldt = enable_raw_mode();

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
      opt = getchar();

      /* if (opt==1) return 0; */
      if (opt == 113)        // input: 'q'uit
        return 0;
      /* else if (opt==2) { */
      else if (opt == 100) { // input: 'd'elete
        if (remove(file) == 0)
          printf("Deleted.\n");
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
        if (last_target[0] == 0) {
          printf("No previous target.\n");
          continue;
        }
        char dest[PATH_MAX];
        snprintf(dest, PATH_MAX, "%s/%s", last_target,
                 strrchr(file, '/') ? strrchr(file, '/') + 1 : file);
        if (rename_or_mv(file, dest) == 0) {
          printf("Moved to %s\n", dest);
        } else
          continue;
        /* if (rename(file, dest) == 0) { */
        /*   printf("Moved to %s\n", dest); */
        /* } else { */
        /*   perror("Move"); */
        /* } */

        break;


        /* } else if (opt==5) { */
      } else if (opt == 109) { // input: 'm'ove
        if (ndirs == 0) {
          printf("No dirs in config.\n");
          continue;
        }
        printf("\n");
        for (int j = 0; j < ndirs; j++) {
          printf("%c) %s\n", dirs[j].key, dirs[j].path);
        }
        printf("Select dir key: ");

        /* if (!fgets(buf,sizeof(buf),stdin)) return 0; */
        /* char key = buf[0]; */
        char key = getchar();

        int found = -1;
        for (int j = 0; j < ndirs; j++)
          if (dirs[j].key == key) {
            found = j;
            break;
          }
        if (found < 0) {
          printf("Invalid.\n");
          continue;
        }
        char target[PATH_MAX];

        // Restore terminal for the fgets input
        fflush(stdin);
        restore_mode(&oldt);

        if (choose_subdir(dirs[found].path, target) < 0) {
          printf("Subdir select error.\n");
          oldt = enable_raw_mode();
          continue;
        }

        // Return to raw mode for single char inputs
        oldt = enable_raw_mode();

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
        if (rename_or_mv(file, dest) == 0) {
          strcpy(last_target, target);
          break;
        } else {
          continue;
        }


        /* } else if (opt == 7) { */
      } else if (opt == 99) { // input: 'c'ustom path
        /* manual directory entry */
        char destdir[PATH_MAX];
        /* printf("Enter destination directory: "); */
        /* if (!fgets(destdir, sizeof(destdir), stdin))  */
        /* break; */

        char *readline_str;

        /* Install our completion function */
        rl_attempted_completion_function = my_completion;

        /* Loop reading lines until EOF (Ctrl-D) */
        while ((readline_str = readline("Path> ")) != NULL) {
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

        /* strip newline */
        destdir[strcspn(destdir, "\r\n")] = '\0';
        /* verify it exists and is a directory */
        struct stat st;
        if (stat(destdir, &st) < 0) {
          perror("stat");
          continue;
        }
        if (!S_ISDIR(st.st_mode)) {
          fprintf(stderr, "%s is not a directory\n", destdir);
          continue;
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

        if (rename_or_mv(file, dest) == 0) {
          printf("Moved to %s\n", dest);
        } else
          continue;
      } else {
        printf("Bad option.\n");
      }
    }
  }

  restore_mode(&oldt);

  return 0;
}
