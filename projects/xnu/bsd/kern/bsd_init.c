/*
 * Copyright (c) 2000-2020 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 *
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)init_main.c	8.16 (Berkeley) 5/14/95
 */

/*
 *
 * Mach Operating System
 * Copyright (c) 1987 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */
/*
 * NOTICE: This file was modified by McAfee Research in 2004 to introduce
 * support for mandatory and extensible security protections.  This notice
 * is included in support of clause 2.2 (b) of the Apple Public License,
 * Version 2.0.
 */

#include <sys/param.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/mount_internal.h>
#include <sys/proc_internal.h>
#include <sys/kauth.h>
#include <sys/systm.h>
#include <sys/vnode_internal.h>
#include <sys/conf.h>
#include <sys/buf_internal.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/kasl.h>

#include <security/audit/audit.h>

#include <sys/malloc.h>
#include <sys/dkstat.h>
#include <sys/codesign.h>

#include <kern/startup.h>
#include <kern/thread.h>
#include <kern/task.h>
#include <kern/ast.h>
#include <kern/zalloc.h>
#include <kern/ux_handler.h>            /* for ux_handler_setup() */

#include <mach/vm_param.h>

#include <vm/vm_map.h>
#include <vm/vm_kern.h>

#include <sys/reboot.h>
#include <dev/busvar.h>                 /* for pseudo_inits */
#include <sys/kdebug.h>
#include <sys/monotonic.h>

#include <mach/mach_types.h>
#include <mach/vm_prot.h>
#include <mach/semaphore.h>
#include <mach/sync_policy.h>
#include <kern/clock.h>
#include <sys/csr.h>
#include <mach/kern_return.h>
#include <mach/thread_act.h>            /* for thread_resume() */
#include <sys/mcache.h>                 /* for mcache_init() */
#include <sys/mbuf.h>                   /* for mbinit() */
#include <sys/event.h>                  /* for knote_init() */
#include <sys/eventhandler.h>           /* for eventhandler_init() */
#include <sys/kern_memorystatus.h>      /* for memorystatus_init() */
#include <sys/kern_memorystatus_freeze.h> /* for memorystatus_freeze_init() */
#include <sys/aio_kern.h>               /* for aio_init() */
#include <sys/semaphore.h>              /* for psem_cache_init() */
#include <net/dlil.h>                   /* for dlil_init() */
#include <net/kpi_protocol.h>           /* for proto_kpi_init() */
#include <net/iptap.h>                  /* for iptap_init() */
#include <sys/socketvar.h>              /* for socketinit() */
#include <sys/protosw.h>                /* for domaininit() */
#include <kern/sched_prim.h>            /* for thread_wakeup() */
#include <net/if_ether.h>               /* for ether_family_init() */
#include <net/if_gif.h>                 /* for gif_init() */
#include <miscfs/devfs/devfsdefs.h>     /* for devfs_kernel_mount() */
#include <vm/vm_kern.h>                 /* for kmem_suballoc() */
#include <sys/msgbuf.h>                 /* for log_setsize() */
#include <sys/proc_uuid_policy.h>       /* proc_uuid_policy_init() */
#include <netinet/flow_divert.h>        /* flow_divert_init() */
#include <net/content_filter.h>         /* for cfil_init() */
#include <net/necp.h>                   /* for necp_init() */
#include <net/network_agent.h>          /* for netagent_init() */
#include <net/packet_mangler.h>         /* for pkt_mnglr_init() */
#include <net/if_utun.h>                /* for utun_register_control() */
#include <net/if_ipsec.h>               /* for ipsec_register_control() */
#include <net/net_str_id.h>             /* for net_str_id_init() */
#include <net/netsrc.h>                 /* for netsrc_init() */
#include <net/ntstat.h>                 /* for nstat_init() */
#include <netinet/tcp_cc.h>                     /* for tcp_cc_init() */
#include <netinet/mptcp_var.h>          /* for mptcp_control_register() */
#include <net/nwk_wq.h>                 /* for nwk_wq_init */
#include <net/restricted_in_port.h> /* for restricted_in_port_init() */
#include <kern/assert.h>                /* for assert() */
#include <sys/kern_overrides.h>         /* for init_system_override() */
#include <sys/lockf.h>                  /* for lf_init() */
#include <sys/fsctl.h>

#include <net/init.h>

#if CONFIG_MACF
#include <security/mac_framework.h>
#include <security/mac_internal.h>      /* mac_init_bsd() */
#include <security/mac_mach_internal.h> /* mac_update_task_label() */
#endif

#include <machine/exec.h>

#if CONFIG_NETBOOT
#include <sys/netboot.h>
#endif

#if CONFIG_IMAGEBOOT
#include <sys/imageboot.h>
#endif

#if PFLOG
#include <net/if_pflog.h>
#endif


#include <pexpert/pexpert.h>
#include <machine/pal_routines.h>
#include <console/video_console.h>

#if CONFIG_XNUPOST
#include <tests/xnupost.h>
#endif

void * get_user_regs(thread_t);         /* XXX kludge for <machine/thread.h> */
void IOKitInitializeTime(void);         /* XXX */
void IOSleep(unsigned int);             /* XXX */
void loopattach(void);                  /* XXX */

const char *const copyright =
    "Copyright (c) 1982, 1986, 1989, 1991, 1993\n\t"
    "The Regents of the University of California. "
    "All rights reserved.\n\n";

/* Components of the first process -- never freed. */
struct  proc proc0 = { .p_comm = "kernel_task", .p_name = "kernel_task" };
struct  session session0;
struct  pgrp pgrp0;
struct  filedesc filedesc0;
struct  plimit limit0;
struct  pstats pstats0;
struct  sigacts sigacts0;
SECURITY_READ_ONLY_LATE(proc_t) kernproc = &proc0;
proc_t XNU_PTRAUTH_SIGNED_PTR("initproc") initproc;

long tk_cancc;
long tk_nin;
long tk_nout;
long tk_rawcc;

int lock_trace = 0;
/* Global variables to make pstat happy. We do swapping differently */
int nswdev, nswap;
int nswapmap;
void *swapmap;
struct swdevt swdevt[1];

static LCK_GRP_DECLARE(hostname_lck_grp, "hostname");
LCK_MTX_DECLARE(hostname_lock, &hostname_lck_grp);
LCK_MTX_DECLARE(domainname_lock, &hostname_lck_grp);

dev_t   rootdev;                /* device of the root */
dev_t   dumpdev;                /* device to take dumps on */
long    dumplo;                 /* offset into dumpdev */
long    hostid;
char    hostname[MAXHOSTNAMELEN];
char    domainname[MAXDOMNAMELEN];
char    rootdevice[DEVMAXNAMESIZE];

struct  vnode *rootvp;
bool rootvp_is_ssd = false;
int boothowto;
int minimalboot = 0;
#if CONFIG_DARKBOOT
int darkboot = 0;
#endif

#if __arm64__
int legacy_footprint_entitlement_mode = LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE;
#endif /* __arm64__ */

#if PROC_REF_DEBUG
__private_extern__ int proc_ref_tracking_disabled = 0; /* disable panics on leaked proc refs across syscall boundary */
#endif

extern kern_return_t IOFindBSDRoot(char *, unsigned int, dev_t *, u_int32_t *);
extern void IOSecureBSDRoot(const char * rootName);
extern kern_return_t IOKitBSDInit(void );
extern boolean_t IOSetRecoveryBoot(bsd_bootfail_mode_t, uuid_t, boolean_t);
extern void kminit(void);
extern void bsd_bufferinit(void);
extern void oslog_setsize(int size);
extern void throttle_init(void);

