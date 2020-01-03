How to Write a Checker Script

You can configure cronjobber to invoke a user-provided script to look at job output and decide whether or not to have mail sent.
If no script is provided, mail will be sent if the job exited non-zero or if the job produced any output on stdout or stderr.

The name of the checker script is given to cronjobber using the `-x` command line flag.
It is invoked directly, unlike the job which is invoked via `$SHELL -c`.

## Current Directory

When the script is started, its current directory will be the cronjobber state directory, which is passed to cronjobber with the `-s` flag.

## Environment

the checker script is passed the same minimal environment given to the job, plus one of the following variables will also be set:

| Variable | Contents |
|----------|----------|
| WEXITSTATUS | If the job exited normally, this variable will hold its exit status.
| WTERMSIG | If the job was killed by a signal, this variable will hold the number of the signal.
| WSTATUS | Shouldn't happen.

## stdin

The checker script stdin will be the log file produced by the job just run.
The checker script may read from stdin and/or directly read `log`, whichever makes more sense.

## stdout/stderr

stdout and stderr are unchanged from those provided by `cron(8)`.
Anything output from the checker will result in cron sending mail to the crontab owner.

## Command line arguments

No command line arguments are passed to the checker in the current version of cronjobber.

## Exit Values

The checker should exit 0 if the job succeeded and mail should NOT be sent.
Otherwise mail will be sent.