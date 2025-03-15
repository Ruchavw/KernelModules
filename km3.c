#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>

#define NUM_CHILDREN 3  // Adjust as needed

// **Function Prototypes (Fix Warning)**
static int child_function(void *arg);
static int parent_function(void *arg);

static struct task_struct *parent_task;
static struct task_struct *child_tasks[NUM_CHILDREN];

static int child_function(void *arg) {  // `static` added for proper scope
    int id = *(int *)arg;
    printk(KERN_INFO "│   ├── Child Thread %d started [PID: %d]\n", id, current->pid);
    
    while (!kthread_should_stop()) {
        ssleep(1);
    }

    printk(KERN_INFO "│   ├── Child Thread %d stopping [PID: %d]\n", id, current->pid);
    return 0;
}

static int parent_function(void *arg) {  // `static` added for proper scope
    int i, ids[NUM_CHILDREN];

    printk(KERN_INFO "Parent Thread started [PID: %d]\n", current->pid);

    for (i = 0; i < NUM_CHILDREN; i++) {
        ids[i] = i + 1;
        child_tasks[i] = kthread_run(child_function, &ids[i], "child_thread_%d", i);
        if (IS_ERR(child_tasks[i])) {
            printk(KERN_ERR "│   ├── Failed to create child thread %d\n", i);
            child_tasks[i] = NULL;
        }
    }

    while (!kthread_should_stop()) {
        ssleep(1);
    }

    printk(KERN_INFO "Parent Thread stopping [PID: %d]\n", current->pid);
    return 0;
}

static int __init my_module_init(void) {
    printk(KERN_INFO "Kernel Module Loaded\n");

    parent_task = kthread_run(parent_function, NULL, "parent_thread");
    if (IS_ERR(parent_task)) {
        printk(KERN_ERR "Failed to create parent thread\n");
        return PTR_ERR(parent_task);
    }

    return 0;
}

static void __exit my_module_exit(void) {
    int i;
    printk(KERN_INFO "Unloading Kernel Module...\n");

    for (i = 0; i < NUM_CHILDREN; i++) {
        if (child_tasks[i]) {
            kthread_stop(child_tasks[i]);
            child_tasks[i] = NULL;
        }
    }

    if (parent_task) {
        kthread_stop(parent_task);
        parent_task = NULL;
    }

    printk(KERN_INFO "Module unloaded successfully.\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Kernel module with parent and multiple child processes (Tree Output)");