#if CONFIG_LOCKERBOOT
#define LOCKER_PROTOBOOT_MOUNT "/protoboot"

const char kernel_protoboot_mount[] = LOCKER_PROTOBOOT_MOUNT;
extern int mount_locker_protoboot(const char *fsname, const char *mntpoint,
    const char *pbdevpath);
#endif

extern int ncl;
#if DEVELOPMENT || DEBUG
extern int syscallfilter_disable;
#endif // DEVELOPMENT || DEBUG

vm_map_t        bsd_pageable_map;
vm_map_t        mb_map;

static  int bsd_simul_execs;
static int bsd_pageable_map_size;
__private_extern__ int execargs_cache_size = 0;
__private_extern__ int execargs_free_count = 0;
__private_extern__ vm_offset_t * execargs_cache = NULL;

void bsd_exec_setup(int);

__private_extern__ int bootarg_execfailurereports = 0;

#if __x86_64__
__private_extern__ int bootarg_no32exec = 1;
#endif
__private_extern__ int bootarg_vnode_cache_defeat = 0;

#if CONFIG_JETSAM && (DEVELOPMENT || DEBUG)
__private_extern__ int bootarg_no_vnode_jetsam = 0;
#endif /* CONFIG_JETSAM && (DEVELOPMENT || DEBUG) */

__private_extern__ int bootarg_no_vnode_drain = 0;

/*
 * Prevent kernel-based ASLR from being used, for testing.
 */
#if DEVELOPMENT || DEBUG
__private_extern__ int bootarg_disable_aslr = 0;
#endif


/*
 * Allow an alternate dyld to be used for testing.
 */

#if DEVELOPMENT || DEBUG
char dyld_alt_path[MAXPATHLEN];
int use_alt_dyld = 0;
extern uint64_t dyld_flags;
#endif

int     cmask = CMASK;
extern int customnbuf;

kern_return_t bsd_autoconf(void);
void bsd_utaskbootstrap(void);

static void parse_bsd_args(void);
#if CONFIG_DEV_KMEM
extern void dev_kmem_init(void);
#endif
extern void select_waitq_init(void);
static void process_name(const char *, proc_t);

static void setconf(void);

#if CONFIG_BASESYSTEMROOT
static int bsd_find_basesystem_dmg(char *bsdmgpath_out, bool *rooted_dmg, bool *skip_signature_check);
static boolean_t bsdmgroot_bootable(void);
#endif // CONFIG_BASESYSTEMROOT

static bool bsd_rooted_ramdisk(void);

#if SYSV_SHM
extern void sysv_shm_lock_init(void);
#endif
#if SYSV_SEM
extern void sysv_sem_lock_init(void);
#endif
#if SYSV_MSG
extern void sysv_msg_lock_init(void);
#endif

extern void ulock_initialize(void);

#if CONFIG_MACF
#if defined (__i386__) || defined (__x86_64__)
/* MACF policy_check configuration flags; see policy_check.c for details */
int policy_check_flags = 0;

extern int check_policy_init(int);
#endif
#endif  /* CONFIG_MACF */

/* If we are using CONFIG_DTRACE */
#if CONFIG_DTRACE
extern void dtrace_postinit(void);
#endif

/*
 * Initialization code.
 * Called from cold start routine as
 * soon as a stack and segmentation
 * have been established.
 * Functions:
 *	turn on clock
 *	hand craft 0th process
 *	call all initialization routines
 *  hand craft 1st user process
 */

/*
 *	Sets the name for the given task.
 */
static void
process_name(const char *s, proc_t p)
{
	strlcpy(p->p_comm, s, sizeof(p->p_comm));
	strlcpy(p->p_name, s, sizeof(p->p_name));
}

/* To allow these values to be patched, they're globals here */
#include <machine/vmparam.h>
struct rlimit vm_initial_limit_stack = { .rlim_cur = DFLSSIZ, .rlim_max = MAXSSIZ - PAGE_MAX_SIZE };
struct rlimit vm_initial_limit_data = { .rlim_cur = DFLDSIZ, .rlim_max = MAXDSIZ };
struct rlimit vm_initial_limit_core = { .rlim_cur = DFLCSIZ, .rlim_max = MAXCSIZ };

extern struct os_refgrp rlimit_refgrp;

extern thread_t cloneproc(task_t, coalition_t, proc_t, int, int);
extern int      (*mountroot)(void);

LCK_ATTR_DECLARE(proc_lck_attr, 0, 0);
LCK_GRP_DECLARE(proc_lck_grp, "proc");
LCK_GRP_DECLARE(proc_slock_grp, "proc-slock");
LCK_GRP_DECLARE(proc_fdmlock_grp, "proc-fdmlock");
LCK_GRP_DECLARE(proc_mlock_grp, "proc-mlock");
LCK_GRP_DECLARE(proc_ucred_mlock_grp, "proc-ucred-mlock");
LCK_GRP_DECLARE(proc_dirslock_grp, "proc-dirslock");
LCK_GRP_DECLARE(proc_kqhashlock_grp, "proc-kqhashlock");
LCK_GRP_DECLARE(proc_knhashlock_grp, "proc-knhashlock");


LCK_MTX_DECLARE_ATTR(proc_list_mlock, &proc_mlock_grp, &proc_lck_attr);

#if XNU_TARGET_OS_OSX
/* hook called after root is mounted XXX temporary hack */
void (*mountroot_post_hook)(void);
void (*unmountroot_pre_hook)(void);
#endif
void set_rootvnode(vnode_t);

extern lck_rw_t rootvnode_rw_lock;

/* called with an iocount and usecount on new_rootvnode */
void
set_rootvnode(vnode_t new_rootvnode)
{
	mount_t new_mount = (new_rootvnode != NULL) ? new_rootvnode->v_mount : NULL;
	vnode_t new_devvp = (new_mount != NULL) ? new_mount->mnt_devvp : NULL;
	vnode_t old_rootvnode = rootvnode;

	new_rootvnode->v_flag |= VROOT;
	rootvp = new_devvp;
	rootvnode = new_rootvnode;
	filedesc0.fd_cdir = new_rootvnode;
	if (new_devvp != NULL) {
		rootdev = vnode_specrdev(new_devvp);
	} else if (new_mount != NULL) {
		rootdev = vfs_statfs(new_mount)->f_fsid.val[0];  /* like ATTR_CMN_DEVID */
	} else {
		rootdev = NODEV;
	}

	if (old_rootvnode) {
		vnode_rele(old_rootvnode);
	}
}

#define RAMDEV "md0"

bool
bsd_rooted_ramdisk(void)
{
	bool is_ramdisk = false;
	char *dev_path = zalloc(ZV_NAMEI);
	if (dev_path == NULL) {
		panic("failed to allocate devpath string! \n");
	}

	if (PE_parse_boot_argn("rd", dev_path, MAXPATHLEN)) {
		if (strncmp(dev_path, RAMDEV, strlen(RAMDEV)) == 0) {
			is_ramdisk = true;
		}
	}

	zfree(ZV_NAMEI, dev_path);
	return is_ramdisk;
}

