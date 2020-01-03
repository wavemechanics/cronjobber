Have you ever seen these problems with cron jobs?

* Frequent emails that nobody reads
* Output redirected to `/dev/null`
* Hundreds of the same job accumulate; subsequent machine crash
* Thundering herds as hundreds of machines always kick off the same job at once
* Job runs fine from your login environment, but not from cron environment
* Job stops working, but there are no mails or logs to help troubleshoot

These problems are seen everywhere, and various workarounds are made up on the spot.
For example, the problem of too much mail is usually "solved" by redirecting output to `/dev/null`.
Of course this means that jobs can silently fail without any trace of what went wrong.

Cronjobber solves these problems in a systematic way:

* All job output is kept in rotated log files. Rotation age is user-defined.
* Mail is only sent when something significant happens.
User-written checker script defines "significant" based on job output and exit value.
* Mail can be sent to someone other than the crontab owner.
* Locks prevent multiple instances of a job from running at once.
* Optional timeouts can signal long-running jobs.
* Optional random backoffs can thin thundering herds.
* Detects if a job didn't finish because of a system crash.
* Sets up predictable environment for testing.

Cronjobber is implemented in a single C source file which should be portable to all posix systems.

[How to build and install](install.md)

[How to Setup a Cron Job](setup.md)

[How to Write a Checker Script](checker.md)

[Examples](examples.md)

[Theory](theory.md)

[TODO](TODO.md)
