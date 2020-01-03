/*
 * cronjobber.c -- cron job wrapper
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

char *revision = "$semver: 1.6.1 $";

extern char **environ;

/* env_special: special environment variables */
char *env_special[] = {
	"HOME",
	"LOGNAME",
	"PATH",
	"SHELL",
	"USER",
	"WEXITSTATUS",
	"WTERMSIG",
	"WSTATUS",
	NULL
};

/* element: component of linked list */
struct element {
	struct element *next;
	void *data;
};

char *progname;
char *jobname = NULL;			/* short name for this job */;
long age = 0;				/* max log file age (seconds) */
char *job = NULL;			/* executable + cmdline args  */
char *cd = NULL;			/* change to dir before invoking job */
char *sender = NULL;			/* mail From address */
mode_t mask = (mode_t)-1;		/* umask */
struct element *env_list = NULL;	/* list of environment variables */
char *sendmail = NULL;			/* path to sendmail */
unsigned int maxbackoff = 0;		/* max random delay seconds */
char *statedir = NULL;			/* lock and log directory */
char *msg_crashed = "crashed";		/* email subject for crashed job */
char *msg_failed = "failed";		/* email subject for failed job*/
unsigned int timeout = 0;		/* job timeout in seconds */
int sig = SIGTERM;			/* signal to send job after timeout */
struct element *recip_list = NULL;	/* email recipient address */
char *checker = NULL;			/* output checker script */
int lockfd;				/* fd of lock file */
pid_t jobpid;				/* pid of cron job */
int jobstatus;				/* cron job exit status */

/* print_args: print command line arguments for debugging */
void
print_args(void)
{
	struct element *p;
                
	printf("revision: %s\n", revision);
	printf("jobname: %s\n", jobname ? jobname : "<not set>");
	printf("statedir: %s\n", statedir ? statedir : "<not set>");
	printf("job: %s\n", job ? job : "<not set>");
	printf("chdir: %s\n", cd ? cd : "<not set>");
	printf("max random delay: %u\n", maxbackoff);
	printf("checker: %s\n", checker ? checker : "<default>");
	for (p = env_list; p; p = p->next)
		printf("env: %s\n", (char *)p->data);
	printf("umask: %03o\n", mask);
	printf("timeout: %u\n", timeout);
	printf("sig = %u\n", (unsigned)sig);
	printf("email subject crashed: %s\n", msg_crashed);
	printf("email subject failed: %s\n", msg_failed);
	printf("email from: %s\n", sender ? sender : "<not set>");
	for (p = recip_list; p; p = p->next)
		printf("email to: %s\n", (char *)p->data);
	printf("sendmail: %s\n", sendmail ? sendmail : "<not set>");
	printf("age: %ld\n", age);
}
                
/* usage: print usage and optional error message */
void
usage(int exit_val, char *fmt, ...)
{
	va_list ap;
	
	fprintf(
		stderr,
		"revision: %s\n"
		"usage: %s\n"
		"	-n jobname\n"
		"	-s statedir\n"
		"	-c \"/path/to/job [args...]\"\n"
		"	[-D /path/to/chdir]\n"
		"	[-r maxbackoff]\n"
		"	[-x /path/to/checker]\n"
		"	[-e var=val]\n"
		"	[-m umask]\n"
		"	[-t timeout]\n"
		"	[-k signo]\n"
		"	[-5 \"crashed subject\"]\n"
		"	[-S \"failed subject\"]\n"
		"	[-F from]\n"
		"	[-T to]\n"
		"	[-M /path/to/sendmail]\n"
		"	[-a age [s|m|h|d]]\n"
		"	[-l]\n"
		"	[-h]\n"
		"	[-V]\n"
		"where:\n"
		"	statedir = directory holding lock and log files\n"
		"	/path/to/chdir = change to this directory to run job\n"
		"	maxbackoff = max random delay (s) before job start [0]\n"
		"	/path/to/checker = script to check job output\n"
		"	var=val = environment variable to set for job\n"
		"	umask = umask to set before invoking job\n"
		"	timeout = job timeout in seconds\n"
		"	signo = signal to send to job (default SIGTERM)\n"
		"	from = from address\n"
		"	to = to address\n"
		"	age = how long to keep log files\n",
		revision,
		progname
	);
	if (fmt) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		fputc('\n', stderr);
		va_end(ap);
	}
	exit(exit_val);
}

