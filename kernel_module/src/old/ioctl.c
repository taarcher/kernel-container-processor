//////////////////////////////////////////////////////////////////////
//                      North Carolina State University
//
//
//
//                             Copyright 2016
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng, Yu-Chia Liu
//
//   Description:
//     Core of Kernel Module for Processor Container
//
////////////////////////////////////////////////////////////////////////

#include "processor_container.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/kthread.h>


/**
 * Node for the linked list of containers
 */
struct ContainerNode {
	__u64 cid;

	int count;

	struct ThreadNode *start;

	struct ContainerNode *next;
	struct ContainerNode *prev;

};

/**
 * Node for the linked list in each container
 */
struct ThreadNode {
	struct task_struct *task;

	struct ThreadNode *next;
	struct ThreadNode *prev;
};

//head of the linked list of containers
struct ContainerNode *head = NULL;
//struct ContainerNode *head;

//mutex lock to use
static DEFINE_MUTEX( mtx );


/**
 * Delete the task in the container.
 * 
 * external functions needed:
 * mutex_lock(), mutex_unlock(), wake_up_process(), 
 */
int processor_container_delete(struct processor_container_cmd __user *user_cmd){


//    printk( KERN_DEBUG "Delete Variables.");
    struct ThreadNode* temp;
    struct ContainerNode* iterC;
	struct ThreadNode* iterT;
    struct task_struct* ctask = current;
    struct ThreadNode* temp2;
    
    int i;
    
	mutex_lock( &mtx );
//    printk( KERN_DEBUG "Mutex locked by delete.");
	
	iterC = head;
	iterT = head->start;
//    printk( KERN_DEBUG "Itterators initialized.");

    temp = NULL;

//    printk( KERN_DEBUG "Starting search through list for the thread %d.", current);
	while( temp == NULL ){
		iterC = iterC->next;
		iterT = iterC->start;
//        printk( KERN_DEBUG "Searching iterC %llu for %d.", iterC->cid, ctask);
		for( i = 0; i < iterC->count; i++ ){
//            printk( KERN_DEBUG "TN # %d.", i);
			if( iterT->task == ctask){
				temp = iterT;
//                printk( KERN_DEBUG "Found the TN: %d : %d.", temp->task, ctask );
			}
//            printk( KERN_DEBUG "wasn't # %d.", i );
		}
	}

//    printk( KERN_DEBUG "Repoint appropriate pointers.");
    if(temp->next != NULL) {
        temp->next->prev = temp->prev;
//        printk( KERN_DEBUG "repoint TN next.");
    }
	temp->prev->next = temp->next;
//    printk( KERN_DEBUG "points repointed.");
    
	iterC->count -= 1;
//    printk( KERN_DEBUG "count decremented to %d.", iterC->count);
    
    printk( KERN_DEBUG "Check if other tasks are in the container: %d", iterC->count);
	if(iterC->count > 0){
//        printk( KERN_DEBUG "Check in end of list.");
		if (temp->next == NULL){
			temp2 = iterC->start;
//            printk( KERN_DEBUG "Wake up front of list");
		}
		wake_up_process( temp2->task );
//        printk( KERN_DEBUG "Wake up next the next task.");
	}

	
//    printk( KERN_DEBUG "check if no other tasks in container.");
	if(iterC->count == 0){
//        printk( KERN_DEBUG "No more tasks in the container.");
		iterC->prev->next = iterC->next;
//        printk( KERN_DEBUG "reassign the previous container pointer.");
//        printk( KERN_DEBUG "check if there is a next item in the list.");
        if( iterC->next != NULL ){
            iterC->next->prev = iterC->prev;
//            printk( KERN_DEBUG "Reassigned the next pointer.");
        }
		kfree(iterC);
//        printk( KERN_DEBUG "free memory of the container.");
	}

    kfree(temp);
//    printk( KERN_DEBUG "freed TN memory.");
    
	mutex_unlock( &mtx );
//    printk( KERN_DEBUG "delete: unlock mutex.");

	schedule();
//    printk( KERN_DEBUG "delete: run schedule.");

    return 0;
}

/**
 * Create a task in the corresponding container.
 * external functions needed:
 * copy_from_user(), mutex_lock(), mutex_unlock(), set_current_state(), schedule()
 * 
 * external variables needed:
 * struct task_struct* current  
 */