/*
 * This function is called very early on in the Mach startup, from the
 * function start_kernel_threads() in osfmk/kern/startup.c.  It's called
 * in the context of the current (startup) task using a call to the
 * function kernel_thread_create() to jump into start_kernel_threads().
 * Internally, kernel_thread_create() calls thread_create_internal(),
 * which calls uthread_alloc().  The function of uthread_alloc() is
 * normally to allocate a uthread structure, and fill out the uu_sigmask,
 * uu_context fields.  It skips filling these out in the case of the "task"
 * being "kernel_task", because the order of operation is inverted.  To
 * account for that, we need to manually fill in at least the contents
 * of the uu_context.vc_ucred field so that the uthread structure can be
 * used like any other.
 */

void
bsd_init(void)
{
	struct uthread *ut;
	unsigned int i;
	struct vfs_context context;
	kern_return_t   ret;
	struct ucred temp_cred;
	struct posix_cred temp_pcred;
	vnode_t init_rootvnode = NULLVP;
#if CONFIG_NETBOOT || CONFIG_IMAGEBOOT
	boolean_t       netboot = FALSE;
#endif
#if CONFIG_LOCKERBOOT
	vnode_t pbvn = NULLVP;
	mount_t pbmnt = NULL;
	char *pbdevp = NULL;
	char pbdevpath[64];
	char pbfsname[MFSNAMELEN];
	const char *slash_dev = NULL;
#endif

#define DEBUG_BSDINIT 0

#if DEBUG_BSDINIT
#define bsd_init_kprintf(x, ...) kprintf("bsd_init: " x, ## __VA_ARGS__)
#else
#define bsd_init_kprintf(x, ...)
#endif

	throttle_init();

	printf(copyright);

	bsd_init_kprintf("calling parse_bsd_args\n");
	parse_bsd_args();

#if CONFIG_DEV_KMEM
	bsd_init_kprintf("calling dev_kmem_init\n");
	dev_kmem_init();
#endif

	/* Initialize kauth subsystem before instancing the first credential */
	bsd_init_kprintf("calling kauth_init\n");
	kauth_init();

	/* Initialize process and pgrp structures. */
	bsd_init_kprintf("calling procinit\n");
	procinit();

	/* kernel_task->proc = kernproc; */
	set_bsdtask_info(kernel_task, (void *)kernproc);

	/* give kernproc a name */
	bsd_init_kprintf("calling process_name\n");
	process_name("kernel_task", kernproc);

	/* Allocate proc lock attribute */

	lck_mtx_init(&kernproc->p_mlock, &proc_mlock_grp, &proc_lck_attr);
	lck_mtx_init(&kernproc->p_fdmlock, &proc_fdmlock_grp, &proc_lck_attr);
	lck_mtx_init(&kernproc->p_ucred_mlock, &proc_ucred_mlock_grp, &proc_lck_attr);
	lck_spin_init(&kernproc->p_slock, &proc_slock_grp, &proc_lck_attr);
	lck_rw_init(&kernproc->p_dirs_lock, &proc_dirslock_grp, &proc_lck_attr);

	assert(bsd_simul_execs != 0);
	execargs_cache_size = bsd_simul_execs;
	execargs_free_count = bsd_simul_execs;
	execargs_cache = zalloc_permanent(bsd_simul_execs * sizeof(vm_offset_t),
	    ZALIGN(vm_offset_t));

	if (current_task() != kernel_task) {
		printf("bsd_init: We have a problem, "
		    "current task is not kernel task\n");
	}

	bsd_init_kprintf("calling get_bsdthread_info\n");
	ut = (uthread_t)get_bsdthread_info(current_thread());

#if CONFIG_MACF
	/*
	 * Initialize the MAC Framework
	 */
	mac_policy_initbsd();

#if defined (__i386__) || defined (__x86_64__)
	/*
	 * We currently only support this on i386/x86_64, as that is the
	 * only lock code we have instrumented so far.
	 */
	check_policy_init(policy_check_flags);
#endif
#endif /* MAC */

	ulock_initialize();

	/*
	 * Create process 0.
	 */
	proc_list_lock();
	LIST_INSERT_HEAD(&allproc, kernproc, p_list);
	kernproc->p_pgrp = &pgrp0;
	LIST_INSERT_HEAD(PGRPHASH(0), &pgrp0, pg_hash);
	LIST_INIT(&pgrp0.pg_members);
	lck_mtx_init(&pgrp0.pg_mlock, &proc_mlock_grp, &proc_lck_attr);
	/* There is no other bsd thread this point and is safe without pgrp lock */
	LIST_INSERT_HEAD(&pgrp0.pg_members, kernproc, p_pglist);
	kernproc->p_listflag |= P_LIST_INPGRP;
	kernproc->p_pgrpid = 0;
	kernproc->p_uniqueid = 0;

	pgrp0.pg_session = &session0;
	pgrp0.pg_membercnt = 1;

	session0.s_count = 1;
	session0.s_leader = kernproc;
	session0.s_listflags = 0;
	lck_mtx_init(&session0.s_mlock, &proc_mlock_grp, &proc_lck_attr);
	LIST_INSERT_HEAD(SESSHASH(0), &session0, s_hash);
	proc_list_unlock();

#if CONFIG_PERSONAS
	kernproc->p_persona = NULL;
#endif

	kernproc->task = kernel_task;

	kernproc->p_stat = SRUN;
	kernproc->p_flag = P_SYSTEM;
	kernproc->p_lflag = 0;
	kernproc->p_ladvflag = 0;

#if defined(__LP64__)
	kernproc->p_flag |= P_LP64;
#endif

#if DEVELOPMENT || DEBUG
	if (bootarg_disable_aslr) {
		kernproc->p_flag |= P_DISABLE_ASLR;
	}
#endif

	kernproc->p_nice = NZERO;
	kernproc->p_pptr = kernproc;

	TAILQ_INIT(&kernproc->p_uthlist);
	TAILQ_INSERT_TAIL(&kernproc->p_uthlist, ut, uu_list);

	kernproc->sigwait = FALSE;
	kernproc->sigwait_thread = THREAD_NULL;
	kernproc->exit_thread = THREAD_NULL;
	kernproc->p_csflags = CS_VALID;

	/*
	 * Create credential.  This also Initializes the audit information.
	 */
	bsd_init_kprintf("calling bzero\n");
	bzero(&temp_cred, sizeof(temp_cred));
	bzero(&temp_pcred, sizeof(temp_pcred));
	temp_pcred.cr_ngroups = 1;
	/* kern_proc, shouldn't call up to DS for group membership */
	temp_pcred.cr_flags = CRF_NOMEMBERD;
	temp_cred.cr_audit.as_aia_p = audit_default_aia_p;

	bsd_init_kprintf("calling kauth_cred_create\n");
	/*
	 * We have to label the temp cred before we create from it to
	 * properly set cr_ngroups, or the create will fail.
	 */
	posix_cred_label(&temp_cred, &temp_pcred);
	kernproc->p_ucred = kauth_cred_create(&temp_cred);

	/* update cred on proc */
	PROC_UPDATE_CREDS_ONPROC(kernproc);

	/* give the (already exisiting) initial thread a reference on it */
	bsd_init_kprintf("calling kauth_cred_ref\n");
	kauth_cred_ref(kernproc->p_ucred);
	ut->uu_context.vc_ucred = kernproc->p_ucred;
	ut->uu_context.vc_thread = current_thread();

	vfs_set_context_kernel(&ut->uu_context);

	TAILQ_INIT(&kernproc->p_aio_activeq);
	TAILQ_INIT(&kernproc->p_aio_doneq);
	kernproc->p_aio_total_count = 0;

#if CONFIG_MACF
	mac_cred_label_associate_kernel(kernproc->p_ucred);
#endif

	/* Create the file descriptor table. */
	kernproc->p_fd = &filedesc0;
	filedesc0.fd_cmask = (mode_t)cmask;
	filedesc0.fd_knlistsize = 0;
	filedesc0.fd_knlist = NULL;
	filedesc0.fd_knhash = NULL;
	filedesc0.fd_knhashmask = 0;
	lck_mtx_init(&filedesc0.fd_kqhashlock, &proc_kqhashlock_grp, &proc_lck_attr);
	lck_mtx_init(&filedesc0.fd_knhashlock, &proc_knhashlock_grp, &proc_lck_attr);

	/* Create the limits structures. */
	kernproc->p_limit = &limit0;
	for (i = 0; i < sizeof(kernproc->p_limit->pl_rlimit) / sizeof(kernproc->p_limit->pl_rlimit[0]); i++) {
		limit0.pl_rlimit[i].rlim_cur =
		    limit0.pl_rlimit[i].rlim_max = RLIM_INFINITY;
	}
	limit0.pl_rlimit[RLIMIT_NOFILE].rlim_cur = NOFILE;
	limit0.pl_rlimit[RLIMIT_NPROC].rlim_cur = maxprocperuid;
	limit0.pl_rlimit[RLIMIT_NPROC].rlim_max = maxproc;
	limit0.pl_rlimit[RLIMIT_STACK] = vm_initial_limit_stack;
	limit0.pl_rlimit[RLIMIT_DATA] = vm_initial_limit_data;
	limit0.pl_rlimit[RLIMIT_CORE] = vm_initial_limit_core;
	os_ref_init_count(&limit0.pl_refcnt, &rlimit_refgrp, 1);

	kernproc->p_stats = &pstats0;
	kernproc->p_sigacts = &sigacts0;
	kernproc->p_subsystem_root_path = NULL;

	/*
	 * Charge root for one process: launchd.
	 */
	bsd_init_kprintf("calling chgproccnt\n");
	(void)chgproccnt(0, 1);

	/*
	 *	Allocate a kernel submap for pageable memory
	 *	for temporary copying (execve()).
	 */
	{
		vm_offset_t     minimum;

		bsd_init_kprintf("calling kmem_suballoc\n");
		assert(bsd_pageable_map_size != 0);
		ret = kmem_suballoc(kernel_map,
		    &minimum,
		    (vm_size_t)bsd_pageable_map_size,
		    TRUE,
		    VM_FLAGS_ANYWHERE,
		    VM_MAP_KERNEL_FLAGS_NONE,
		    VM_KERN_MEMORY_BSD,
		    &bsd_pageable_map);
		if (ret != KERN_SUCCESS) {
			panic("bsd_init: Failed to allocate bsd pageable map");
		}
	}

	/*
	 * Initialize buffers and hash links for buffers
	 *
	 * SIDE EFFECT: Starts a thread for bcleanbuf_thread(), so must
	 *		happen after a credential has been associated with
	 *		the kernel task.
	 */
	bsd_init_kprintf("calling bsd_bufferinit\n");
	bsd_bufferinit();

	/*
	 * Initialize the calendar.
	 */
	bsd_init_kprintf("calling IOKitInitializeTime\n");
	IOKitInitializeTime();

	/* Initialize the file systems. */
	bsd_init_kprintf("calling vfsinit\n");
	vfsinit();

#if CONFIG_PROC_UUID_POLICY
	/* Initial proc_uuid_policy subsystem */
	bsd_init_kprintf("calling proc_uuid_policy_init()\n");
	proc_uuid_policy_init();
#endif

#if SOCKETS
	/* Initialize per-CPU cache allocator */
	mcache_init();

	/* Initialize mbuf's. */
	bsd_init_kprintf("calling mbinit\n");
	mbinit();
	net_str_id_init(); /* for mbuf tags */
	restricted_in_port_init();
#endif /* SOCKETS */

	/*
	 * Initializes security event auditing.
	 * XXX: Should/could this occur later?
	 */
#if CONFIG_AUDIT
	bsd_init_kprintf("calling audit_init\n");
	audit_init();
#endif

	/* Initialize kqueues */
	bsd_init_kprintf("calling knote_init\n");
	knote_init();

	/* Initialize event handler */
	bsd_init_kprintf("calling eventhandler_init\n");
	eventhandler_init();

	/* Initialize for async IO */
	bsd_init_kprintf("calling aio_init\n");
	aio_init();

	pthread_init();
	/* POSIX Shm and Sem */
	bsd_init_kprintf("calling pshm_cache_init\n");
	pshm_cache_init();
	bsd_init_kprintf("calling psem_cache_init\n");
	psem_cache_init();
	bsd_init_kprintf("calling select_waitq_init\n");
	select_waitq_init();

	/*
	 * Initialize protocols.  Block reception of incoming packets
	 * until everything is ready.
	 */
#if NETWORKING
	bsd_init_kprintf("calling nwk_wq_init\n");
	nwk_wq_init();
	bsd_init_kprintf("calling dlil_init\n");
	dlil_init();
	bsd_init_kprintf("calling proto_kpi_init\n");
	proto_kpi_init();
#endif /* NETWORKING */
#if SOCKETS
	bsd_init_kprintf("calling socketinit\n");
	socketinit();
	bsd_init_kprintf("calling domaininit\n");
	domaininit();
	iptap_init();
#if FLOW_DIVERT
	flow_divert_init();
#endif  /* FLOW_DIVERT */
#endif /* SOCKETS */
#if NETWORKING
#if NECP
	/* Initialize Network Extension Control Policies */
	necp_init();
#endif
	netagent_init();
#endif /* NETWORKING */
	kernproc->p_fd->fd_cdir = NULL;
	kernproc->p_fd->fd_rdir = NULL;

#if defined (__x86_64__) && (DEBUG || DEVELOPMENT)
	hvg_bsd_init();
#endif /* DEBUG || DEVELOPMENT */

#if CONFIG_FREEZE
#ifndef CONFIG_MEMORYSTATUS
    #error "CONFIG_FREEZE defined without matching CONFIG_MEMORYSTATUS"
#endif
	/* Initialise background freezing */
	bsd_init_kprintf("calling memorystatus_freeze_init\n");
	memorystatus_freeze_init();
#endif

#if CONFIG_MEMORYSTATUS
	/* Initialize kernel memory status notifications */
	bsd_init_kprintf("calling memorystatus_init\n");
	memorystatus_init();
#endif /* CONFIG_MEMORYSTATUS */

	bsd_init_kprintf("calling sysctl_mib_init\n");
	sysctl_mib_init();

	bsd_init_kprintf("calling bsd_autoconf\n");
	bsd_autoconf();

#if CONFIG_DTRACE
	dtrace_postinit();
#endif

	/*
	 * We attach the loopback interface *way* down here to ensure
	 * it happens after autoconf(), otherwise it becomes the
	 * "primary" interface.
	 */
#include <loop.h>
#if NLOOP > 0
	bsd_init_kprintf("calling loopattach\n");
	loopattach();                   /* XXX */
#endif
#if NGIF
	/* Initialize gif interface (after lo0) */
	gif_init();
#endif

#if PFLOG
	/* Initialize packet filter log interface */
	pfloginit();
#endif /* PFLOG */

#if NETHER > 0
	/* Register the built-in dlil ethernet interface family */
	bsd_init_kprintf("calling ether_family_init\n");
	ether_family_init();
#endif /* ETHER */

#if NETWORKING
#if CONTENT_FILTER
	cfil_init();
#endif

#if PACKET_MANGLER
	pkt_mnglr_init();
#endif

	/*
	 * Register subsystems with kernel control handlers
	 */
	utun_register_control();
#if IPSEC
	ipsec_register_control();
#endif /* IPSEC */
	netsrc_init();
	nstat_init();
	tcp_cc_init();
#if MPTCP
	mptcp_control_register();
#endif /* MPTCP */

	/*
	 * The the networking stack is now initialized so it is a good time to call
	 * the clients that are waiting for the networking stack to be usable.
	 */
	bsd_init_kprintf("calling net_init_run\n");
	net_init_run();
#endif /* NETWORKING */

	bsd_init_kprintf("calling inittodr\n");
	inittodr(0);

	/* Mount the root file system. */
	while (TRUE) {
		int err;

		bsd_init_kprintf("calling setconf\n");
		setconf();
#if CONFIG_NETBOOT
		netboot = (mountroot == netboot_mountroot);
#endif

		bsd_init_kprintf("vfs_mountroot\n");
		if (0 == (err = vfs_mountroot())) {
			break;
		}
		rootdevice[0] = '\0';
#if CONFIG_NETBOOT
		if (netboot) {
			PE_display_icon( 0, "noroot");  /* XXX a netboot-specific icon would be nicer */
			vc_progress_set(FALSE, 0);
			for (i = 1; 1; i *= 2) {
				printf("bsd_init: failed to mount network root, error %d, %s\n",
				    err, PE_boot_args());
				printf("We are hanging here...\n");
				IOSleep(i * 60 * 1000);
			}
			/*NOTREACHED*/
		}
#endif
		printf("cannot mount root, errno = %d\n", err);
		boothowto |= RB_ASKNAME;
	}

	IOSecureBSDRoot(rootdevice);

	context.vc_thread = current_thread();
	context.vc_ucred = kernproc->p_ucred;
	mountlist.tqh_first->mnt_flag |= MNT_ROOTFS;

	bsd_init_kprintf("calling VFS_ROOT\n");
	/* Get the vnode for '/'.  Set fdp->fd_fd.fd_cdir to reference it. */
	if (VFS_ROOT(mountlist.tqh_first, &init_rootvnode, &context)) {
		panic("bsd_init: cannot find root vnode: %s", PE_boot_args());
	}
	(void)vnode_ref(init_rootvnode);
	(void)vnode_put(init_rootvnode);

	lck_rw_lock_exclusive(&rootvnode_rw_lock);
	set_rootvnode(init_rootvnode);
	lck_rw_unlock_exclusive(&rootvnode_rw_lock);
	init_rootvnode = NULLVP;  /* use rootvnode after this point */


	if (!bsd_rooted_ramdisk()) {
#if CONFIG_IMAGEBOOT
#if XNU_TARGET_OS_OSX && defined(__arm64__)
		/* Apple Silicon MacOS */
		if (!imageboot_desired()) {
			/* enforce sealedness */
			int autherr = VNOP_IOCTL(rootvnode, FSIOC_KERNEL_ROOTAUTH, NULL, 0, vfs_context_kernel());
			if (autherr) {
				panic("rootvp not authenticated after mounting \n");
			}
		}
#endif // TARGET_OS_OSX && arm64
#endif // config_imageboot
		/* Otherwise, noop */
	}


#if CONFIG_NETBOOT
	if (netboot) {
		int err;

		netboot = TRUE;
		/* post mount setup */
		if ((err = netboot_setup()) != 0) {
			PE_display_icon( 0, "noroot");  /* XXX a netboot-specific icon would be nicer */
			vc_progress_set(FALSE, 0);
			for (i = 1; 1; i *= 2) {
				printf("bsd_init: NetBoot could not find root, error %d: %s\n",
				    err, PE_boot_args());
				printf("We are hanging here...\n");
				IOSleep(i * 60 * 1000);
			}
			/*NOTREACHED*/
		}
	}
#endif


#if CONFIG_IMAGEBOOT
#if CONFIG_LOCKERBOOT
	/*
	 * Stash the protoboot vnode, mount, filesystem name, and device name for
	 * later use. Note that the mount-from name may not have the "/dev/"
	 * component, so we must sniff out this condition and add it as needed.
	 */
	pbvn = rootvnode;
	pbmnt = pbvn->v_mount;
	pbdevp = vfs_statfs(pbmnt)->f_mntfromname;
	slash_dev = strnstr(pbdevp, "/dev/", strlen(pbdevp));
	if (slash_dev) {
		/*
		 * If the old root is a snapshot mount, it will have the form:
		 *
		 *     com.apple.os.update-<boot manifest hash>@<dev node path>
		 *
		 * So we just search the mntfromname for any occurrence of "/dev/" and
		 * grab that as the device path. The image boot code needs a dev node to
		 * do the re-mount, so we cannot directly mount the snapshot as the
		 * protoboot volume currently.
		 */
		strlcpy(pbdevpath, slash_dev, sizeof(pbdevpath));
	} else {
		snprintf(pbdevpath, sizeof(pbdevpath), "/dev/%s", pbdevp);
	}

	bsd_init_kprintf("protoboot mount-from: %s\n", pbdevp);
	bsd_init_kprintf("protoboot dev path: %s\n", pbdevpath);

	strlcpy(pbfsname, pbmnt->mnt_vtable->vfc_name, sizeof(pbfsname));
#endif

	/*
	 * See if a system disk image is present. If so, mount it and
	 * switch the root vnode to point to it
	 */
	imageboot_type_t imageboot_type = imageboot_needed();
	if (netboot == FALSE && imageboot_type) {
		/*
		 * An image was found.  No turning back: we're booted
		 * with a kernel from the disk image.
		 */
		bsd_init_kprintf("doing image boot: type = %d\n", imageboot_type);
		imageboot_setup(imageboot_type);
	}

#if CONFIG_LOCKERBOOT
	if (imageboot_type == IMAGEBOOT_LOCKER) {
		bsd_init_kprintf("booting from locker\n");
		if (vnode_tag(rootvnode) != VT_LOCKERFS) {
			panic("root filesystem not a locker: fsname = %s",
			    rootvnode->v_mount->mnt_vtable->vfc_name);
		}
	}
#endif /* CONFIG_LOCKERBOOT */
#endif /* CONFIG_IMAGEBOOT */

	/* set initial time; all other resource data is  already zero'ed */
	microtime_with_abstime(&kernproc->p_start, &kernproc->p_stats->ps_start);

#if DEVFS
	{
		char mounthere[] = "/dev"; /* !const because of internal casting */

		bsd_init_kprintf("calling devfs_kernel_mount\n");
		devfs_kernel_mount(mounthere);
	}
#endif /* DEVFS */

#if CONFIG_BASESYSTEMROOT
#if CONFIG_IMAGEBOOT
	if (bsdmgroot_bootable()) {
		int error;
		bool rooted_dmg = false;
		bool skip_signature_check = false;

		printf("trying to find and mount BaseSystem dmg as root volume\n");
#if DEVELOPMENT || DEBUG
		printf("(set boot-arg -nobsdmgroot to avoid this)\n");
#endif // DEVELOPMENT || DEBUG

		char *dmgpath = NULL;
		dmgpath = zalloc_flags(ZV_NAMEI, Z_ZERO | Z_WAITOK);
		if (dmgpath == NULL) {
			panic("%s: M_NAMEI zone exhausted", __FUNCTION__);
		}

		error = bsd_find_basesystem_dmg(dmgpath, &rooted_dmg, &skip_signature_check);
		if (error) {
			bsd_init_kprintf("failed to to find BaseSystem dmg: error = %d\n", error);
		} else {
			PE_parse_boot_argn("bsdmgpath", dmgpath, sizeof(dmgpath));

			bsd_init_kprintf("found BaseSystem dmg at: %s\n", dmgpath);

			error = imageboot_pivot_image(dmgpath, IMAGEBOOT_DMG, "/System/Volumes/BaseSystem", "System/Volumes/macOS", rooted_dmg, skip_signature_check);
			if (error) {
				bsd_init_kprintf("couldn't mount BaseSystem dmg: error = %d", error);
			}
		}
		zfree(ZV_NAMEI, dmgpath);
	}
#else /* CONFIG_IMAGEBOOT */
#error CONFIG_BASESYSTEMROOT requires CONFIG_IMAGEBOOT
#endif /* CONFIG_IMAGEBOOT */
#endif /* CONFIG_BASESYSTEMROOT */

#if CONFIG_LOCKERBOOT
	/*
	 * We need to wait until devfs is up before remounting the protoboot volume
	 * within the locker so that it can have a real devfs vnode backing it.
	 */
	if (imageboot_type == IMAGEBOOT_LOCKER) {
		bsd_init_kprintf("re-mounting protoboot volume\n");
		int error = mount_locker_protoboot(pbfsname, LOCKER_PROTOBOOT_MOUNT,
		    pbdevpath);
		if (error) {
			panic("failed to mount protoboot volume: dev path = %s, error = %d",
			    pbdevpath, error);
		}
	}
#endif /* CONFIG_LOCKERBOOT */

	/* Initialize signal state for process 0. */
	bsd_init_kprintf("calling siginit\n");
	siginit(kernproc);

	bsd_init_kprintf("calling bsd_utaskbootstrap\n");
	bsd_utaskbootstrap();

	pal_kernel_announce();

	bsd_init_kprintf("calling mountroot_post_hook\n");

#if XNU_TARGET_OS_OSX
	/* invoke post-root-mount hook */
	if (mountroot_post_hook != NULL) {
		mountroot_post_hook();
	}
#endif

#if 0 /* not yet */
	consider_zone_gc(FALSE);
#endif

	bsd_init_kprintf("done\n");
}

