// init: The initial user-level program.
// Called by an exec() system call in initcode.S.
// Creates a new console device file, if needed,
// and then opens it as file descriptors 0, 1, and 2.
// Then it loops, starting a console shell, handles
// orphaned zombies until the shell exits, and repeats.
// The system is up!

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
	int pid, wpid;

	if (open("console", O_RDWR) < 0)
	{
		mknod("console", 1, 1);
		open("console", O_RDWR);
	}
	dup(0);		// stdout
	dup(0);		// stderr

	for ( ; ; )
	{
		printf(1, "init: starting sh\n");
		pid = fork();
		if (pid < 0)
		{
			printf(1, "init: fork failed\n");
			exit();
		}
		if (pid == 0)
		{
			exec("sh", argv);
			printf(1, "init: exec sh failed\n");
			exit();
		}
		while ((wpid = wait()) >= 0 && wpid != pid)
			printf(1, "zombie!\n");
	}
}
