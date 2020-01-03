How to Setup a Cron Job

## Create state directory

Each job requires its own state directory on a filesystem that supports `fcntl()` locking.
Local filesystems should be ok, NAS filesystems should be tested.

The state directory holds a `lock` file and log files from each job run.
It may be convenient to put the `wrap` and `checker` scripts in this directory, but this isn't required.
You really only need to create an empty directory and make sure it is writable by the cron job owner.

## Create wrapper script

Cronjobber may be invoked directly from cron, but because of the number of command line options, the cron entry may become unwieldy.
A wrapper script may be written to make the cron entry less confusing.
See the [man page](cronjobber.man.txt) for cronjobber command line options.

Here is an example wrapper script:

```
#!/bin/sh

D=/root/cron/gen-key

exec /usr/local/bin/cronjobber \
        -n gen-key \
        -s $D \
        -c $D/job \
        -D $D \
        -x $D/checker \
        -t 300 \
        -T dl \
        -a 7d
```

## Create checker script (optional)

The job of the checker script is to decide whether or not job output should be mailed or not.
Without a checker script, cronjobber will send mail if the job exited non-zero or if any stdout or sterr output was produced.
Use a checker script to refine this behaviour.

The checker script is invoked with certain environment variables set to indicate the job's exit status, and with job output on stdin.
The checker script uses this information to decide whether or not to have cronjobber email the job output.
If the checker script exits 0, then no job output will be emailed.
Otherwise, the output will be emailed.
See the [man page](cronjobber.man.txt) for more information.

Here is an example checker script that causes mail to be sent if the job exited non-zero or if the job output did not include the word `OK`:

```
#!/bin/sh

if test "$WEXITSTATUS" != 0
then
        exit 1
fi
grep OK >/dev/null 2>&1
```

## Test

Test by invoking the wrapper.
Errors having to do with the wrapper itself will be printed immediately.
But other errors will cause an email to be sent.
Look in the state directory for the log file and verify it is sane.

## Crontab entry

The crontab entry should simply kick off the wrapper.
Do not redirect output to `/dev/null`.
When everything is working, and you have refined the checker, you will only receive email when something has gone wrong.
And you always have the option of looking in the state directory for logged output of previous runs.