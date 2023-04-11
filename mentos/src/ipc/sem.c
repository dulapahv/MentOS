/// @file sem.c
/// @brief
/// @copyright (c) 2014-2023 This file is distributed under the MIT License.
/// See LICENSE.md for details.
///! @cond Doxygen_Suppress

// Setup the logging for this file (do this before any other include).
#include "sys/kernel_levels.h"           // Include kernel log levels.
#define __DEBUG_HEADER__ "[IPCsem]"      ///< Change header.
#define __DEBUG_LEVEL__  LOGLEVEL_NOTICE ///< Set log level.
#include "io/debug.h"                    // Include debugging functions.
#include "klib/list.h"
#include <stdio.h> 
#include "ipc/sem.h" 
#include "mem/slab.h"
#include "system/signal.h"
#include "sys/errno.h"
#include "system/panic.h"
#include "time.h"

/*
    03/04 : at the moment we have various functions with their description (see comments).
            The first time that the function semget is called, we are going to generate the list and the first semaphore set
            with the assumption that we are not given a IPC_PRIVATE key (temporary ofc).
            We are able to create new semaphores set and generate unique keys (IPC_PRIVATE) with
            a counter that searches for the first possible key to assign. -> temporary idea
            IPC_CREAT flag, it should be working properly, it creates a semaphore set with the given key.
            IPC_EXCL flag,  it should be working properly, it returns -1 and sets the errno if the 
                            key is already used

    11/04 : right now we have a first version of working semaphores in MentOS.
            We have completed the semctl function and we have implemented the first version of the semop function
            both user and kernel side. The way it works is pretty straightforward, the user tries to perform an operation
            and based on the value of the semaphore the kernel returns certain values. If the operation cannot be performed
            then the user will stay in a while loop. The cycle ends with a positive return value (the operation has been taken care of)
            or in case of errors. 
            For testing purposes -> you can try the t_semget and the t_sem1 tests. They both use semaphores and blocking / non blocking operations.
            t_sem1 is also an exercise that was assingned by Professor Drago in the OS course.
*/

///@brief a value to compute the semid value
int semid_assign = 0;


/// @brief to initialize a single semaphore 
/// @param temp the pointer to the struct of the semaphore
void sem_init(struct sem * temp){
    temp -> sem_val = 0; /*default*/
    temp -> sem_pid = sys_getpid();
    temp -> sem_zcnt = 0; /*default*/
}

/// @brief to initialize a semid struct (set of semaphores)
/// @param temp the pointer to the semid struct
/// @param key IPC_KEY associated with the set of semaphores
/// @param nsems number of semaphores to initialize
void semid_init(struct semid_ds * temp, key_t key, int nsems){
    temp -> owner = sys_getpid();
    temp -> key = key;
    temp -> semid = ++semid_assign;  //temporary -> gonna need a way to compute IPCKEY -> semid
    temp -> sem_otime = 0; /*default*/
    temp -> sem_ctime = 0; /*default*/
    temp -> sem_nsems = nsems;
    temp -> sems = (struct sem*)kmalloc(sizeof(struct sem)*nsems);
    for (int i=0; i<nsems; i++){
        sem_init(&(temp->sems[i]));
    }
}

///@brief for debugging purposes, print all the stats of the semid_ds
///@param temp the pointer to the semid struct
void semid_print(struct semid_ds *temp){
    printf("pid, IPC_KEY, Semid, semop, change: %d, %d, %d, %d, %d\n", temp->owner, temp->key, temp->semid, temp->sem_otime, temp->sem_ctime);
    for(int i=0; i<(temp->sem_nsems); i++){
        printf("%d semaphore:\n", i+1);
        printf("value: %d, pid %d, process waiting %d\n", temp->sems[i].sem_val, temp->sems[i].sem_pid, temp->sems[i].sem_zcnt);
        //printf("pid of last op: %d\n", temp->sems[i].sem_pid);
        //printf("process waiting: %d\n", temp->sems[i].sem_zcnt);
    }
}


/// @brief list of all current active semaphores
list_t *current_semaphores;

int count_ipc_private = 2;  //temporary -> to implement the IPC_PRIVATE mechanism

