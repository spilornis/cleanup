# File/Directory Move Function in C

Here's a C function to move a file or directory to a target directory with proper error handling and cross-filesystem support:

```c
/**
 * @file file_move.c
 * 
 * Moves a file or directory to a target location.
 * If source and target are on the same filesystem, uses rename()
 * Otherwise executes the "mv" command as a fallback.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * @brief Move a file or directory to a target location
 *
 * This function attempts to move a file or directory to the specified
 * target path. It first checks if the source and target are on the same
 * filesystem. If they are, it uses rename() for efficiency. If not, it
 * falls back to executing the "mv" command.
 *
 * @param source_path Path to the source file or directory to be moved
 * @param target_path Target path where source should be moved to
 * @return 0 on success, negative values on failure:
 *         -1: General error (check errno)
 *         -2: Source does not exist
 *         -3: Target is not a directory
 *         -4: External mv command failed
 */
int move_file_or_dir(const char *source_path, const char *target_path) {
    struct stat source_stat, target_stat;
    
    /* Check if source exists */
    if (stat(source_path, &source_stat) != 0) {
        fprintf(stderr, "Error: Source '%s' does not exist or is inaccessible: %s\n", 
                source_path, strerror(errno));
        return -2;
    }
    
    /* Check if target exists and is a directory */
    if (stat(target_path, &target_stat) != 0) {
        fprintf(stderr, "Error: Target '%s' does not exist or is inaccessible: %s\n", 
                target_path, strerror(errno));
        return -1;
    }
    
    if (!S_ISDIR(target_stat.st_mode)) {
        fprintf(stderr, "Error: Target '%s' is not a directory\n", target_path);
        return -3;
    }
    
    /* Create the full target path including the source filename */
    char *source_basename = strrchr(source_path, '/');
    if (source_basename == NULL) {
        source_basename = (char *)source_path; /* No '/' in path */
    } else {
        source_basename++; /* Skip the '/' */
    }
    
    size_t target_len = strlen(target_path);
    size_t basename_len = strlen(source_basename);
    char *full_target_path = malloc(target_len + basename_len + 2); /* +2 for '/' and '\0' */
    
    if (full_target_path == NULL) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return -1;
    }
    
    strcpy(full_target_path, target_path);
    if (target_path[target_len - 1] != '/') {
        strcat(full_target_path, "/");
    }
    strcat(full_target_path, source_basename);
    
    /* Check if source and target are on the same filesystem */
    if (source_stat.st_dev == target_stat.st_dev) {
        /* Same filesystem, use rename() */
        if (rename(source_path, full_target_path) != 0) {
            fprintf(stderr, "Error moving '%s' to '%s': %s\n", 
                    source_path, full_target_path, strerror(errno));
            free(full_target_path);
            return -1;
        }
    } else {
        /* Different filesystems, use external mv command */
        pid_t pid = fork();
        
        if (pid < 0) {
            fprintf(stderr, "Error: Fork failed: %s\n", strerror(errno));
            free(full_target_path);
            return -1;
        } else if (pid == 0) {
            /* Child process */
            execlp("mv", "mv", source_path, target_path, NULL);
            /* If execlp returns, it failed */
            fprintf(stderr, "Error: Failed to execute mv command: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            /* Parent process */
            int status;
            waitpid(pid, &status, 0);
            
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                fprintf(stderr, "Error: mv command failed\n");
                free(full_target_path);
                return -4;
            }
        }
    }
    
    free(full_target_path);
    return 0;
}
```

## Usage Example

Here's a simple example of how to use this function:

```c
#include <stdio.h>

int move_file_or_dir(const char *source_path, const char *target_path);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s source_path target_directory\n", argv[0]);
        return 1;
    }
    
    int result = move_file_or_dir(argv[1], argv[2]);
    
    if (result == 0) {
        printf("Successfully moved '%s' to '%s'\n", argv[1], argv[2]);
        return 0;
    } else {
        fprintf(stderr, "Failed to move file. Error code: %d\n", result);
        return 1;
    }
}
```

## Notes

1. The function detects whether source and target are on the same filesystem by comparing their device IDs.
2. When using the "mv" command for cross-filesystem moves, it uses `fork()` and `execlp()`.
3. Error codes are returned as negative integers for easy identification of different error conditions.
4. The function properly handles paths with or without trailing slashes.
