#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/percpu-defs.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/balloon.h>
#include <xen/features.h>
#include <xen/page.h>
#include <xen/xenbus.h>         /* update CMA with xenbus     */
#define Alloc_rate 1.1 		/* decrease probability of ocurrence of thrashing via Alloc_rate x new_target */
/* 
 * The following boolean variables, is_less_than_maxALM, can_provide_mem and enable_to_run_memAlloc, 
 * represent six system states respectively.
 *
 * 'is_less_than_maxALM' :
 *		0: The memory of this machine is less than maximum memory amount 'maxALM'
 *		1: The memory of this machine is no less than maximum memory maount 'maxALM"
 *
 * 'can_provide_mem' :
 *		0: Memory adjuster has insufficient memory to allocate memory additional to VM		
 *		1: Memory adjuster has sufficient memory to allocate memory to VM
 * In addition, 'enable_to_run_memAlloc' is enabled when guest OS is initiated. (i.e., This 
 * boolean variable is set to 0 until the guest OS boots up. )
 */

bool is_less_than_maxALM = 1;
bool can_provide_mem = 1;
bool enable_to_run_memAlloc = 0; 

long long int Mmax;
long long int CMA = -1;


/*
 * export variables (can_provide_mem, is_less_than_maxALM, enable_to_run_memAlloc,
 * Mmax and CMA) to other files for conveniently use
 */ 
EXPORT_SYMBOL_GPL(can_provide_mem);
EXPORT_SYMBOL_GPL(is_less_than_maxALM);
EXPORT_SYMBOL_GPL(enable_to_run_memAlloc);
EXPORT_SYMBOL_GPL(Mmax);
EXPORT_SYMBOL_GPL(CMA);

/* delcare task template. This task generated from this template can be delayed */

static void cagma_memory_requester_process(struct work_struct *work);
static DECLARE_DELAYED_WORK(cagma_memory_requester_worker, cagma_memory_requester_process);


/* 
 * import two functions (do_sysinfo and find_lock_task_mm), which have been exported, 
 * from other files into this file such that we can call this two functions in this file.
 *
 * function do_sysinfo: return some system information (e.g., available memory amount)
 * function find_lock_task_mm: 
 */

extern int do_sysinfo(struct sysinfo *info);
extern struct task_struct *find_lock_task_mm(struct task_struct *p);

/* 
 * The function send_req: send the request for adjusting allocated memory 
 * Parameter:
 * 	first parameter is the size of unused memory 
 *      second parameter is critical memory amount
 */
static void send_req(long long int AVMtemp, long long int CMAtemp)
{
	struct xenbus_transaction trans;
	long long int out2lld,new_target,req;
	char *output;

	/* obtain allocated memory amount via interface xenbus */ 
	xenbus_transaction_start(&trans);
	output = (char *)xenbus_read(trans,"memory","target",NULL);
	xenbus_transaction_end(trans, 0);

	req = CMAtemp - AVMtemp;
	sscanf(output,"%lld",&out2lld);
	//req = (req * (long long int)((Alloc_rate)*1024)) >> 10;
	
	/* calculate new allocated memory amount */ 
	new_target = out2lld + req;
	printk("new_target:%lld \n",new_target);	


	/*
	 * check whether expected allocated memory is bigger than maximum memory amount
	 * if so, then guest OS can not send the request anymore.
	 * if not, then guest OS can still send the request to hypervisor.
	 */ 
	if (new_target >= Mmax)
	{
		new_target = Mmax;	
		is_less_than_maxALM = 0;
	}
	
	/* send the request to hypervisor via interface xenbus */
	xenbus_transaction_start(&trans);
	/* write new_target into memory/target in xenstore via interface xenbus */
	xenbus_printf(trans, "memory","target", "%lld", new_target); 
	xenbus_transaction_end(trans, 0);


}



static void cagma_memory_requester_process(struct work_struct *work)
{
	struct task_struct *task;
	unsigned long CMAtemp = 0, AVM, temp; 
	struct sysinfo sinfo;
	
	/*  travel all processes/threads to find the process/thread that enables swapping event */
	for_each_process(task)
	{
		struct task_struct *p;
		
		/* find a process/thread which has a vaild mm structure */
		p = find_lock_task_mm(task);
		
		if (!p)
			continue;
		/* 
		 * find_lock_task_mm() calls task_lock() but if a process/thread is found,
		 * then find_lock_task_mm() directly reture without unlocking task. Therefore,
		 * we have to additionally call task_unlock to unlock protection.
		 */ 
		task_unlock(p);
		
		/* obtain memory usage (consist of usage in disk) of the found process/thread */
		CMAtemp = get_mm_rss(p->mm) + get_mm_counter(p->mm, MM_SWAPENTS);
	
	}
	
	/*
	 * The unit of memory usage is in the number of pages.
	 * So, we transform it into memory usage in Byte via 2^(12)
	 */ 
	CMAtemp = CMAtemp << 2;
	CMAtemp = (CMAtemp * (unsigned long) (Alloc_rate * 1024)) >> 10;

	/* obtain unused memory amount via function do_sysinfo() */
	do_sysinfo(&sinfo);
	/* the unit of sinfo.freeram is KiloByte (KB) */
	AVM = sinfo.freeram >> 10;

	/* 
	 * set the constraint (AVM >= CMAtemp) to avoid degrading performance via
	 * decreasing times of adjusting allocated memory amount.
	 */ 
	if (AVM >= CMAtemp)
		return;
	else /* AVM < CMAtemp */
	{
		CMA = CMAtemp;
		send_req(AVM,CMA);			
	}
		
}

/* Generate a task to send request */
void cagma_memory_requester_worker_gen(void)
{
	printk("first generate\n");
	schedule_delayed_work(&cagma_memory_requester_worker, 0);
}

EXPORT_SYMBOL_GPL(cagma_memory_requester_worker_gen);

/* Initialize allocator */
static int __init cagma_memory_requester_init(void)
{

	pr_info("Initialising cagma-memory-requester driver\n");
	return 0;
}

subsys_initcall(cagma_memory_requester_init);

MODULE_LICENSE("GPL");
