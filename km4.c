#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/mm_types.h>
#include <linux/rbtree.h>
#include <linux/mmap_lock.h>  // Add this for mmap_read_lock functions

#define MAX_CHILDREN 4
#define MEM_SIZE_BASE (4 * PAGE_SIZE)  // Base memory size for allocation

// Process structure
struct process_node {
    char name[20];
    struct task_struct *task;
    void *memory;           // Dynamically allocated memory
    size_t memory_size;     // Size of allocated memory
    int child_count;
    struct process_node *children[MAX_CHILDREN];
};

// Structure to hold memory segment information
struct memory_segment {
    unsigned long start;
    unsigned long end;
    unsigned long flags;
    unsigned long pgoff;
    dev_t dev;
    unsigned long ino;
    char type[20];
};

// Global root node
static struct process_node *root_process = NULL;

// Process function that allocates memory and waits until stopped
static int process_function(void *data) {
    struct process_node *node = (struct process_node *)data;
    unsigned int random_value;
    
    // Get a random value for memory size
    get_random_bytes(&random_value, sizeof(random_value));
    
    // Allocate memory dynamically - different size for each process
    node->memory_size = MEM_SIZE_BASE + (random_value % (4 * PAGE_SIZE));
    node->memory = kmalloc(node->memory_size, GFP_KERNEL);
    
    if (node->memory) {
        // Write some data into the allocated memory to ensure it's used
        memset(node->memory, 0xAB, node->memory_size);
        printk(KERN_INFO "Process %s [PID: %d] allocated %zu bytes at %pK\n", 
               node->name, task_pid_nr(current), node->memory_size, node->memory);
    } else {
        printk(KERN_ERR "Process %s [PID: %d] failed to allocate memory\n", 
               node->name, task_pid_nr(current));
    }
    
    // Wait until stopped
    while (!kthread_should_stop()) {
        ssleep(1);
    }
    
    // Free allocated memory before exiting
    if (node->memory) {
        kfree(node->memory);
        node->memory = NULL;
    }
    
    return 0;
}

// Create a new process node
static struct process_node *create_process_node(const char *name) {
    struct process_node *node = kmalloc(sizeof(struct process_node), GFP_KERNEL);
    if (node) {
        memset(node, 0, sizeof(struct process_node));
        strncpy(node->name, name, sizeof(node->name) - 1);
        node->name[sizeof(node->name) - 1] = '\0';  // Ensure null termination
    }
    return node;
}

// Add a child to a parent node
static int add_child(struct process_node *parent, struct process_node *child) {
    if (!parent || !child || parent->child_count >= MAX_CHILDREN)
        return -EINVAL;
    
    parent->children[parent->child_count++] = child;
    return 0;
}

// Start a process and its children recursively
static int start_process_tree(struct process_node *node) {
    int i, ret = 0;
    
    if (!node)
        return -EINVAL;
    
    // Create and start this process
    node->task = kthread_run(process_function, node, "%s", node->name);
    if (IS_ERR(node->task)) {
        ret = PTR_ERR(node->task);
        node->task = NULL;
        return ret;
    }
    
    // Allow some time for memory allocation
    msleep(100);
    
    // Start all children
    for (i = 0; i < node->child_count; i++) {
        ret = start_process_tree(node->children[i]);
        if (ret)
            break;
    }
    
    return ret;
}

// Stop a process and its children recursively
static void stop_process_tree(struct process_node *node) {
    int i;
    
    if (!node)
        return;
    
    // Stop all children first
    for (i = 0; i < node->child_count; i++) {
        stop_process_tree(node->children[i]);
    }
    
    // Stop this process
    if (node->task) {
        kthread_stop(node->task);
        node->task = NULL;
    }
}

// Free a process node and its children recursively
static void free_process_tree(struct process_node *node) {
    int i;
    
    if (!node)
        return;
    
    // Free all children first
    for (i = 0; i < node->child_count; i++) {
        free_process_tree(node->children[i]);
    }
    
    // Free this node
    kfree(node);
}

// Get memory information for a task directly from the kernel
// Get memory information for a task directly from the kernel
static void get_memory_info(struct task_struct *task, int level) {
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    char indent[100] = "";
    int i;
    
    // Create indentation
    for (i = 0; i < level + 1; i++) {
        strcat(indent, "    ");
    }
    
    // Get the memory map
    mm = task->mm;
    if (!mm) {
        printk(KERN_INFO "%s├── Kernel thread (no userspace memory)\n", indent);
        return;
    }
    
    // Use mmap_lock instead of mmap_sem for newer kernels
    if (mmap_read_lock_killable(mm) == 0) {
        // Iterate through all VMAs using VMA_ITERATOR
        VMA_ITERATOR(vmi, mm, 0);
        for_each_vma(vmi, vma) {
            char prot[5], segment_type[20];
            
            // Protection flags
            prot[0] = vma->vm_flags & VM_READ ? 'r' : '-';
            prot[1] = vma->vm_flags & VM_WRITE ? 'w' : '-';
            prot[2] = vma->vm_flags & VM_EXEC ? 'x' : '-';
            prot[3] = vma->vm_flags & VM_MAYSHARE ? 's' : 'p';
            prot[4] = '\0';
            
            // Determine segment type
            if (vma->vm_start <= mm->start_brk && vma->vm_end >= mm->brk) {
                strcpy(segment_type, "Heap");
            } else if (vma->vm_start <= mm->start_stack && vma->vm_end >= mm->start_stack) {
                strcpy(segment_type, "Stack");
            } else if (vma->vm_flags & VM_EXEC) {
                strcpy(segment_type, "Code");
            } else if (vma->vm_flags & VM_WRITE) {
                strcpy(segment_type, "Data");
            } else {
                strcpy(segment_type, "Other");
            }
            
            // Print the memory segment information
            printk(KERN_INFO "%s├── %s: %lx-%lx %s\n", 
                   indent, segment_type, vma->vm_start, vma->vm_end, prot);
        }
        mmap_read_unlock(mm);
    } else {
        printk(KERN_INFO "%s├── Memory map locked, couldn't access\n", indent);
    }
}

