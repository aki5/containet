typedef struct Args Args;
struct Args {
	int argc;
	char **argv;
	char *root;
	char *toproot;
	char *topwork;
	char *ip4addr;
	char *identity;
	int ctrlsock; // domain socket to switch
	char *postname;
};


int runcontainer(Args *Args, int cloneflags);