void
bsdinit_task(void)
{
	proc_t p = current_proc();

	process_name("init", p);

	/* Set up exception-to-signal reflection */
	ux_handler_setup();

#if CONFIG_MACF
	mac_cred_label_associate_user(p->p_ucred);
#endif

	vm_init_before_launchd();

#if CONFIG_XNUPOST
	int result = bsd_list_tests();
	result = bsd_do_post();
	if (result != 0) {
		panic("bsd_do_post: Tests failed with result = 0x%08x\n", result);
	}
#endif

	bsd_init_kprintf("bsd_do_post - done");

	load_init_program(p);
	lock_trace = 1;
}

kern_return_t
bsd_autoconf(void)
{
	kprintf("bsd_autoconf: calling kminit\n");
	kminit();

	/*
	 * Early startup for bsd pseudodevices.
	 */
	{
		struct pseudo_init *pi;

		for (pi = pseudo_inits; pi->ps_func; pi++) {
			(*pi->ps_func)(pi->ps_count);
		}
	}

	return IOKitBSDInit();
}


#include <sys/disklabel.h>  /* for MAXPARTITIONS */

static void
setconf(void)
{
	u_int32_t       flags;
	kern_return_t   err;

	err = IOFindBSDRoot(rootdevice, sizeof(rootdevice), &rootdev, &flags);
	if (err) {
		printf("setconf: IOFindBSDRoot returned an error (%d);"
		    "setting rootdevice to 'sd0a'.\n", err);     /* XXX DEBUG TEMP */
		rootdev = makedev( 6, 0 );
		strlcpy(rootdevice, "sd0a", sizeof(rootdevice));
		flags = 0;
	}

#if CONFIG_NETBOOT
	if (flags & 1) {
		/* network device */
		mountroot = netboot_mountroot;
	} else {
#endif
	/* otherwise have vfs determine root filesystem */
	mountroot = NULL;
#if CONFIG_NETBOOT
}
#endif
}

