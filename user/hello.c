#include <lib.h>

int main()
{
	debugf("enter\n");
	int id = fork();
	if (id == 0)
	{
		debugf("child\n");
	}
	else
	{
		debugf("parent, child id = %d\n", id);
	}
	return 0;
}
