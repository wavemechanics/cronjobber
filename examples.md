An example cronjobber setup

## Raid controller status

This is an example of how cronjobber can be used to watch for changes in the output of a command.
In this example, we use the output of raid controller status command `tw_cli`.

the actual cron job retrieves `tw_cli` output, and cronjobber ensures the output is saved to a log under `/root/cron/3ware`.

Here is an exmaple wrapper script:

```
#!/bin/sh

D=/root/cron/3ware

exec /local/bin/cronjobber \
        -n 3ware \
        -s $D \
        -c "/usr/local/sbin/tw_cli /c0 show" \
        -D $D \
        -x $D/checker \
        -t 60 \
        -T root \
        -a 7d
```

The wrapper will execute `/usr/local/sbin/tw_cli /c0 show` and place the output in a log file in the state directory `/root/cron/3ware`.
There is a 60 second command timeout, and logs will be kept for 7 days.
Error emails will be sent to `root`.

The `checker` script below simply diffs the output with known good output and exits with the appropriate value.
If `tw_cli` could not be executed, or if a single character differs from the expected output, `root` will be sent mail containing the contents of the output log file.
The email subject will be `3ware: failed`.

```
#!/bin/sh

if test "$WEXITSTATUS" != 0
then
        exit 1
fi
diff - log <<\EOF >/dev/null 2>&1

Unit  UnitType  Status         %RCmpl  %V/I/M  Stripe  Size(GB)  Cache  AVrfy
------------------------------------------------------------------------------
u0    JBOD      OK             -       -       -       114.499   OFF    -      
u1    JBOD      OK             -       -       -       114.499   OFF    -      

Port   Status           Unit   Size        Blocks        Serial
---------------------------------------------------------------
p0     OK               u0     114.49 GB   240121728     Y3MFBDDE            
p1     OK               u1     114.49 GB   240121728     Y3MFBDYE            

EOF
```

The actual crontab entry would look something like this:

```
# Check raid controller status daily
31      6       *       *       *       root    /root/cron/3ware/wrap
```

After a week, the state directory would be along the lines of:

```
$ ls -l /root/cron/3ware
total 24
-rwxr-xr-x  1 root  wheel  108 Apr 15 06:19 checker
-rwxr-xr-x  1 root  wheel  183 Jun 19 13:22 job
-rw-r--r--  1 root  wheel    0 Apr  2 06:16 lock
-rw-r--r--  1 root  wheel  113 Jul  2 06:31 log.20110702063100
-rw-r--r--  1 root  wheel  137 Jul  3 06:31 log.20110703063101
-rw-r--r--  1 root  wheel  133 Jul  4 06:31 log.20110704063101
-rw-r--r--  1 root  wheel  133 Jul  5 06:31 log.20110705063100
-rw-r--r--  1 root  wheel  106 Jul  6 06:31 log.20110706063100
-rw-r--r--  1 root  wheel  125 Jul  7 06:31 log.20110707063101
-rw-r--r--  1 root  wheel  113 Jul  8 06:31 log.20110708063100
-rw-r--r--  1 root  wheel  116 Jul  9 06:31 log.20110709063100
-rwxr-xr-x  1 root  wheel  187 May 28 06:51 wrap
```
(The above output has actually been taken from a different cron job, but you get the point.)