int processor_container_create(struct processor_container_cmd __user *user_cmd)
{
	struct processor_container_cmd user;
    struct ContainerNode* temp;
    struct ContainerNode* temp2;
    struct task_struct* ctask = current;
    
    int flag = 0;

	copy_from_user( &user, user_cmd, sizeof (struct processor_container_cmd));

	mutex_lock( &mtx );

//    printk(KERN_DEBUG "Thread %llu looking for container %llu.", ctask, user.cid);
    
	if( head == NULL){
		head = kmalloc(sizeof (struct ContainerNode ), GFP_KERNEL);
        
		head->count = 0;
		head->next = NULL;
		head->start = NULL;
//        printk(KERN_DEBUG "Thread %llu created the head of the container list.", ctask);
	}

	temp = head;
    temp2 = NULL;
    
	while (temp->next != NULL && flag == 0){
//        printk( KERN_DEBUG "Checking next container.");
		if (temp->cid == user.cid){
//            printk(KERN_DEBUG "Thread %llu found container %llu : %llu.", ctask, temp->cid, user.cid);
            flag = 1;
		}
		temp = temp->next;
	}
//    printk(KERN_DEBUG "Thread %llu didn't find a container.", ctask);
    
	if ( flag == 0 ){
//        printk( KERN_DEBUG "Thread %llu attempting to make container %llu.", ctask, user.cid);
		struct ContainerNode* newCon = kmalloc(sizeof (struct ContainerNode ), GFP_KERNEL);
//        printk( KERN_DEBUG "Assign memory for new container.");
		newCon->next = head->next;
//        printk( KERN_DEBUG "Assign next pointer from head.");
        if( head->next != NULL ){
            head->next->prev = newCon;
//            printk( KERN_DEBUG "Assign prev pointer in list newCon.");
        }
		head->next = newCon;
//        printk( KERN_DEBUG "Assign head's next pointer to newCon.");
		newCon->prev = head;
//        printk( KERN_DEBUG "Assign head to newCon's prev pointer.");

		newCon->count = 0;
//        printk( KERN_DEBUG "Set newCon's counter to 0." );
		newCon->cid = user.cid;
//        printk( KERN_DEBUG "Set newCon's cid to %llu : %llu .", newCon->cid, user.cid);
        newCon->start = NULL;
//        printk( KERN_DEBUG "Set newCon's start to NULL.");
        
//        printk(KERN_DEBUG "Thread %d made container %d.", ctask, newCon->cid);
		temp = newCon;
	}

	struct ThreadNode* newNode = kmalloc(sizeof (struct ThreadNode ), GFP_KERNEL);
//    printk( KERN_DEBUG "Thread %d assigned memory for new ThreadNode.", ctask);
    
	newNode->task = ctask;
//    printk( KERN_DEBUG "Thread %d set the task in the new TN - %d.", ctask, newNode->task);
	if (temp->start != NULL){
        newNode->next = temp->start->next;
//        printk( KERN_DEBUG "Thread %d set the next pointer for the new TN.", ctask);
	}
    temp->start = newNode;
//    printk( KERN_DEBUG "Thread %d set the new TN as the start for the container.", ctask);
	newNode->prev = temp->start;
//    printk( KERN_DEBUG "Thread %d set its prev as the container start.", ctask);

	temp->count += 1;
//    printk( KERN_DEBUG "Thread %d set the container counter to %d.", ctask, temp->count);

	if (temp->count > 1){
		set_current_state( TASK_INTERRUPTIBLE );
//        printk(KERN_DEBUG "Thread %llu is asleep.", ctask);
	}
//    printk(KERN_DEBUG "There are %d threads in container %llu.", temp->count, temp->cid);
	mutex_unlock( &mtx );

	schedule();

    return 0;
}

/**
 * switch to the next task in the next container
 * 
 * external functions needed:
 * mutex_lock(), mutex_unlock(), wake_up_process(), set_current_state(), schedule()
 */
int processor_container_switch(struct processor_container_cmd __user *user_cmd)
{

//    printk( KERN_DEBUG "Starting the switch task with %d.", current);
    
    struct ThreadNode* temp;
    struct ContainerNode* iterC;
	struct ThreadNode* iterT;
    struct task_struct* ctask = current; 
    
    int i;
    
	mutex_lock( &mtx );
//    printk( KERN_DEBUG "Mutex locked by switch.");
	
	iterC = head;
	iterT = head->start;
//    printk( KERN_DEBUG "Itterators initialized.");

    temp = NULL;

//    printk( KERN_DEBUG "Starting search through list for the thread %d.", current);
	while( temp == NULL ){
		iterC = iterC->next;
		iterT = iterC->start;
//        printk( KERN_DEBUG "Searching iterC %llu for %d.", iterC->cid, ctask);
		for( i = 0; i < iterC->count; i++ ){
//            printk( KERN_DEBUG "TN # %d.", i);
			if( iterT->task == ctask){
				temp = iterT;
//                printk( KERN_DEBUG "Found the TN." );
			}
//            printk( KERN_DEBUG "wasn't # %d.", i );
		}
	}
    
//    printk( KERN_DEBUG "Check Container for more than one task: %d", iterC->count);
    
    if( iterC->count > 1 ){
//        printk( KERN_DEBUG "Check the next TN for a task.");
        iterT = temp->next;
        if ( iterT == NULL ){
            iterT = iterC->start;
//            printk( KERN_DEBUG "TN was the last in the list, so back to start.");
        }

        set_current_state( TASK_INTERRUPTIBLE );
//        printk( KERN_DEBUG "Set task %d to sleep.", ctask);
        wake_up_process( iterT->task );
//        printk( KERN_DEBUG "Wake up task %d.", iterT->task);
    }

	mutex_unlock( &mtx );
//    printk( KERN_DEBUG "switch: unlock mutex.");
    
	schedule();
//    printk( KERN_DEBUG "switch: run schedule.");
    
    return 0;
}

/**
 * control function that receive the command in user space and pass arguments to
 * corresponding functions.
 */
int processor_container_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    switch (cmd)
    {
    case PCONTAINER_IOCTL_CSWITCH:
        return processor_container_switch((void __user *)arg);
    case PCONTAINER_IOCTL_CREATE:
        return processor_container_create((void __user *)arg);
    case PCONTAINER_IOCTL_DELETE:
        return processor_container_delete((void __user *)arg);
    default:
        return -ENOTTY;
    }
}
