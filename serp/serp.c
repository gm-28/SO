#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/delay.h>
#include "serial_reg.h"
#define BASE_ADRESS 0x3f8
#define N_DEVS 4
#define BUF_SIZE 32

MODULE_LICENSE("Dual BSD/GPL");

dev_t dev;
struct cdev *cdp;
int serp_open(struct inode *inodep, struct file *filep);
ssize_t serp_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp);
ssize_t serp_read(struct file *filep, char __user *buff, size_t count, loff_t *offp);
int serp_release (struct inode *inode, struct file *filp);


struct file_operations fops= {
	.owner =     THIS_MODULE,
	.read =	     serp_read,
	.write =     serp_write,
	.open =	     serp_open,
	.release =   serp_release,
};

int serp_open(struct inode *inodep, struct file *filep){
    nonseekable_open(inodep,filep);
    filep->private_data = cdp;
    if (filep->private_data == NULL){
        printk(KERN_WARNING "-Private data error !!");
		return -1;
    }
    return 0;
}

ssize_t serp_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp){
    int i,time;
	char *dev_buf=(char *)kmalloc(count*sizeof(char), GFP_KERNEL);
	
	if(copy_from_user(dev_buf, buff, count)){
		printk(KERN_WARNING "-Copy error!!\n");
		kfree(dev_buf);
		return -1;
	}

	for(i=0;i<count;i++){
		time=0;
		while((inb(BASE_ADRESS+UART_LSR) & UART_LSR_THRE )==0){
			msleep_interruptible(10);
			time++;
			if(time==500){
				printk(KERN_WARNING "-Transmitter holder not empty, error !!\n");
				kfree(dev_buf);
				return -1;
			}
		}
		outb(dev_buf[i],BASE_ADRESS+UART_TX);
	}
	kfree(dev_buf);
    return 1;
}

ssize_t serp_read(struct file *filep, char __user *buff, size_t count, loff_t *offp){
    char *dev_buf=(char *)kmalloc(count*sizeof(char), GFP_KERNEL);
	int i,time=0;

	for(i=0;i<count;i++){
		time=0;
		while((inb(BASE_ADRESS+UART_LSR) & UART_LSR_DR )==0){
			msleep_interruptible(10);
			time++;
			if(time==500){
				printk(KERN_WARNING "-No character found or not enough characters(write 32), error !!\n");
				kfree(dev_buf);
				return -EAGAIN;
			}
		}
		if(inb(BASE_ADRESS+UART_LSR) & UART_LSR_DR & UART_LSR_OE){
			printk(KERN_WARNING "-Over run error !!\n");
			kfree(dev_buf);
			return -EIO;
		}
		dev_buf[i]=inb(BASE_ADRESS+UART_RX);
	}
    if(copy_to_user(buff,dev_buf,count)){
        printk(KERN_WARNING "-Copy error !!\n");
		kfree(dev_buf);
		return -1;
    }
	
	kfree(dev_buf);
    return 1;
}

int serp_release (struct inode *inode, struct file *filp){
    printk(KERN_ALERT "-Serp Released !!\n");
	return 0;
}

static int serp_init(void){
	int result = alloc_chrdev_region(&dev,0,N_DEVS, "serp");
	unsigned char lcr=0;

    if (result < 0) {
        printk(KERN_ALERT "-serp: can't alloc!");
        return result;
    }
	
	cdp = cdev_alloc();
    cdp->ops = &fops;
    cdp->owner = THIS_MODULE;
	cdev_init(cdp, &fops);
	
	if (cdev_add(cdp, dev, 1)<0){
            printk(KERN_WARNING "-Adding error\n");
            return -1;
	} 	else printk(KERN_ALERT "-Device Added\n");
    
	if (request_region(BASE_ADRESS, 0x08, "serp")==NULL){
		printk(KERN_WARNING "-Request error\n");
		return -1;
	}
	
	lcr = UART_LCR_WLEN8 | UART_LCR_STOP | UART_LCR_PARITY | UART_LCR_EPAR | UART_LCR_DLAB; 
	outb(lcr, BASE_ADRESS+UART_LCR); 
	
	//setting DL register 115200/b	b=1200
	outb(UART_DIV_1200, BASE_ADRESS+UART_DLL); //UART_DIV_1200=96 colocar no DLL
	outb(0x00, BASE_ADRESS+UART_DLM); 	//colocar 0 no DLM
	
	//reseting DLAB after setting bit rate
	lcr &= ~UART_LCR_DLAB;
	outb(lcr, BASE_ADRESS+UART_LCR);
	
	//disabling Interrupt Enable 
	outb(0x00, BASE_ADRESS+UART_IER);
	
	while((inb(BASE_ADRESS + UART_LSR) & UART_LSR_THRE)==0){
		schedule();
	}
	return 0;
}

static void serp_exit(void){
    unregister_chrdev_region (dev,1);
    cdev_del(cdp);
	release_region(BASE_ADRESS, 0x08);
    printk(KERN_ALERT "-Goodbye.\n");
}


module_init(serp_init);
module_exit(serp_exit);
