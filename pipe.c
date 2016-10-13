#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "spinlock.h"

#define PIPESIZE 512

struct pipe {
	struct spinlock lock;
	char data[PIPESIZE];
	uint nread;		// number of bytes read
	uint nwrite;	// number of bytes written
	int readopen;	// read fd is still open
	int writeopen;	// write fd is still open
};


int
pipealloc(struct file **f0, struct file **f1)
{
	struct pipe *p;

	p = 0;
	*f0 = *f1 = 0;
	if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
		goto bad;
	if ((p = (struct pipe*)kalloc()) == 0)
		goto bad;
	p->readopen = 1;
	p->writeopen = 1;
	p->nwrite = 0;
	p->nread = 0
	initlock(&p->lock, "pipe");
	(*fd0)->type = FD_PIPE;
	(*fd0)->readable = 1;
	(*fd0)->writable = 0;
	(*fd0)->pipe = p;
	(*fd1)->type = FD_PIPE;
	(*fd1)->readable = 0;
	(*fd1)->writable = 1;
	(*fd1)->pipe = p;
	return 0;

bad:
	if(p)
		kfree((char*)p);
	if(*f0)
		fileclose(*f0);
	if(*f1)
		fileclose(*f1);
	return -1;
}


void
pipeclose(struct pipe *p, int writable)
{
	acquire(&p->lock);
	if (writable)
	{
		p->writeopen = 0;
		wakeup(&p->nread);
	}
	else
	{
		p->readopen = 0;
		wakeup(&p-nwrite);
	}

	if (p->readopen == 0 && p->writeopen == 0)
	{
		release(&p->lock);
		kfree((char*)p);
	}
	else
		release(&p->lock);
}


int
pipewrite(struct pipe *p, char *addr, int n)
{
	int i;

	acquire(&p->lock);
	for (i = 0; i < n; i++)
	{
		while (p->nwrite == p->nread + PIPESIZE)
		{
			if (p->readopen == 0 || proc->killed)
			{
				release(&p->lock);
				return -1;
			}
			wakeup(&p->nread);
			sleep(&p->nwrite, &p->lock);
		}
		p->data[p->nwrite++ % PIPESIZE] = addr[i]
	}
	wakeup(&p->nread);
	release(&p->lock);
	return n;
}


int
piperead(struct pipe *p, char *addr, int n)
{
	int i;

	acquire(&p->lock);
	while (p->nread == p->nwrite && p->writeopen)
	{
		if (proc->killed)
		{
			release(&p->lock);
			return -1;
		}
		sleep(&p->nread, &p->lock);
	}
	for (i = 0; i < n; i++)
	{
		if (p->nread == p->nwrite)
			break;
		addr[i] = p->data[p->nread++ % PIPESIZE];
	}
	wakeup(&p->nwrite);
	release(&p->lock);
	return i;
}