long sys_semget(key_t key, int nsems, int semflg)
{
    struct semid_ds *temp = NULL;
    //check if nsems is a valid value
    if(nsems<=0){
        printf("Errore NSEMS\n");  //debuggin purposes
        errno = EINVAL;
        return -1;
    }

    /*if (count == 0){  //first ever to call semget
        current_semaphores = list_create();  //init the list
        temp = (struct semid_ds *)kmalloc(sizeof(struct semid_ds));  //create the first one
        semid_init(temp, key, nsems);
        list_insert_front(current_semaphores, temp);
        count++;
        return temp->semid;
    }*/

    if (current_semaphores == NULL){  //if this is the first time that the function is called
        current_semaphores = list_create();  //we initialize the list
    }    

    if (key == IPC_PRIVATE){ // need to find a unique key
        int flag = 1;
        while (1){  //exit when i find a unique key
            listnode_foreach(listnode, current_semaphores){
                if (((struct semid_ds *)listnode->value)->key == count_ipc_private)
                    flag = 0;
            }
            if (flag==1)break;
            flag = 1;
            count_ipc_private*=3; //multiply to try to find a unique key 
        }
        //we have the key
        temp = (struct semid_ds *)kmalloc(sizeof(struct semid_ds));
        semid_init(temp, count_ipc_private, nsems);
        list_insert_front(current_semaphores, temp);
        return temp -> semid;
    } 
    int flag = 0;
    int temp_semid = -1;
    listnode_foreach(listnode, current_semaphores){  //iterate through the list 
        if (((struct semid_ds *)listnode->value)->key == key){  //if we find a semid with the given key
            temp_semid = ((struct semid_ds *)listnode->value)->semid;  //saving the semid
            flag = 1;  //found
        }
    }  
    if (flag==0){ //unique key
        if (semflg & IPC_CREAT){  //and i want to create a semaphore set with it
            temp = (struct semid_ds *)kmalloc(sizeof(struct semid_ds));
            semid_init(temp, key, nsems);
            list_insert_front(current_semaphores, temp);
            return temp -> semid;    
        }
        errno = EINVAL; //invalid argument bc it is a unique key but with no IPC_CREAT
        return -1;
    }
    if(semflg & IPC_EXCL){ //error, the sem set already exist 
        errno = EEXIST;
        return -1;
    }
    return temp_semid;  //return the correspondent semid

    //TODO("Not implemented");
}

long sys_semop(int semid, struct sembuf *sops, unsigned nsops)
{

    int flag = 0;
    struct semid_ds *temp;
    listnode_foreach(listnode, current_semaphores){  //iterate through the list 
        if (((struct semid_ds *)listnode->value)->semid == semid){  //if we find a semid with the given key
            flag = 1;
            temp = ((struct semid_ds *)listnode->value);
            break;
        }
    }

    if (!flag){ /*if the semid does not find a match then we set the errno and return -1*/
        errno = EINVAL;
        return errno;
    }

    if (sops->sem_num<0 || sops->sem_num >= temp->sem_nsems){  //checking parameters
        errno = EINVAL;
        return -1;
    }

    temp -> sem_otime = sys_time(NULL);

    if (sops->sem_op < 0){
        /*If the operation is negative then we need to check for possible blocking operation*/
        
        /*if the value of the sem were to become negative then we return a special value*/
        if (temp->sems[sops->sem_num].sem_val < (-nsops*(sops->sem_op))){   
            return OPERATION_NOT_ALLOWED; //not allowed
        }else{
            /*otherwise we can modify the sem_val and all the other parameters of the semaphore*/
            temp->sems[sops->sem_num].sem_val += (nsops*(sops->sem_op));
            temp->sems[sops->sem_num].sem_pid = sys_getpid();
            temp -> sem_ctime = sys_time(NULL);
            return 1; //allowed
        }
    }
    else{
        /*the operation is non negative so we can always do it*/
        temp->sems[sops->sem_num].sem_val += (nsops*(sops->sem_op));
        temp->sems[sops->sem_num].sem_pid = sys_getpid();
        temp -> sem_ctime = sys_time(NULL);
        return 1; //allowed
    }

    return 0;
}

