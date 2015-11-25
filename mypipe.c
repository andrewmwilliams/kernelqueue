/*
  mypipe.c
  Kernel module which implements a queue of N messages.
  For the assignment listed at:
  http://www.cs.binghamton.edu/~kartik/cs350/assignments/producer_consumer_strings_kernel.html
  15 November 2015
  Andrew Williams awilli64@binghamton.edu
*/

#include <linux/fs.h>         //for file operations struct
#include <linux/module.h>     //needed by all kernel modules
#include <linux/miscdevice.h> //for misc_register and misc_deregister
#include <linux/mutex.h>      //for mutex
#include <linux/semaphore.h>  //for semaphore
#include <linux/slab.h>       //for kmalloc, kfree

MODULE_LICENSE ("GPL");

//node for linked list of buffer entries
struct node {
  struct node *next;
  char        *data;
};

//access into linked list of buffer entries
static struct node *head;
static struct node *tail;

//synchronization variables
static DEFINE_SEMAPHORE (fullSem);    //how many full slots the queue has
static DEFINE_SEMAPHORE (emptySem);   //how many empty slots the queue has
static DEFINE_MUTEX     (queueMutex);

static int numBufEntries = 10; //default buffer size

module_param (numBufEntries, int, S_IRUGO);

static ssize_t mypipe_read  (struct file *, char *, size_t, loff_t *);
static ssize_t mypipe_write (struct file *, const char *, size_t, loff_t *);

void clearBuffer (struct node *);

static struct file_operations mypipe_fops = {
  .owner = THIS_MODULE,
  .read  = mypipe_read,
  .write = mypipe_write,
};

static struct miscdevice my_pipe_device = {
  MISC_DYNAMIC_MINOR,
  "mypipe",
  &mypipe_fops,
};

int __init mypipe_init (void) {
  head = tail = NULL;
  sema_init (&fullSem, 0);
  sema_init (&emptySem, numBufEntries);
  mutex_init (&queueMutex);
  misc_register (&my_pipe_device);
  return 0;
}

void __exit mypipe_exit (void) {
  clearBuffer(head);
  misc_deregister (&my_pipe_device);
}

static ssize_t mypipe_read (struct file *file,   //not used
			    char __user *buffer, //fill with data for user process to read
			    size_t length,       //length of buffer
			    loff_t *off) {       //not used
  struct node *tmp;
 
  if (down_interruptible (&fullSem)) {
    //error received, lock not acquired
    printk (KERN_ALERT "down_interruptible on fullSem failed\n");
    return -1;
  }
  if (mutex_lock_interruptible (&queueMutex)) {
    //error received, lock not acquired
    printk (KERN_ALERT "mutex_lock_interruptible on queueMutex failed\n");
    return -1;
  }
  strcpy (buffer, head->data);
  
  tmp = head;
  head = head->next;
  kfree (tmp->data);
  kfree (tmp);
  //if we just deleted the last item
  if (head == NULL)
    tail = NULL;

  mutex_unlock (&queueMutex);
  up (&emptySem);
  
  return strlen (buffer);
}

static ssize_t mypipe_write (struct file *file,  //not used
			     const char *buffer, //data coming from user process
			     size_t len,         //length of buffer
			     loff_t *off) {      //not used
  //insert the string in buffer into the linked list

  if (down_interruptible (&emptySem)) {
    printk (KERN_ALERT "down_interruptoble on emptySem failed\n");
    return -1;
  }
  if (mutex_lock_interruptible (&queueMutex)) {
    printk (KERN_ALERT "mutex_lock_interruptible on queueMutex failed\n");
    return -1;
  }
  
  //if the list is currently empty
  if (head == NULL) {
    head = (struct node *) kmalloc (sizeof (struct node), GFP_KERNEL);
    if (head == NULL) {
      printk (KERN_ALERT "kmalloc failed to get memory for a node\n");
      return -1;
    }
    head->data = (char *)  kmalloc (len, GFP_KERNEL);
    if (head->data == NULL) {
      printk (KERN_ALERT "kmalloc failed to get memory for a string\n");
      return -1;
    }
    head->next = NULL;
    strcpy (head->data, buffer);
    tail = head;
  } else {
    //the list is not empty, so we insert at the tail
    tail->next = (struct node *) kmalloc (sizeof (struct node), GFP_KERNEL);
    if (tail->next == NULL) {
      printk (KERN_ALERT "kmalloc failed to get memory for a node\n");
      return -1;
    }
    tail->next->data = (char *)  kmalloc (len, GFP_KERNEL);
    if (tail->next->data == NULL) {
      printk (KERN_ALERT "kmalloc failed to get memory for a string\n");
      return -1;
    }
    tail->next->next = NULL;
    strcpy (tail->next->data, buffer);
    tail = tail->next;
  }
  mutex_unlock (&queueMutex);
  up (&fullSem);

  return strlen (buffer);
}

void clearBuffer (struct node * curr) {
  if (curr == NULL)
    return;
  clearBuffer(curr->next);
  kfree (curr->data);
  kfree (curr);
}

module_init (mypipe_init);
module_exit (mypipe_exit);
