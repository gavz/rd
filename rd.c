/* rd - privilege elevator
 * Copyright (C) 2022 FearlessDoggo21
 * see LICENCE file for licensing information */

#include <crypt.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

noreturn static void die(const char *fmt, ...);
static char *readpw(void);

noreturn static void
die(const char *fmt, ...)
{
	/* perror if last char not '\n' */
	if (fmt[strlen(fmt) - 1] == '\n') {
		va_list ap;
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	} else {
		perror(fmt);
	}
	exit(1);
}

static char *
readpw(void)
{
	printf("rd: enter passwd: ");
	fflush(stdout);

	/* termios to not echo typed chars (hide passwd) */
	struct termios term;
	if (tcgetattr(STDIN_FILENO, &term) == -1)
		die("\nrd: unable to get terminal attributes");

	term.c_lflag &= ~ECHO;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) == -1)
		die("\nrd: unable to set terminal attributes");

	/* read loop with buffer reallocation for long passwds */
	size_t length = 0, ret;
	char *passwd = malloc(50);
	while ((ret = read(STDIN_FILENO, passwd + length, 50)) == 50)
		if ((passwd = realloc(passwd, (length += ret) + 50)) == NULL)
			die("\nrd: unable to allocate memory");
	if (ret == (size_t)-1)
		die("\nrd: unable to read from stdin");
	passwd[length + ret - 1] = '\0';

	term.c_lflag |= ECHO;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) == -1)
		die("\nrd: unable to set terminal attributes");
	putchar('\n');
	return passwd;
}

int
main(int argc, char **argv)
{
	if (getuid() != 0 && geteuid() != 0)
		die("rd: insufficient privileges\n");

	struct passwd *pw;
	if ((pw = getpwnam("root")) == NULL)
		die("rd: unable to get passwd file entry");

	/* get hashed passwd from /etc/passwd or /etc/shadow */
	const char *hash;
	if (pw->pw_passwd[0] == '!') {
		die("rd: password is locked\n");
	} else if (!strcmp(pw->pw_passwd, "x")) {
		struct spwd *sp;
		if ((sp = getspnam("root")) == NULL)
			die("rd: unable to get shadow file entry");
		hash = sp->sp_pwdp;
	} else {
		hash = pw->pw_passwd;
	}

	/* if passwd exists (no free login) */
	if (hash[0] != '\0') {
		/* get the salt from the entry */
		const char *salt;
		if ((salt = strdup(hash)) == NULL)
			die("rd: unable to allocate memory");
		char *ptr = strchr(salt + 1, '$');
		ptr = strchr(ptr + 1, '$');
		ptr[1] = '\0';

		/* hash and compare the read passwd to the shadow entry */
		if (strcmp(hash, crypt(readpw(), salt)))
			die("rd: incorrect password\n");
	}

	if (setgid(pw->pw_gid) == -1)
		die("rd: unable to set group id");
	if (setuid(pw->pw_uid) == -1)
		die("rd: unable to set user id");

	execvp(argv[1], &argv[1]);
	die("rd: unable to run %s: %s\n", argv[1], strerror(errno));
}