/*
 * Boot into the flavor of Recovery dictated by `mode`.
 */
boolean_t
bsd_boot_to_recovery(bsd_bootfail_mode_t mode, uuid_t volume_uuid, boolean_t reboot)
{
	return IOSetRecoveryBoot(mode, volume_uuid, reboot);
}

void
bsd_utaskbootstrap(void)
{
	thread_t thread;
	struct uthread *ut;

	/*
	 * Clone the bootstrap process from the kernel process, without
	 * inheriting either task characteristics or memory from the kernel;
	 */
	thread = cloneproc(TASK_NULL, COALITION_NULL, kernproc, FALSE, TRUE);

	/* Hold the reference as it will be dropped during shutdown */
	initproc = proc_find(1);
#if __PROC_INTERNAL_DEBUG
	if (initproc == PROC_NULL) {
		panic("bsd_utaskbootstrap: initproc not set\n");
	}
#endif

	zalloc_first_proc_made();

	/*
	 * Since we aren't going back out the normal way to our parent,
	 * we have to drop the transition locks explicitly.
	 */
	proc_signalend(initproc, 0);
	proc_transend(initproc, 0);

	ut = (struct uthread *)get_bsdthread_info(thread);
	ut->uu_sigmask = 0;
	act_set_astbsd(thread);
	task_clear_return_wait(get_threadtask(thread), TCRW_CLEAR_ALL_WAIT);
}

