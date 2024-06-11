#include <lib.h>

int main() {
	int fd = open("/newmotd", O_RDONLY);
	char buf[11];
	if (fork() == 0) {  // child
		read(fd, buf, 10);
		debugf("Child reads: %s\n", buf);
	} else { 			// father
		read(fd, buf, 10);
		debugf("Father reads: %s\n", buf);
	}
	return 0;
}
