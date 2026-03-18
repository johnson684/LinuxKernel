#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/cred.h>

MODULE_LICENSE("Dual BSD/GPL");

static int __init process_init(void)
{
        struct task_struct *task;
        unsigned int uid;

        printk(KERN_INFO "LOADING MODULE\n");

        for_each_process(task) {
                rcu_read_lock();
                uid = __task_cred(task)->uid.val;
                rcu_read_unlock();
                
                printk(KERN_INFO "PID: %d, UID: %d, PROCESS: %s\n", 
                       task->pid, 
                       uid, 
                       task->comm);
        }

        return 0;
}

static void __exit process_exit(void)
{
        printk(KERN_INFO "REMOVING MODULE\n");
}

module_init(process_init);
module_exit(process_exit);
