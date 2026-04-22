/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

// HINT 1 : TRAPHANDLER_NOEC(t_divide, T_DIVIDE);
//          Do something like this if there is no error code for the trap
// HINT 2 : TRAPHANDLER(t_dblflt, T_DBLFLT);
//          Do something like this if the trap includes an error code..
// HINT 3 : READ Intel's manual to check if the trap includes an error code
//          or not...



// LLM Generated Prompt: check if what i have written is correct 

/* Trap handlers that don't push error codes */
TRAPHANDLER_NOEC(t_divide, T_DIVIDE);
TRAPHANDLER_NOEC(t_debug, T_DEBUG);
TRAPHANDLER_NOEC(t_nmi, T_NMI);
TRAPHANDLER_NOEC(t_brkpt, T_BRKPT);
TRAPHANDLER_NOEC(t_oflow, T_OFLOW);
TRAPHANDLER_NOEC(t_bound, T_BOUND);
TRAPHANDLER_NOEC(t_illop, T_ILLOP);
TRAPHANDLER_NOEC(t_device, T_DEVICE);
TRAPHANDLER_NOEC(t_fperr, T_FPERR);
TRAPHANDLER_NOEC(t_align, T_ALIGN);
TRAPHANDLER_NOEC(t_mchk, T_MCHK);
TRAPHANDLER_NOEC(t_simderr, T_SIMDERR);
TRAPHANDLER_NOEC(t_syscall, T_SYSCALL);

/* Trap handlers that push error codes */
TRAPHANDLER(t_dblflt, T_DBLFLT);
TRAPHANDLER(t_tss, T_TSS);
TRAPHANDLER(t_segnp, T_SEGNP);
TRAPHANDLER(t_stack, T_STACK);
TRAPHANDLER(t_gpflt, T_GPFLT);
TRAPHANDLER(t_pgflt, T_PGFLT);

/*
 * Lab 4: IRQ handlers
 */
TRAPHANDLER_NOEC(t_irq0, IRQ_OFFSET + 0)
TRAPHANDLER_NOEC(t_irq1, IRQ_OFFSET + 1)
TRAPHANDLER_NOEC(t_irq2, IRQ_OFFSET + 2)
TRAPHANDLER_NOEC(t_irq3, IRQ_OFFSET + 3)
TRAPHANDLER_NOEC(t_irq4, IRQ_OFFSET + 4)
TRAPHANDLER_NOEC(t_irq5, IRQ_OFFSET + 5)
TRAPHANDLER_NOEC(t_irq6, IRQ_OFFSET + 6)
TRAPHANDLER_NOEC(t_irq7, IRQ_OFFSET + 7)
TRAPHANDLER_NOEC(t_irq8, IRQ_OFFSET + 8)
TRAPHANDLER_NOEC(t_irq9, IRQ_OFFSET + 9)
TRAPHANDLER_NOEC(t_irq10, IRQ_OFFSET + 10)
TRAPHANDLER_NOEC(t_irq11, IRQ_OFFSET + 11)
TRAPHANDLER_NOEC(t_irq12, IRQ_OFFSET + 12)
TRAPHANDLER_NOEC(t_irq13, IRQ_OFFSET + 13)
TRAPHANDLER_NOEC(t_irq14, IRQ_OFFSET + 14)
TRAPHANDLER_NOEC(t_irq15, IRQ_OFFSET + 15)

/*
 * Lab 3: Your code here for _alltraps
 */
 // LLM: Help me with the _alltraps function
_alltraps:

	pushl %ds
	pushl %es
	
	pushal
	

	movw $GD_KD, %ax
	movw %ax, %ds
	movw %ax, %es
	

	pushl %esp
	
	call trap
	
	addl $4, %esp   // Remove pushed esp
	popal           // Restore general purpose registers
	popl %es        // Restore es
	popl %ds        // Restore ds
	addl $8, %esp   // Skip trapno and err
	iret            // Return from interrupt


