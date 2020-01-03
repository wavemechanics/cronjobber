How to Build and Install

* Type `make cronjobber` to build the binary.
* Copy `cronjobber` to a convenient `bin` directory in your `$PATH`.

If you want to install the mandoc man page, do something like this:

```
# cp cronjobber.8.mandoc /usr/local/share/man/man8/cronjobber.8
```

You can render the man page in different output formats, such as PDF:

```
$ make cronjobber.8.pdf
```

Type `make` on its own to see other output formats and targets.