static void
parse_bsd_args(void)
{
	char namep[48];
	int msgbuf;

	if (PE_parse_boot_argn("-s", namep, sizeof(namep))) {
		boothowto |= RB_SINGLE;
	}

	if (PE_parse_boot_argn("-x", namep, sizeof(namep))) { /* safe boot */
		boothowto |= RB_SAFEBOOT;
	}

	if (PE_parse_boot_argn("-minimalboot", namep, sizeof(namep))) {
		/*
		 * -minimalboot indicates that we want userspace to be bootstrapped to a
		 * minimal environment.  What constitutes minimal is up to the bootstrap
		 * process.
		 */
		minimalboot = 1;
	}

#if __x86_64__
	int no32exec;

	/* disable 32 bit grading */
	if (PE_parse_boot_argn("no32exec", &no32exec, sizeof(no32exec))) {
		bootarg_no32exec = !!no32exec;
	}
#endif

	int execfailure_crashreports;
	/* enable crash reports on various exec failures */
	if (PE_parse_boot_argn("execfailurecrashes", &execfailure_crashreports, sizeof(execfailure_crashreports))) {
		bootarg_execfailurereports = !!execfailure_crashreports;
	}

	/* disable vnode_cache_is_authorized() by setting vnode_cache_defeat */
	if (PE_parse_boot_argn("-vnode_cache_defeat", namep, sizeof(namep))) {
		bootarg_vnode_cache_defeat = 1;
	}

#if DEVELOPMENT || DEBUG
	if (PE_parse_boot_argn("-disable_aslr", namep, sizeof(namep))) {
		bootarg_disable_aslr = 1;
	}
#endif



	PE_parse_boot_argn("ncl", &ncl, sizeof(ncl));
	if (PE_parse_boot_argn("nbuf", &max_nbuf_headers,
	    sizeof(max_nbuf_headers))) {
		customnbuf = 1;
	}

#if CONFIG_MACF
#if defined (__i386__) || defined (__x86_64__)
	PE_parse_boot_argn("policy_check", &policy_check_flags, sizeof(policy_check_flags));
#endif
#endif  /* CONFIG_MACF */

	if (PE_parse_boot_argn("msgbuf", &msgbuf, sizeof(msgbuf))) {
		log_setsize(msgbuf);
		oslog_setsize(msgbuf);
	}

	if (PE_parse_boot_argn("-novfscache", namep, sizeof(namep))) {
		nc_disabled = 1;
	}

#if CONFIG_JETSAM && (DEVELOPMENT || DEBUG)
	if (PE_parse_boot_argn("-no_vnode_jetsam", namep, sizeof(namep))) {
		bootarg_no_vnode_jetsam = 1;
	}
#endif /* CONFIG_JETSAM && (DEVELOPMENT || DEBUG) */

	if (PE_parse_boot_argn("-no_vnode_drain", namep, sizeof(namep))) {
		bootarg_no_vnode_drain = 1;
	}

#if CONFIG_DARKBOOT
	/*
	 * The darkboot flag is specified by the bootloader and is stored in
	 * boot_args->bootFlags. This flag is available starting revision 2.
	 */
	boot_args *args = (boot_args *) PE_state.bootArgs;
	if ((args != NULL) && (args->Revision >= kBootArgsRevision2)) {
		darkboot = (args->bootFlags & kBootFlagsDarkBoot) ? 1 : 0;
	} else {
		darkboot = 0;
	}
#endif

#if PROC_REF_DEBUG
	if (PE_parse_boot_argn("-disable_procref_tracking", namep, sizeof(namep))) {
		proc_ref_tracking_disabled = 1;
	}
#endif

	PE_parse_boot_argn("sigrestrict", &sigrestrict_arg, sizeof(sigrestrict_arg));

#if DEVELOPMENT || DEBUG
	if (PE_parse_boot_argn("-no_sigsys", namep, sizeof(namep))) {
		send_sigsys = false;
	}

	if (PE_parse_boot_argn("alt-dyld", dyld_alt_path, sizeof(dyld_alt_path))) {
		if (strlen(dyld_alt_path) > 0) {
			use_alt_dyld = 1;
		}
	}
	PE_parse_boot_argn("dyld_flags", &dyld_flags, sizeof(dyld_flags));

	if (PE_parse_boot_argn("-disable_syscallfilter", &namep, sizeof(namep))) {
		syscallfilter_disable = 1;
	}

#if __arm64__
	if (PE_parse_boot_argn("legacy_footprint_entitlement_mode", &legacy_footprint_entitlement_mode, sizeof(legacy_footprint_entitlement_mode))) {
		/*
		 * legacy_footprint_entitlement_mode specifies the behavior we want associated
		 * with the entitlement. The supported modes are:
		 *
		 * LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE:
		 *	Indicates that we want every process to have the memory accounting
		 *	that is available in iOS 12.0 and beyond.
		 *
		 * LEGACY_FOOTPRINT_ENTITLEMENT_IOS11_ACCT:
		 *	Indicates that for every process that has the 'legacy footprint entitlement',
		 *      we want to give it the old iOS 11.0 accounting behavior which accounted some
		 *	of the process's memory to the kernel.
		 *
		 * LEGACY_FOOTPRINT_ENTITLEMENT_LIMIT_INCREASE:
		 *      Indicates that for every process that has the 'legacy footprint entitlement',
		 *	we want it to have a higher memory limit which will help them acclimate to the
		 *	iOS 12.0 (& beyond) accounting behavior that does the right accounting.
		 *      The bonus added to the system-wide task limit to calculate this higher memory limit
		 *      is available in legacy_footprint_bonus_mb.
		 */

		if (legacy_footprint_entitlement_mode < LEGACY_FOOTPRINT_ENTITLEMENT_IGNORE ||
		    legacy_footprint_entitlement_mode > LEGACY_FOOTPRINT_ENTITLEMENT_LIMIT_INCREASE) {
			legacy_footprint_entitlement_mode = LEGACY_FOOTPRINT_ENTITLEMENT_LIMIT_INCREASE;
		}
	}
#endif /* __arm64__ */
#endif /* DEVELOPMENT || DEBUG */
}

