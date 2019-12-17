typedef struct Args Args;
struct Args {
	int argc;
	char **argv;
	char **environ;
	char *root;
	char *toproot;
	char *topwork;
	char *ip4addr;
	char *identity;
	int ctrlsock; // domain socket to switch
	char *postname;
	char *authtoken;

	// private variables..
	int tube[2];
};


int runcontainer(Args *Args, int cloneflags);
void cleancontainer(Args *ap);