// Function to print the process tree structure
// Modify the print_process_tree_structure function to include memory info:
static void print_process_tree_structure(struct process_node *node, int level, char *prefix) {
    int i;
    char new_prefix[256] = "";
    
    if (!node || !node->task)
        return;
    
    // Print this process node information
    printk(KERN_INFO "Process Tree Structure:\n");
    printk(KERN_INFO "Parent pid = %d\n", task_pid_nr(node->task));
    
    // Add memory segment information for the parent
    get_memory_info(node->task, 0);
    
    // Print children with proper indentation
    for (i = 0; i < node->child_count; i++) {
        struct process_node *child = node->children[i];
        
        if (!child || !child->task)
            continue;
        
        // Print child with indentation
        printk(KERN_INFO "|---- %s pid = %d\n", 
               child->name, task_pid_nr(child->task));
        
        // Add memory segment information for this child
        get_memory_info(child->task, 1);
        
        // Build new prefix for grandchildren
        snprintf(new_prefix, sizeof(new_prefix), "|    ");
        
        // Print grandchildren for this child
        for (int j = 0; j < child->child_count; j++) {
            struct process_node *grandchild = child->children[j];
            
            if (!grandchild || !grandchild->task)
                continue;
                
            printk(KERN_INFO "|    |---- %s pid = %d\n", 
                  grandchild->name, task_pid_nr(grandchild->task));
                  
            // Add memory segment information for this grandchild
            get_memory_info(grandchild->task, 2);
            
            // Print great-grandchildren for this grandchild
            for (int k = 0; k < grandchild->child_count; k++) {
                struct process_node *ggchild = grandchild->children[k];
                
                if (!ggchild || !ggchild->task)
                    continue;
                    
                printk(KERN_INFO "|    |    |---- %s pid = %d\n", 
                      ggchild->name, task_pid_nr(ggchild->task));
                      
                // Add memory segment information for this great-grandchild
                get_memory_info(ggchild->task, 3);
            }
        }
    }
}

// Function to print the stopping of processes
static void print_process_stopping(struct process_node *node) {
    int i;
    
    if (!node || !node->task)
        return;
    
    // Print children first (reverse order of starting)
    for (i = 0; i < node->child_count; i++) {
        // Print grandchildren first
        for (int j = 0; j < node->children[i]->child_count; j++) {
            // Print great-grandchildren first
            for (int k = 0; k < node->children[i]->children[j]->child_count; k++) {
                struct process_node *ggchild = node->children[i]->children[j]->children[k];
                printk(KERN_INFO "%s [PID: %d] stopping\n", 
                       ggchild->name, task_pid_nr(ggchild->task));
            }
            
            struct process_node *grandchild = node->children[i]->children[j];
            printk(KERN_INFO "%s [PID: %d] stopping\n", 
                   grandchild->name, task_pid_nr(grandchild->task));
        }
        
        struct process_node *child = node->children[i];
        printk(KERN_INFO "%s [PID: %d] stopping\n", 
               child->name, task_pid_nr(child->task));
    }
    
    // Finally print the root
    printk(KERN_INFO "Parent [PID: %d] stopping\n", task_pid_nr(node->task));
    printk(KERN_INFO "Process Tree Module unloaded successfully.\n");
}

// Module initialization
// Module initialization
static int __init jackfruit_init(void) {
    struct process_node *child1, *child2, *child3;
    struct process_node *child1_1, *child3_1;
    struct process_node *child1_1_1, *child1_1_2, *child3_1_1;
    
    printk(KERN_INFO "Process Tree Module Loading\n");
    
    // Create process tree
    root_process = create_process_node("Parent");
    
    // Create first level children
    child1 = create_process_node("Child_1");
    child2 = create_process_node("Child_2");
    child3 = create_process_node("Child_3");
    
    // Create second level children
    child1_1 = create_process_node("Child_1_1");
    child3_1 = create_process_node("Child_3_1");
    
    // Create third level children
    child1_1_1 = create_process_node("Child_1_1_1");
    child1_1_2 = create_process_node("Child_1_1_2");
    child3_1_1 = create_process_node("Child_3_1_1");
    
    // Build the tree
    add_child(root_process, child1);
    add_child(root_process, child2);
    add_child(root_process, child3);
    
    add_child(child1, child1_1);
    add_child(child3, child3_1);
    
    add_child(child1_1, child1_1_1);
    add_child(child1_1, child1_1_2);
    add_child(child3_1, child3_1_1);
    
    // Start all processes
    if (start_process_tree(root_process) != 0) {
        printk(KERN_ERR "Failed to start process tree\n");
        free_process_tree(root_process);
        return -EINVAL;
    }
    
    // Allow time for all processes to start
    msleep(500);
    
    // Display the process tree
    print_process_tree_structure(root_process, 0, "");
    
    return 0;
}

// Module cleanup
// Module cleanup
static void __exit jackfruit_exit(void) {
    printk(KERN_INFO "Unloading Process Tree Module...\n");
    
    if (root_process) {
        // Print stopping messages first
        print_process_stopping(root_process);
        
        // Stop all processes
        stop_process_tree(root_process);
        
        // Free all process nodes
        free_process_tree(root_process);
        root_process = NULL;
    }
}

module_init(jackfruit_init);
module_exit(jackfruit_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jackfruit Problem Solver");
MODULE_DESCRIPTION("Kernel module creating processes with dynamic memory and showing memory maps in tree structure");
