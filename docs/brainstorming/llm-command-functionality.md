You can get most of this for free by re-using the same readline trick you already have in option 7, and then either

  1. build a single string (command + filename) and invoke it via `system(…)`, or  
  2. split the user’s line into an argv[] array and fork/execvp(…) it directly.

Below is a very minimal patch showing one way to do it with `system()`.  You could of course steal the tokenizing-and-execvp version if you really want more control, but for many use-cases `system()` is enough.

--- in your `main()` replace/add in the big switch over `opt`---

```c
     else if (opt == 8) {
       char *rl, *cmdline;
       /* install filename completer if you like */
       rl_attempted_completion_function = my_completion;
       rl = readline("! Command> ");
       if (!rl) break;               /* user hit ^D */
       if (*rl) add_history(rl);     /* remember it */
       
       /* construct "user-entered command" plus the filename operand */
       size_t n = strlen(rl) + 1 + strlen(file) + 1;
       cmdline = malloc(n);
       if (!cmdline) {
         perror("malloc");
         free(rl);
         break;
       }
       /* if you want to support a "{}" placeholder you could
        * look for it in rl and do a replace.  For now we just
        * always append the filename at the end:
        */
       snprintf(cmdline, n, "%s %s", rl, file);

       /* fire off the command and wait for it to finish */
       int st = system(cmdline);
       if (st == -1) {
         perror("system");
       }
       else {
         printf("-> returned %d\n", WEXITSTATUS(st));
       }

       free(cmdline);
       free(rl);
       /* stay in the inner loop so the user can run more commands
        * on the same file, or hit another menu choice */
       continue;
     }
```

If you’d rather invoke it more “natively” (no shell, no quoting worries), you can replace `system(cmdline)` with something like:

```c
     /* simple whitespace tokenization; not as robust as wordexp(3) */
     char *argv[64];
     int   argc2 = 0;
     argv[argc2++] = strtok(cmdline, " \t");
     while (argc2 < 63 && (argv[argc2] = strtok(NULL, " \t")))
       argc2++;
     argv[argc2++] = file;    /* tack the filename on as last argument */
     argv[argc2]   = NULL;

     if (fork() == 0) {
       execvp(argv[0], argv);
       perror("execvp");
       _exit(127);
     } else {
       wait(NULL);
     }
```

—or, if you want full shell-style expansion of `~`, `*`, `?`, etc., you can use `wordexp(3)` instead of `strtok()`.

Either way, the only real new code you need is:

  1. a new menu choice (say `8) Custom Command`),  
  2. a readline prompt,  
  3. a loop that either `system()`-calls or `fork()`/`exec()`s the user’s line + `file`.  

That’s all there is to it.
