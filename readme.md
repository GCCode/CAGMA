# CAGMA Project is to solve performance degradation when insufficient memory problem occurs

大标题
====

# The Prograss of this project: 
1. Build AutoKickStart to automatically download clean kernel 		....... 01/30/2017 finished
   and update the kernel with modified install.cfg 			
2. Build MemEventTrigger to monitor 'memory/target' and 'mem-		....... 02/28/2017 finished
   ory/warning' in Xenstore						

3. Modify Kernel 4.2.2 for checking AVM and UMA for each VM 		....... 03/10/2017 finished
   periodically and adjusting memory of each VM according to 
   that :						
	a. CMA Watcher
	b. Memory Adjuster		
 
3. Build a Workload Generator for verifying cabibility of CAGMA:	....... 03/20/2017 finished
	a. Data Collector
	b. Task Generator

4. Optimize Source Code within this project :
	a. Optimize AutoKickStart sub-project 				....... 08/11/2017 finished
	b. Optimize modified kernel 4.2.2
	c. Optimize Workload Generator