#if CONFIG_BASESYSTEMROOT

extern const char* IOGetBootUUID(void);
extern const char* IOGetApfsPrebootUUID(void);

// Get the UUID of the Preboot (and Recovery) folder associated with the
// current boot volume, if applicable. The meaning of the UUID can be
// filesystem-dependent and not all kinds of boots will have a UUID.
// If available, the string will be returned. It does not need to be
// deallocate. (Future: if we need to return the string as a copy that the
// caller must free, we'll introduce a new functcion for that.)
// NULL will be returned if the current boot has no applicable Preboot UUID.
static
const char *
get_preboot_uuid(void)
{
	const char *maybe_uuid_string;

	// try IOGetApfsPrebootUUID
	maybe_uuid_string = IOGetApfsPrebootUUID();
	if (maybe_uuid_string) {
		uuid_t maybe_uuid;
		int error = uuid_parse(maybe_uuid_string, maybe_uuid);
		if (error == 0) {
			return maybe_uuid_string;
		}
	}

	// try IOGetBootUUID
	maybe_uuid_string = IOGetBootUUID();
	if (maybe_uuid_string) {
		uuid_t maybe_uuid;
		int error = uuid_parse(maybe_uuid_string, maybe_uuid);
		if (error == 0) {
			return maybe_uuid_string;
		}
	}

	// didn't find it
	return NULL;
}

#if defined(__arm64__)
extern const char *IOGetBootObjectsPath(void);
#endif

