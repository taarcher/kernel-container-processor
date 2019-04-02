//Thomas "Andy" Archer
//CSC 501 - 601
//Project 1
//unity id: taarcher



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
    //container id
	__u64 cid;

    //number of threads in the container
	int count;

    //start of the thread linked list
	struct ThreadNode *start;

    //container linked list pointers
	struct ContainerNode *next;
	struct ContainerNode *prev;

};

/**
 * Node for the linked list in each container
 */
struct ThreadNode {
    //thread id
	pid_t task;

    //pointers for the thread linked list
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

    //structs for calculations
    struct ThreadNode* temp;
    struct ContainerNode* iterC;
	struct ThreadNode* iterT;
    struct task_struct* ctask = current;
    struct ThreadNode* temp2;
    
    int i;
    
    //lock the global mutex
	mutex_lock( &mtx );
	printk( KERN_DEBUG "mtx locked by delete." );
    printf( "mtx locked by delete." );
    //initialize the itterators for the linked lists
	iterC = head;
	iterT = head->start->next;

    temp = NULL;
    
    //search for the node with the active task to be deleted
	while( temp == NULL ){
		iterC = iterC->next;
		iterT = iterC->start;
		for( i = 0; i < iterC->count; i++ ){
            iterT= iterT->next;
			if( iterT->task == ctask->pid){
				temp = iterT;
                i = iterC->count;
			}
            
		}
	}

    //decrement the counter for the container
	iterC->count -= 1;
 
    //check for propper repositioning of the pointers
    if(iterC->count > 0) {
        if(temp->next != NULL) {
            temp->next->prev = temp->prev;
        }
        temp->prev->next = temp->next;
    
    //check to see if the last item in the list is the thread, and loop back to the 
    //beginning to run the first thread in the list
		if (temp->next == NULL){
			temp2 = iterC->start->next;
		}
		wake_up_process( temp2->task );
	}

	//free the memory for the container
	if(iterC->count == 0){
		iterC->prev->next = iterC->next;
        if( iterC->next != NULL ){
            iterC->next->prev = iterC->prev;
        }
        kfree(iterC->start);
		kfree(iterC);
	}

    //free the memory for thread
    kfree(temp);
    
    //unlock the mutex
	mutex_unlock( &mtx );
    printk( KERN_DEBUG "mtx unlocked by delete." );
    printf( "mtx unlocked by delete." );
    //run schedule
	schedule();

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
    //structs for use
	struct processor_container_cmd user;
    struct ContainerNode* temp;
    struct ContainerNode* temp2;
    struct task_struct* ctask = current;
    
    int flag = 0;

    //copy from user space to kernel space
	copy_from_user( &user, user_cmd, sizeof (struct processor_container_cmd));

    //lock the mutex
	mutex_lock( &mtx );
    printk( KERN_DEBUG "mtx locked by create." );
    printf( "mtx locked by create." );

    //check that the linked list of containers has been started
	if( head == NULL){
		head = kmalloc(sizeof (struct ContainerNode ), GFP_KERNEL);
        
		head->count = 0;
		head->next = NULL;
		head->start = NULL;
        head->prev = NULL;
	}

	temp = head;
    temp2 = NULL;
    
    //check if there are other containers in the list
	while (temp->next != NULL && flag == 0){
        temp = temp->next;
		if (temp->cid == user.cid){
            flag = 1;
		}
	}

    //make the container if needed
	if ( flag == 0 ){
		struct ContainerNode* newCon = kmalloc(sizeof (struct ContainerNode ), GFP_KERNEL);
		newCon->next = head->next;
        if( head->next != NULL ){
            head->next->prev = newCon;
        }
		head->next = newCon;
		newCon->prev = head;

		newCon->count = 0;
		newCon->cid = user.cid;
        struct ContainerNode* Thead = kmalloc(sizeof (struct ThreadNode ), GFP_KERNEL);
        newCon->start = Thead;
        newCon->start->next = NULL;
        newCon->start->prev = NULL;
        
		temp = newCon;
	}

    //make the node for the thread
	struct ThreadNode* newNode = kmalloc(sizeof (struct ThreadNode ), GFP_KERNEL);
    newNode->next = NULL;
    newNode->prev = NULL;
    
    //assign the task to the thread
	newNode->task = ctask->pid;
    
    if(temp->start->next != NULL){
        newNode->next = temp->start->next;
        newNode->next->prev = newNode;
    }
    temp->start->next = newNode;
    newNode->prev = temp->start;
    
    //increase the counter appropriately
	temp->count += 1;

    //check if the thread should be asleep
	if (temp->count > 1){
		set_current_state( TASK_INTERRUPTIBLE );
	}
    
    //unlock the mutex
	mutex_unlock( &mtx );
    printk( KERN_DEBUG "mtx unlocked by create." );
    printf( "mtx unlocked by create." );

    //run schedule
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
    //structs for work
    struct ThreadNode* temp;
    struct ContainerNode* iterC;
	struct ThreadNode* iterT;
    struct task_struct* ctask = current; 
    
    int i;
    
    //lock the mutex
	mutex_lock( &mtx );
	printk( KERN_DEBUG "mtx locked by switch." );
    printf( "mtx locked by switch." );
    //initialize the itterator
	iterC = head;

    temp = NULL;
    
    //search for the active task
	while( temp == NULL ){
		iterC = iterC->next;
		iterT = iterC->start->next;
		for( i = 0; i < iterC->count; i++ ){
			if( iterT->task == ctask->pid){
				temp = iterT;
                i = iterC->count;
			}
            iterT = iterT->next;
		}
	}
    
    
    if( iterC->count > 1 ){
        iterT = temp->next;
        if ( temp->next == NULL ){
            iterT = iterC->start->next;
        }

        //wake up / put to sleep
        set_current_state( TASK_INTERRUPTIBLE );
        wake_up_process( iterT->task );

    }

    //unlock the mutex
	mutex_unlock( &mtx );
    printk( KERN_DEBUG "mtx unlocked by switch." );
    printf( "mtx unlocked by switch." );
    
    //run schedule
	schedule();
    
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