/* die: print error message and exit with failure status */
void
die(char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: %s: ", progname, jobname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (fmt[strlen(fmt)-1] == ':')
		fprintf(stderr, " %s", strerror(errno));
	fputc('\n', stderr);
	exit(1);
}

/* product: multiply, but don't overflow */
long
product(long a, long b)
{
	if (b > LONG_MAX / a)
		return -1;	/* a*b would overflow */
	return a*b;
}

/* parse_age: parse command line age specification */
void
parse_age(char *s)
{
	char *endptr;

	errno = 0;
	age = strtol(s, &endptr, 0);
	if (age < 0 || errno == EINVAL || errno == ERANGE)
		usage(1, "age value out of range");
	while (*endptr == ' ' || *endptr == '\t')
		++endptr;
	switch (*endptr) {
	case 'm':
		age = product(age, 60);
		break;
	case 'h':
		age = product(age, 60*60);
		break;
	case 'd':
		age = product(age, 24*60*60);
		break;
	case 's':
	case '\0':
		break;
	default:
		usage(1, "age unit must be s, m, h or d");
		break;
	}
	if (age == -1)
		usage(1, "age value out of range");
}

/* parse_mask: parse command line mask value */
void
parse_mask(char *s)
{
	unsigned long u;

	errno = 0;
	u = strtoul(s, NULL, 0);
	if (errno == EINVAL || errno == ERANGE || u > 0777)
		usage(1, "umask out of range");
	mask = u;
}

/* parse_backoff: parse backoff command line argument */
unsigned int
parse_backoff(char *s)
{
	unsigned long l;

	errno = 0;
	l = strtoul(s, NULL, 0);
	if (errno == EINVAL || errno == ERANGE || l > UINT_MAX)
		usage(1, "backoff value out of range");
	return (unsigned int)l;
}

/* parse_timeout: parse timeout command line argument */
unsigned int
parse_timeout(char *s)
{
	long t;

	errno = 0;
	t = strtol(s, NULL, 0);
	if (t < 0 || errno == EINVAL || errno == ERANGE || t > UINT_MAX)
		usage(1, "timeout value out of range");
	return (unsigned int)t;
}

/* lookup_home: figure out user's home directory */
char *
lookup_home(void)
{
	char *home;
	struct passwd *p;
	uid_t euid;

	if ((home = getenv("HOME")) != NULL)
		return home;
	euid = geteuid();
	errno = 0;
	if ((p = getpwuid(euid)) == NULL) {
		if (errno != 0)
			die("lookup_home: getpwuid %lu:", (unsigned long)euid);
		else
			die(
				"lookup_home: no uid %lu in /etc/passwd",
				(unsigned long)euid
			);
	}
	if ((home = strdup(p->pw_dir)) == NULL)
		die("lookup_home: out of memory");
	return home;
}

/* list_add: add element to linked list */
void
list_add(struct element **listp, void *data)
{
	struct element *newp;

	if ((newp = malloc(sizeof(struct element))) == NULL)
		die("list_add: out of memory saving environment variables");
	newp->data = data;
	newp->next = *listp;
	*listp = newp;
}

/* find_recip: figure out default email recipient */
void
find_recip(void)
{
	char *s;

	if ((s = getenv("USER")) == NULL)
		if ((s = getenv("LOGNAME")) == NULL)
			die("find_recip: no USER or LOGNAME");
	list_add(&recip_list, s);
}

/* ignoresigpipe: we check write() return values */
void
ignoresigpipe(void)
{
	struct sigaction sa;

	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGPIPE, &sa, NULL) == -1)
		die("ignoresigpipe: cannot ignore SIGPIPE:");
}

/* cloexec: set fd close-on-exec flag */
void
cloexec(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFD, 0)) == -1)
		die("cloexec: cannot get file flags:");
	flags |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFD, flags) == -1)
		die("cloexec: cannot set file flags:");
}

