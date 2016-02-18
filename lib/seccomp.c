/*
 *	Copyright (c) 2016 Aki Nyrhinen
 *
 *	Permission is hereby granted, free of charge, to any person obtaining a copy
 *	of this software and associated documentation files (the "Software"), to deal
 *	in the Software without restriction, including without limitation the rights
 *	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *	copies of the Software, and to permit persons to whom the Software is
 *	furnished to do so, subject to the following conditions:
 *
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 *	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *	THE SOFTWARE.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <sys/prctl.h>
#include <linux/unistd.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define nelem(x) (int)(sizeof(x)/sizeof(x[0]))

static struct sock_filter filter[] = {

	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, nr)),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_rt_sigreturn, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_exit_group, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_fork, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_exit, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_read, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_write, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_execve, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_brk, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_access, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_mmap, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_open, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_close, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_poll, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_arch_prctl, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_ioctl, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_getuid, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_getpid, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_getppid, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_rt_sigaction, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_uname, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_getcwd, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_fcntl, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_getpgrp, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_getpgid, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_setpgid, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_geteuid, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_wait4, 0, 1),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),

	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL)
};

static struct sock_fprog prog = {
	.len = nelem(filter),
	.filter = filter,
};

int
seccomp(void)
{
	if(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)){
		fprintf(stderr, "prctl PR_SET_NO_NEW_PRIVS: %s\n", strerror(errno));
		return -1;
	}

	if(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)){
		fprintf(stderr, "prctl PR_SET_SECCOMP SECCOMP_MODE_FILTER: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}
