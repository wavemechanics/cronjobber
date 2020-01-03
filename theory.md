These are the steps performed by cronjobber each time it is invoked.
Note that when cronjobber itself prints a message, the message will be delivered to the crontab owner by the normal means.

## Change to state directory

Cronjobber `chdir`s into the state directory given by the `-s` command line option.
Each cron job must have its own state directory writable by the cron job owner.
This directory may be empty the first time cronjobber runs.
Files within this directory will be created as necessary.

If cronjobber cannot change to the state directory, it will print a message and exit.

## Get exclusive lock

To prevent multiple instances of this job from running at the same time, cronjobber opens or creates a file called `lock` and requests an exclusive lock (`F_WRLCK`) on this file.
If the lock cannot be obtained, then another instance of cronjobber still holds the lock, so this instance prints a message and exits.

If cronjobber frequently finds the state directory locked, then the job is routinely taking too long and you should either figure out a way to speed up the job or reduce the frequency of runs.

## Check for crashed instance

Cronjobber next looks for a file named `log`.
If this file exists, then the previous instance must have crashed before rotating the log files, probably the result of a system crash.
This means the actual cron job may not have completed.
So the contents of the log file are emailed in a '`crashed`' message, and then the log is rotated the usual way.

Normally the timestamps embedded in the names of rotated log files are based on the time the cron job started.
But when a log file from a crashed job is rotated, the embedded timestamp will be based on the modification time of the log file.

## Create log file

A new `log` file is now created.
This will contain the output of this instance of the job.

## Random backoff

If an `-r` value was given on the cronjobber command line, then cronjobber will now wait for a random time up to the value of `-r` seconds.
`/dev/urandom` is read for a random value.
If this doesn't work, `getpid()` is called to get a "random" value.
Using `getpid()` isn't terrible since we are only trying to thin a thundering herd; we don't need cryptographic quality randomness.

## Kick off job

Cronjobber now forks.
The child changes to the directory given by the `-D` option, and redirects stdout and stderr to the log file.
The child also sets up environment variables and umask as specified by the `-e` and `-m` options.

The job is actually kicked off in the child by `exec`-ing a shell with `-c` and the exact string given to cronjobber's `-c`- option.
If there are any errors in this step, a message will be printed, and the child will exit.
These messages will be delivered to the crontab owner.

## Wait for job

Meanwhile the parent process waits for the child to exit.
If a `-t` timeout value was given, an alarm is set in the parent to go off if the child takes too long.
if the timeout alarm goes off, the parent will send the child the signal given by `-k`, or `SIGTERM` by default.
the parent will only send one signal and will continue waiting for the child after the signal is sent.
If the child ignores the signal, the parent could wait indefinitely.
This is deliberate.
The parent must remain as long as the child exists because the parent holds the lock.

When the child exits, its wait status is saved for reporting to the checker.

## Run checker

If cronjobber was not given a checker script with the `-x` option, then the default checker is used.
The default checker looks at the job's exit status and the size of the log file.
If the exit status is not zero, or fi the size of the log file is not zero, then the job is deemed to have failed, and a '`failed`' message will be sent.

If there was a checker script given by `-x`, then it is started similarly to the job itself.
The current directory is the state directory.
stdin is redirected from `log`.
Environment variables are set as they were for the job, plus one of `WTERMSIG`, `WEXITSTATUS`, or `WSTATUS` is set to reflect the exit status of the job.

The checker may look at the `W*` environment variables and/or the log file to determine whether or not the job succeeded.
If the job succeeded, the checker should exit 0.
Any other exit value will tell cronjobber the job failed, and the log will be sent in a '`failed`' message.

The checker's stdin is the log file, but since the checker is executing from within in the state directory, it may also read `log` directory if that makes coding the checker easier.

## Rotate and clean logs

The log file is renamed to include a timestamp, and logs older than the age given by the `-a` option are removed.
If no age is given, logs are kept indefinitely.

## Release lock

cronjobber now exits and implicitly releases the `fcntl()` lock.