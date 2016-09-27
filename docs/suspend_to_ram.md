# Suspend to RAM

## Scope
Currently suspend-to-RAM is implement only for x86-64.  The implementation is
alpha-quality.  We currently do not restore peripherals or the external
interrupt controller.  It can only be used on a system with only one
active CPU (though this is easily handled by hot-unplugging all CPUs except CPU
0.

## X86-64

### Implementation Constraints
We use the ACPICA library to handle performing the ACPI procedure for transition
to S3.  Upon return to S0, we resume executing in 16-bit real mode at an IP of our choice.
All other CPU state is lost, but RAM still has its previous contents.   We have a stranger
than usual resume path, due to our AML interpreter being in userspace (in the
acpisvc process).  Since the last instruction we execute is in userspace, the
kernel cannot know the final thread's state, and so that thread must be
responsible for restoring it.  Furthermore, that thread needs to finish the
ACPI resume process before interrupts are re-enabled in the kernel.

### The Suspend Path
An process with the appropriate handle can send acpisvc an *s\_state\_transition* command.
Upon receiving this, acpisvc executes *perform\_suspend*.  This disables interrupts
and performs an *mx\_acpi\_prepare\_for\_suspend* syscall to setup the real-mode return path.
The kernel is reponsible for setting up a resume trampoline that will restore
the CPU's privileged state, re-initialize core system peripherals (e.g.
interrupt controllers), and transfer control flow back to acpisvc.  After this
syscall, acpisvc configures the real-mode wake IP using a value returned by the
syscall.  It then performs the ACPI S-state transition process, and just before
issuing the final transition call, saves all of its registers to a static buffer
in its address space (see *x86\_do\_suspend*).

### The Resume Path
The system resumes in 16-bit real mode at the wake IP.  The kernel transitions
back to 64-bit mode and switches to the kernel's address space (see
*x86\_bootstrap16\_entry*, *_x86\_suspend\_wakeup*, and *x86\_suspend\_wakeup*).
It sets its stack to a statically allocated buffer that is only used for this purpose.
It reinitializes the serial port, for debug use.  It reinitializes the CPU state,
including MMU configuration and feature-enable bits.
It also reconfigures the APIC (including its timer).  After this, it switches to
the acpisvc address space and transfers control flow to an address given by
acpisvc in the *mx\_acpi\_prepare\_for\_suspend* syscall.  Note that since we
stopped execution inside of the acpisvc, it's important that we do not touch the
scheduler until we're back in our original acpisvc execution context.  acpisvc's
resume IP is *x86\_suspend\_resume*.  It restores the process's general purpose
registers and returns to *perform\_suspend*.  It then finishes restoring its
context, finishes the ACPI return-from-S3 procedure, and re-enables interrupts.
The system will potentially re-enter the scheduler anytime after this, during
either an interrupt or a blocking syscall.

### Known issues
- Systems that use TSC Deadline mode for one-shot timers may hang on resume, due
  to races around restoring the TSC offset that need to be resolved.
- Systems that use the PIT for wall-time **will** hang on resume, due to the PIT
  and IOAPIC not being restored.
- Suspend will go poorly if invoked with multiple running CPUs.  We need to
  route control over CPU hotplug to usermode to allow acpisvc to handle this
  gracefully.
- Most peripherals are not restored.