long sys_semctl(int semid, int semnum, int cmd, union semun *arg)
{
    int flag = 0;
    struct semid_ds *temp;
    listnode_foreach(listnode, current_semaphores){  //iterate through the list 
        if (((struct semid_ds *)listnode->value)->semid == semid){  //if we find a semid with the given key
            flag = 1;
            temp = ((struct semid_ds *)listnode->value);
            break;
        }
    }

    if (!flag){   /*if the semid does not find a match then we set the errno and return -1*/
        errno = EINVAL;
        return -1;
    }
    
    switch (cmd)
    {
    //remove the semaphore set; any processes blocked is awakened (errno set to EIDRM); no argument required.
    case IPC_RMID:
        list_remove_node(current_semaphores, list_find(current_semaphores, temp));

        //printf("\ndone\n");
        /*gonna need to unblock all the processes on the semaphore*/
               
    
        break;
    
    //place a copy of the semid_ds data structure in the buffer pointed to by arg.buf.
    case IPC_STAT:
        if (arg->buf == NULL || arg->buf->sems == NULL){ /*checking the parameters*/
            errno = EINVAL;
            return -1;
        }

        //copying all the data
        arg->buf->key = temp -> key;
        arg->buf->owner = temp -> owner;
        arg->buf->semid = temp -> semid; 
        arg->buf->sem_otime = temp -> sem_otime;
        arg->buf->sem_ctime = temp -> sem_ctime;
        arg->buf->sem_nsems = temp -> sem_nsems;
        for(int i=0; i<temp->sem_nsems; i++){
            arg->buf->sems[i].sem_val = temp->sems[i].sem_val;
            arg->buf->sems[i].sem_pid = temp->sems[i].sem_pid;
            arg->buf->sems[i].sem_zcnt = temp->sems[i].sem_zcnt;
        }

        return 0;
    
    //update selected fields of the semid_ds using values in the buffer pointed to by arg.buf.
    //case IPC_SET:
        /* code */
        //break;
    
    //the value of the semnum-th semaphore in the set is initialized to the value specified in arg.val.
    case SETVAL:
        if(semnum<0 || semnum>=(temp->sem_nsems)){  //if the index is valid
            errno = EINVAL;
            return -1;
        }

        if(arg->val<0){  //checking if the value is valid
            errno = ERANGE;
            return -1;
        }
        //setting the values
        temp -> sem_ctime = sys_time(NULL);
        temp->sems[semnum].sem_val = arg->val;
        return 0;
    
    //returns the value of the semnum-th semaphore in the set specified by semid; no argument required.
    case GETVAL:
        if(semnum<0 || semnum>=(temp->sem_nsems)){  //if the index is valid
            errno = EINVAL;
            return -1;
        }

        return temp->sems[semnum].sem_val;

    //initialize all semaphore in the set referred to by semid, using the values supplied in the array pointed to by arg.array.
    case SETALL:
        if (arg->array == NULL){  /*checking parameters*/
            errno = EINVAL;
            return -1;
        }
        for (int i = 0; i<temp->sem_nsems; i++){   //setting all the values
            temp->sems[i].sem_val = arg->array[i]; 
        }
        temp -> sem_ctime = sys_time(NULL);
        return 0;
    
    //retrieve the values of all of the semaphores in the set referred to by semid, placing them in the array pointed to by arg.array.
    case GETALL:
        if (arg->array == NULL){  //checking if the argument passed is valid
            errno = EINVAL;
            return -1;
        }
        for (int i = 0; i<temp->sem_nsems; i++){
            arg->array[i] = temp->sems[i].sem_val;
        }
        return 0;
    
    //return the process ID of the last process to perform a semop on the semnum-th semaphore.
    case GETPID:
        if(semnum<0 || semnum>=(temp->sem_nsems)){  //if the index is valid
            errno = EINVAL;
            return -1;
        }

        return temp->sems[semnum].sem_pid;

    //return the number of processes currently waiting for the value of the semnum-th semaphore to become 0. 
    case GETZCNT:
        if(semnum<0 || semnum>=(temp->sem_nsems)){ //if the index is valid
            errno = EINVAL;
            return -1;
        }

        return temp->sems[semnum].sem_zcnt;
    
    //not a valid argument.
    default:
        errno = EINVAL; 
        return -1;
    }

    return 0;
}

///! @endcond
