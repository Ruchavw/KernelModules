#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/string.h>

// Process structure to mimic system processes
struct process_info {
    char name[20];
    struct task_struct *task;
    int children_count;
    struct process_info *children[5]; // Max 5 children per process
};

// Root process and all child processes
static struct process_info *Parent = NULL;
static struct process_info *Child_1 = NULL;
static struct process_info *Child_2 = NULL;
static struct process_info *Child_3 = NULL;
static struct process_info *Child_1_1 = NULL;
static struct process_info *Child_3_1 = NULL;
static struct process_info *Child_1_1_1 = NULL;
static struct process_info *Child_1_1_2 = NULL;
static struct process_info *Child_3_1_1 = NULL;

// Function to create a new process_info structure
static struct process_info *create_process_info(const char *name) {
    struct process_info *proc = kmalloc(sizeof(struct process_info), GFP_KERNEL);
    if (proc) {
        strncpy(proc->name, name, sizeof(proc->name) - 1);
        proc->name[sizeof(proc->name) - 1] = '\0';
        proc->task = NULL;
        proc->children_count = 0;
    }
    return proc;
}

// Function to add a child process to a parent
static void add_child(struct process_info *parent, struct process_info *child) {
    if (parent && child && parent->children_count < 5) {
        parent->children[parent->children_count++] = child;
    }
}

// Thread function for all processes
static int process_function(void *arg) {
    struct process_info *proc = (struct process_info *)arg;
    
    printk(KERN_INFO "%s [PID: %d] started\n", proc->name, current->pid);
    
    while (!kthread_should_stop()) {
        ssleep(1);
    }
    
    printk(KERN_INFO "%s [PID: %d] stopping\n", proc->name, current->pid);
    return 0;
}

// Function to start a process and all its children recursively
static int start_process(struct process_info *proc) {
    int i;
    
    if (!proc) return -EINVAL;
    
    // Start this process
    proc->task = kthread_run(process_function, proc, proc->name);
    if (IS_ERR(proc->task)) {
        printk(KERN_ERR "Failed to create %s process\n", proc->name);
        proc->task = NULL;
        return PTR_ERR(proc->task);
    }
    
    // Print current tree state after small delay to ensure PID is assigned
    msleep(100);
    
    // Start all children recursively
    for (i = 0; i < proc->children_count; i++) {
        start_process(proc->children[i]);
    }
    
    return 0;
}

// Function to stop a process and all its children recursively
static void stop_process(struct process_info *proc) {
    int i;
    
    if (!proc) return;
    
    // Stop all children first
    for (i = 0; i < proc->children_count; i++) {
        stop_process(proc->children[i]);
    }
    
    // Stop this process
    if (proc->task) {
        kthread_stop(proc->task);
        proc->task = NULL;
    }
}

// Function to free process structures recursively
static void free_process(struct process_info *proc) {
    int i;
    
    if (!proc) return;
    
    // Free all children first
    for (i = 0; i < proc->children_count; i++) {
        free_process(proc->children[i]);
    }
    
    // Free this process
    kfree(proc);
}

// Print the process tree recursively
static void print_tree(struct process_info *proc, int level) {
    int i, j;
    char indent[100] = "";
    
    if (!proc || !proc->task) return;
    
    // Create indentation
    for (j = 0; j < level; j++) {
    	if(j==level-1)
    		strcat(indent, "|---- ");
    	else
        	strcat(indent, "|     ");
    }
    
    // Print this process
    printk(KERN_INFO "%s%s pid = %d\n", indent, proc->name, proc->task->pid);
    
    // Print all children
    for (i = 0; i < proc->children_count; i++) {
        print_tree(proc->children[i], level + 1);
    }
}

static int __init process_tree_init(void) {
    printk(KERN_INFO "Process Tree Module Loading\n");
    
    // Create all process info structures
    Parent = create_process_info("Parent");
    Child_1 = create_process_info("Child_1");
    Child_2 = create_process_info("Child_2");
    Child_3 = create_process_info("Child_3");
    Child_1_1 = create_process_info("Child_1_1");
    Child_3_1 = create_process_info("Child_3_1");
    Child_1_1_1 = create_process_info("Child_1_1_1");
    Child_1_1_2 = create_process_info("Child_1_1_2");
    Child_3_1_1 = create_process_info("Child_3_1_1");
    
    if (!Parent || !Child_1 || !Child_2 || !Child_3 || !Child_1_1 || 
        !Child_3_1 || !Child_1_1_1 || !Child_1_1_2 || !Child_3_1_1) {
        printk(KERN_ERR "Failed to allocate memory for processes\n");
        goto cleanup;
    }
    
    // Build the process tree structure as shown in the image
    add_child(Parent, Child_1);
    add_child(Parent, Child_2);
    add_child(Parent, Child_3);
    
    add_child(Child_1, Child_1_1);
    
    add_child(Child_1_1, Child_1_1_1);
    add_child(Child_1_1, Child_1_1_2);
    
    add_child(Child_3, Child_3_1);
    
    add_child(Child_3_1, Child_3_1_1);
    
    // Start the root process (Parent), which will recursively start all others
    if (start_process(Parent) != 0) {
        goto cleanup;
    }
    
    // Print the process tree after all processes started
    msleep(500);
    printk(KERN_INFO "Process Tree Structure:\n");
    print_tree(Parent, 0);
    
    return 0;
    
cleanup:
    // Free all allocated structures
    if (Parent) free_process(Parent);
    Parent = Child_1 = Child_2 = Child_3 = Child_1_1 = Child_3_1 = Child_1_1_1 = Child_1_1_2 = Child_3_1_1 = NULL;
    return -ENOMEM;
}

static void __exit process_tree_exit(void) {
    printk(KERN_INFO "Unloading Process Tree Module...\n");
    
    if (Parent) {
        // Stop the Parent process and all its children
        stop_process(Parent);
        
        // Free all process structures
        free_process(Parent);
        Parent = Child_1 = Child_2 = Child_3 = Child_1_1 = Child_3_1 = Child_1_1_1 = Child_1_1_2 = Child_3_1_1 = NULL;
    }
    
    printk(KERN_INFO "Process Tree Module unloaded successfully.\n");
}

module_init(process_tree_init);
module_exit(process_tree_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Process Tree Creator");
MODULE_DESCRIPTION("Kernel module creating a process tree similar to Parent hierarchy");
