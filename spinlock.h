// Mutex lock
struct spinlock {
	uint locked;		// Is the lock held?

	// FOR DEBUGGING:
	char *name;			// Name of lock
	struct cpu *cpu;	// The CPU holding the lock
	uint pcs[10];		// The call stack (an array of
						//	program counters) that
						//	locked the lock
}