/* getlock: get exclusive lock within state directory */
void
getlock(void)
{
	struct flock l;

	if ((lockfd = open("lock", O_RDWR|O_CREAT, 0664)) == -1)
		die("getlock: cannot open lock file `%s/lock':", statedir);
	l.l_type = F_WRLCK;
	l.l_start = 0;
	l.l_whence = SEEK_SET;
	l.l_len = 0;
	if (fcntl(lockfd, F_SETLK, &l) == -1)
		die(
			"getlock: cannot get exclusive lock on `%s/lock:",
			statedir
		);
	cloexec(lockfd);
}

/* write_chunk: keep trying write */
int
write_chunk(int fd, char *buf, size_t size)
{
	ssize_t wrote;

	while (size > 0) {
		wrote = write(fd, buf, size);
		if (wrote == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += wrote;
		size -= wrote;
	}
	return 0;
}

/* fdcopy: copy data from fd to fd */
int
fdcopy(int src, int dst)
{
	char buf[PIPE_BUF];	/* PIPE_BUF not really significant, */
				/* but we have to pick some size */
	ssize_t size;

	while ((size = read(src, buf, sizeof buf)) != 0) {
		if (size == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (write_chunk(dst, buf, size) == -1)
			return -1;
	}
	return 0;
}

/* waitfor: wait for pid to exit or be signalled, only */
int
waitfor(pid_t pid, int *statp)
{
	int res;

	for (;;) {
		res = waitpid(pid, statp, 0);
		if (res == 0)
			continue;
		if (res == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (WIFEXITED(*statp) || WIFSIGNALED(*statp))
			return 0;
		/* don't care about stopping and continuing */
	}
}

/* write_string: */
int
write_string(int fd, char *s)
{
	return write_chunk(fd, s, strlen(s));
}

/* sendlog: mail log file */
void
sendlog(char *subject)
{
	int pfd[2];
	pid_t pid;
	int waitstat;
	int argc;
	char **argv;
	int logfd;
	struct element *p;

	if (pipe(pfd) == -1)
		die("sendlog: cannot create pipe:");
	if ((pid = fork()) == -1)
		die("sendlog: cannot fork:");
	if (pid == 0) {	/* child */
		close(pfd[1]);	/* close write side of pipe */

		/* allocate argv for sendmail command line */
		/* need room for path, -f, sender, recip, [...,] NULL */
		argc = 4;	/* path, -f, sender, NULL */
		for (p = recip_list; p; p = p->next)
			++argc; /* room for each recipient */
		if ((argv = (char **)malloc(argc * sizeof(char *))) == NULL)
			die("sendlog: out of memory");

		/* construct the command line argv[] */
		argc = 0;
		argv[argc] = strrchr(sendmail, '/');
		if (argv[argc] == NULL)
			argv[argc] = sendmail;
		++argc;
		if (sender != NULL) {
			argv[argc++] = "-f";
			argv[argc++] = sender;
		}
		for (p = recip_list; p; p = p->next)
			argv[argc++] = (char *)p->data;

		argv[argc++] = NULL;

		/* arrange for pfd[0] to be stdin */
		if (pfd[0] != STDIN_FILENO) {
			dup2(pfd[0], STDIN_FILENO);
			close(pfd[0]);
		}
		/* stdout and stderr not changed */

		/* kick off sendmail */
		execvp(sendmail, argv);
		/* can't call die() because it calls exit() */
		fprintf(
			stderr,
			"%s: exec: %s: %s\n",
			progname,
			sendmail,
			strerror(errno)
		);
		_exit(127);
	}

	/* parent */
	close(pfd[0]);	/* close read end of pipe */

	/* open log file so we can copy it to sendmail */
	if ((logfd = open("log", O_RDONLY)) == -1)
		die("sendlog: cannot open log:");

	/* write subject and contents of log file to sendmail */
	if (
		write_string(pfd[1], "Subject: ") == -1 ||
		write_string(pfd[1], jobname) == -1 ||
		write_string(pfd[1], ": ") == -1 ||
		write_string(pfd[1], subject) == -1 ||
		write_string(pfd[1], "\n\n") == -1
	) {
		die("sendlog: cannot write to sendmail:");
	}
	if (fdcopy(logfd, pfd[1]))
		die("sendlog: cannot write log to sendmail:");
	close(pfd[1]);

	/* wait for sendmail to go away */
	if (waitfor(pid, &waitstat) == -1)
		die("sendlog: waitpid:");
	if (WIFSIGNALED(waitstat))
		die(
			"sendlog: sendmail killed with signal %d",
			WTERMSIG(waitstat)
		);
	if (WEXITSTATUS(waitstat) != 0)
		die(
			"sendlog: sendmail exitted non-zero = %d",
			WEXITSTATUS(waitstat)
		);
	/* waitfor() only returns if exitted or signalled */

	close(logfd);
}

/* rotate_log: move "log" to "log.YYYYMMDDHHMMSS" */
void
rotate_log(time_t stamp)
{
	char buf[19];	/* log.YYYYMMDDHHMMSS */

	if (age == 0) {
		if (unlink("log") != -1)
			return;
		die("rotate_log: cannot remove `%s/log':", statedir);
	}
	if (strftime(buf, sizeof buf, "log.%Y%m%d%H%M%S", gmtime(&stamp)) == 0)
		die("rotate_log: strftime failed");
	if (rename("log", buf) == -1)
		die("rotate_log: cannot rename `log' to `%s':", buf);
}

/* stale_log: clean up previously unfinished job */
void
stale_log(void)
{
	struct stat s;

	if (stat("log", &s) == -1) {
		if (errno == ENOENT)
			return;	/* ok, previous job completed */
		die("stale_log: cannot stat `%s/log':", statedir);
	}
	sendlog(msg_crashed);
	rotate_log(s.st_mtime);
}

/* clean_logs: remove logs older than age seconds */
void
clean_logs(void)
{
	DIR *d;
	struct dirent *entry;
	struct stat s;
	time_t now;

	if ((now = time(NULL)) == (time_t)-1)
		die("clean_logs: time:");
	if ((d = opendir(".")) == NULL)
		die("clean_logs: opendir:");
	while ((entry = readdir(d)) != NULL) {
		if (strncmp(entry->d_name, "log.", 4) == 0) {
			if (stat(entry->d_name, &s) == -1)
				die("clean_logs: cannot stat `%s':");
			if (s.st_mtime >= now-age)
				continue;
			if (unlink(entry->d_name) == -1)
				die(
					"clean_logs: cannot remove `%s':",
					entry->d_name
				);
		}
	}
	closedir(d);
}

/* catch_alarm: SIGALRM handler */
void
catch_alarm(int unused)
{
	if (timeout > 0)
		kill(jobpid, sig);
}

/* prefix_of: true if s starts with "special=" or "special\0" */
int
prefix_of(char *special, char *s)
{
	for (; *special != '\0'; ++special)
		if (*special != *s++)
			return 0;
	return (*s == '=' || *s == '\0');
}

/* get_env: get var=val string from environ */
char *
get_env(char *var)
{
	char **cpp;

	for (cpp = environ; *cpp; ++cpp)
		if (prefix_of(var, *cpp))
			return *cpp;
	return NULL;
}

/* build_envp: set up environment for job */
char **
build_envp(void)
{
	char **cpp;
	int found;
	int envc;
	char **envp;
	struct element *p;
	char *val;

	/* look for special variables in env_list. add if not there */
	for (cpp = env_special; *cpp; ++cpp) {
		found = 0;
		for (p = env_list; p; p = p->next)
			if (prefix_of(*cpp, (char *)p->data)) {
				found = 1;
				break;
			}
		if (!found)
			list_add(&env_list, *cpp);
	}

	/* count how many environment variables there are */
	envc = 0;
	for (p = env_list; p; p = p->next)
		++envc;
	++envc;	/* for terminating NULL */

	/* allocate a new envp */
	if ((envp = (char **)malloc(envc * sizeof(char *))) == NULL)
		die("build_envp: out of memory");

	/* populate envp */
	cpp = envp;
	for (p = env_list; p; p = p->next) {
		if (strchr((char *)p->data, '=') != NULL)
			*cpp++ = (char *)p->data;
		else if ((val = get_env((char *)p->data)) != NULL)
			*cpp++ = val;
	}
	*cpp++ = NULL;

	return envp;
}

/* backoff: sleep for a random number of seconds */
void
backoff(void)
{
	unsigned long r;
	int fd;

	if (maxbackoff == 0)
		return;
	/* we don't need cryptographic quality randomness; we only need to
	 * introduce some jitter.
	 */
	r = (unsigned long)getpid();
	if ((fd = open("/dev/urandom", O_RDONLY)) != -1) {
		(void)read(fd, (void *)&r, sizeof r);
		close(fd);
	}
	sleep(r % maxbackoff);	/* don't care if we return early or in error */
}

/* run_job: kick off job with output to log and optional timeout */
void
run_job(void)
{
	int logfd;
	struct sigaction sa;
	pid_t ret;
	char *shell;
	char **envp;

	if ((logfd = open("log", O_RDWR|O_CREAT, 0644)) == -1)
		die("run_job: cannot create log:");
	backoff();

	if ((jobpid = fork()) == -1)
		die("run_job: fork failed:");
	if (jobpid == 0) {	/* child */
		if (chdir(cd) == -1)
			die("run_job: cannot chdir `%s':", cd);
		envp = build_envp();
		/* arrange for logfd to be stdout and stderr */
		if (logfd != STDOUT_FILENO)
			dup2(logfd, STDOUT_FILENO);
		if (logfd != STDERR_FILENO)
			dup2(logfd, STDERR_FILENO);
		close(logfd);
		/* stdin not changed */

		if (mask != (mode_t)-1)
			umask(mask);

		if ((shell = getenv("SHELL")) == NULL)
			shell = "/bin/sh";

		execle(shell, shell, "-c", job, NULL, envp);
		fprintf(
			stderr,
			"%s: cannot run `%s -c \"%s\"': %s\n",
			progname,
			shell,
			job,
			strerror(errno)
		);
		_exit(127);
	}

	close(logfd);
	if (timeout > 0) {
		sa.sa_handler = catch_alarm;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		if (sigaction(SIGALRM, &sa, NULL) == -1)
			die("run_job: cannot set alarm handler");
		alarm(timeout);
	}

	for (;;) {
		ret = waitpid(jobpid, &jobstatus, 0);
		if (ret == -1 && errno == EINTR)
			continue;
		if (ret == -1)
			die("run_job: waitpid:");
		if (WIFEXITED(jobstatus) || WIFSIGNALED(jobstatus))
			break;
	}

	if (timeout > 0) {
		alarm(0);
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		(void)sigaction(SIGALRM, &sa, NULL);
	}

	/* exit status left in jobstatus for checker */
}

/* export_status: put job exit status in checker's environment */
int
export_status(void)
{
	char buf[100];
	size_t size;

	if (WIFEXITED(jobstatus))
		size = snprintf(buf, sizeof buf, "%d", WEXITSTATUS(jobstatus));
	else if (WIFSIGNALED(jobstatus))
		size = snprintf(buf, sizeof buf, "%d", WTERMSIG(jobstatus));
	else
		size = snprintf(
			buf,
			sizeof buf,
			"%0x04d",
			(unsigned int)jobstatus
		);
	if (size >= sizeof buf) {
		errno = ENOMEM;
		return -1;
	}

	(void)unsetenv("WTERMSIG");
	(void)unsetenv("WEXITSTATUS");
	(void)unsetenv("WSTATUS");

	if (WIFEXITED(jobstatus)) {
		if (setenv("WEXITSTATUS", buf, 1) == -1)
			return -1;
	} else if (WIFSIGNALED(jobstatus)) {
		if (setenv("WTERMSIG", buf, 1) == -1)
			return -1;
	} else {
		if (setenv("WSTATUS", buf, 1) == -1)
			return -1;
	}

	return 0;
}

/* run_checker: run checker script to look at job output log */
int
run_checker(void)
{
	pid_t pid;
	int logfd;
	int status;
	char **envp;

	if ((pid = fork()) == -1)
		die("run_checker: cannot fork:");
	if (pid == 0) {	/* child */
		if (export_status() == -1) {
			fprintf(
				stderr,
				"%s: check_job: cannot export status: %s\n",
				progname,
				strerror(errno)
			);
			_exit(127);
		}
		envp = build_envp();
		if ((logfd = open("log", O_RDONLY)) == -1) {
			fprintf(
				stderr,
				"%s: check_job: cannot open log: %s\n",
				progname,
				strerror(errno)
			);
			_exit(127);
		}
		/* arrange for log to be stdin */
		if (logfd != STDIN_FILENO) {
			dup2(logfd, STDIN_FILENO);
			close(logfd);
		}
		/* stdout and stderr not changed */

		/* kick off checker */
		execle(checker, checker, NULL, envp);
		fprintf(
			stderr,
			"%s: exec: %s: %s\n",
			progname,
			checker,
			strerror(errno)
		);
		_exit(127);
	}
	if (waitfor(pid, &status) == -1)
		die("run_checker: waitpid:");
	if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
		return 0;
	return 1;
}

/* check_job: run checker and possibly mail log */
void
check_job(void)
{
	int ok = 1;
	struct stat s;

	if (checker)
		ok = run_checker();
	else if (!(WIFEXITED(jobstatus) && WEXITSTATUS(jobstatus) == 0))
		ok = 0;
	else if (stat("log", &s) == -1)
		die("check_job: stat `log':");
	else if (s.st_size > 0)
		ok = 0;

	if (!ok)
		sendlog(msg_failed);
}

void
parse_cmdline(int argc, char **argv)
{
	int list = 0;
	int c;

	while ((c = getopt(argc, argv, "5:a:c:D:e:F:hk:lm:M:n:r:s:S:t:T:Vx:")) != -1)
		switch (c) {
		case '5':
			msg_crashed = optarg;
			break;
		case 'a':
			parse_age(optarg);
			break;
		case 'c':
			job = optarg;
			break;
		case 'D':
			cd = optarg;
			break;
		case 'e':
			list_add(&env_list, optarg);
			break;
		case 'F':
			sender = optarg;
			break;
		case 'h':
			usage(0, NULL);
			break;
		case 'k':
			sig = atoi(optarg);
			if (sig < 1)
				usage(1, "signal must be > 0");
			break;
		case 'l':
			list = 1;
			break;
		case 'm':
			parse_mask(optarg);
			break;
		case 'M':
			sendmail = optarg;
			break;
		case 'n':
			jobname = optarg;
			break;
		case 'r':
			maxbackoff = parse_backoff(optarg);
			break;
		case 's':
			statedir = optarg;
			break;
		case 'S':
			msg_failed = optarg;
			break;
		case 't':
			timeout = parse_timeout(optarg);
			break;
		case 'T':
			list_add(&recip_list, optarg);
			break;
		case 'V':
			printf("%s\n", revision);
			exit(0);
		case 'x':
			checker = optarg;
			break;
		default:
			usage(1, NULL);
			break;
		}
	if (jobname == NULL || statedir == NULL || job == NULL)
		usage(1, "must provide at least -n, -s  and -c");
	if (cd == NULL)
		cd = lookup_home();
	if (sendmail == NULL)
		sendmail = getenv("MAILER");
	if (sendmail == NULL)
		sendmail = "sendmail";
	if (recip_list == NULL)
		find_recip();
	if (list) {
		print_args();
		exit(0);
	}
}

int
main(int argc, char *argv[])
{
	time_t start_time;

	progname = argv[0];
	if ((start_time = time(NULL)) == (time_t)-1)
		die("main: time:");
	ignoresigpipe();
	parse_cmdline(argc, argv);
	if (chdir(statedir) == -1)
		die("main: cannot change to state directory `%s':", statedir);
	getlock();
	stale_log();
	run_job();
	check_job();
	rotate_log(start_time);
	clean_logs();
	exit(0);
}