// Find the BaseSystem.dmg to be used as the initial root volume during certain
// kinds of boots.
// This may mount volumes and lookup vnodes.
// The DEVELOPMENT kernel will look for BaseSystem.rooted.dmg first.
// If it returns 0 (no error), then it also writes the absolute path to the
// BaseSystem.dmg into its argument (which must be a char[MAXPATHLEN]).
static
int
bsd_find_basesystem_dmg(char *bsdmgpath_out, bool *rooted_dmg, bool *skip_signature_check)
{
	int error;
	size_t len;
	char *dmgbasepath;
	char *dmgpath;
	bool allow_rooted_dmg = false;

	dmgbasepath = zalloc_flags(ZV_NAMEI, Z_ZERO | Z_WAITOK);
	dmgpath = zalloc_flags(ZV_NAMEI, Z_ZERO | Z_WAITOK);
	vnode_t imagevp = NULLVP;

#if DEVELOPMENT || DEBUG
	allow_rooted_dmg = true;
#endif

	//must provide output bool
	if (rooted_dmg && skip_signature_check) {
		*rooted_dmg = false;
		*skip_signature_check = false;
	} else {
		error = EINVAL;
		goto done;
	}

	error = vfs_mount_recovery();
	if (error) {
		goto done;
	}

	len = strlcpy(dmgbasepath, "/System/Volumes/Recovery/", MAXPATHLEN);
	if (len > MAXPATHLEN) {
		error = ENAMETOOLONG;
		goto done;
	}

	if (csr_check(CSR_ALLOW_ANY_RECOVERY_OS) == 0) {
		*skip_signature_check = true;
		allow_rooted_dmg = true;
	}

#if defined(__arm64__)
	const char *boot_obj_path = IOGetBootObjectsPath();
	if (boot_obj_path) {
		if (boot_obj_path[0] == '/') {
			dmgbasepath[len - 1] = '\0';
		}

		len = strlcat(dmgbasepath, boot_obj_path, MAXPATHLEN);
		if (len > MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto done;
		}

		len = strlcat(dmgbasepath, "/usr/standalone/firmware/", MAXPATHLEN);
		if (len > MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto done;
		}

		if (allow_rooted_dmg) {
			len = strlcpy(dmgpath, dmgbasepath, MAXPATHLEN);
			if (len > MAXPATHLEN) {
				error = ENAMETOOLONG;
				goto done;
			}

			len = strlcat(dmgpath, "arm64eBaseSystem.rooted.dmg", MAXPATHLEN);
			if (len > MAXPATHLEN) {
				error = ENAMETOOLONG;
				goto done;
			}

			error = vnode_lookup(dmgpath, 0, &imagevp, vfs_context_kernel());
			if (error == 0) {
				*rooted_dmg = true;
				*skip_signature_check = true;
				goto done;
			}
			memset(dmgpath, 0, MAXPATHLEN);
		}

		len = strlcpy(dmgpath, dmgbasepath, MAXPATHLEN);
		if (len > MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto done;
		}

		len = strlcat(dmgpath, "arm64eBaseSystem.dmg", MAXPATHLEN);
		if (len > MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto done;
		}

		error = vnode_lookup(dmgpath, 0, &imagevp, vfs_context_kernel());
		if (error == 0) {
			goto done;
		}
		memset(dmgpath, 0, MAXPATHLEN);
		dmgbasepath[strlen("/System/Volumes/Recovery/")] = '\0';
	}
#endif // __arm64__

	const char *preboot_uuid = get_preboot_uuid();
	if (preboot_uuid == NULL) {
		// no preboot? bail out
		return EINVAL;
	}

	len = strlcat(dmgbasepath, preboot_uuid, MAXPATHLEN);
	if (len > MAXPATHLEN) {
		error = ENAMETOOLONG;
		goto done;
	}

	if (allow_rooted_dmg) {
		// Try BaseSystem.rooted.dmg
		len = strlcpy(dmgpath, dmgbasepath, MAXPATHLEN);
		if (len > MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto done;
		}

		len = strlcat(dmgpath, "/BaseSystem.rooted.dmg", MAXPATHLEN);
		if (len > MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto done;
		}

		error = vnode_lookup(dmgpath, 0, &imagevp, vfs_context_kernel());
		if (error == 0) {
			// we found it! success!
			*rooted_dmg = true;
			*skip_signature_check = true;
			goto done;
		}
	}

	// Try BaseSystem.dmg
	len = strlcpy(dmgpath, dmgbasepath, MAXPATHLEN);
	if (len > MAXPATHLEN) {
		error = ENAMETOOLONG;
		goto done;
	}

	len = strlcat(dmgpath, "/BaseSystem.dmg", MAXPATHLEN);
	if (len > MAXPATHLEN) {
		error = ENAMETOOLONG;
		goto done;
	}

	error = vnode_lookup(dmgpath, 0, &imagevp, vfs_context_kernel());
	if (error == 0) {
		// success!
		goto done;
	}

done:
	if (error == 0) {
		strlcpy(bsdmgpath_out, dmgpath, MAXPATHLEN);
	} else {
		bsd_init_kprintf("%s: error %d\n", __func__, error);
	}
	if (imagevp != NULLVP) {
		vnode_put(imagevp);
	}
	zfree(ZV_NAMEI, dmgpath);
	zfree(ZV_NAMEI, dmgbasepath);
	return error;
}

static boolean_t
bsdmgroot_bootable(void)
{
#if defined(__arm64__)
#define BSDMGROOT_DEFAULT true
#else
#define BSDMGROOT_DEFAULT false
#endif

	boolean_t resolved = BSDMGROOT_DEFAULT;

	boolean_t boot_arg_bsdmgroot = false;
	boolean_t boot_arg_nobsdmgroot = false;
	int error;
	mount_t mp;
	boolean_t root_part_of_volume_group = false;
	struct vfs_attr vfsattr;

	mp = rootvnode->v_mount;
	VFSATTR_INIT(&vfsattr);
	VFSATTR_WANTED(&vfsattr, f_capabilities);

	boot_arg_bsdmgroot = PE_parse_boot_argn("-bsdmgroot", NULL, 0);
	boot_arg_nobsdmgroot = PE_parse_boot_argn("-nobsdmgroot", NULL, 0);

	error = vfs_getattr(mp, &vfsattr, vfs_context_kernel());
	if (!error && VFSATTR_IS_SUPPORTED(&vfsattr, f_capabilities)) {
		if ((vfsattr.f_capabilities.capabilities[VOL_CAPABILITIES_FORMAT] & VOL_CAP_FMT_VOL_GROUPS) &&
		    (vfsattr.f_capabilities.valid[VOL_CAPABILITIES_FORMAT] & VOL_CAP_FMT_VOL_GROUPS)) {
			root_part_of_volume_group = true;
		}
	}

	boolean_t singleuser = (boothowto & RB_SINGLE) != 0;

	// Start with the #defined default above.
	// If booting to single-user mode, default to false, because single-
	// user mode inside the BaseSystem is probably not what's wanted.
	// If the 'yes' boot-arg is set, we'll allow that even in single-user
	// mode, we'll assume you know what you're doing.
	// The 'no' boot-arg overpowers the 'yes' boot-arg.
	// In any case, we will not attempt to root from BaseSystem if the
	// original (booter-chosen) root volume isn't in a volume group.
	// This is just out of an abundance of caution: if the boot environment
	// seems to be "something other than a standard install",
	// we'll be conservative in messing with the root volume.

	if (singleuser) {
		resolved = false;
	}

	if (boot_arg_bsdmgroot) {
		resolved = true;
	}

	if (boot_arg_nobsdmgroot) {
		resolved = false;
	}

	if (!root_part_of_volume_group) {
		resolved = false;
	}

	return resolved;
}
#endif // CONFIG_BASESYSTEMROOT

void
bsd_exec_setup(int scale)
{
	switch (scale) {
	case 0:
	case 1:
		bsd_simul_execs = BSD_SIMUL_EXECS;
		break;
	case 2:
	case 3:
		bsd_simul_execs = 65;
		break;
	case 4:
	case 5:
		bsd_simul_execs = 129;
		break;
	case 6:
	case 7:
		bsd_simul_execs = 257;
		break;
	default:
		bsd_simul_execs = 513;
		break;
	}
	bsd_pageable_map_size = (bsd_simul_execs * BSD_PAGEABLE_SIZE_PER_EXEC);
}

#if !CONFIG_NETBOOT
int
netboot_root(void);

int
netboot_root(void)
{
	return 0;
}
#endif
