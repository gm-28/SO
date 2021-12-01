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
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include "serial_reg.h"

#define BASE_ADRESS 0x3f8
#define N_DEVS 		4
#define BUF_SIZE 	32
#define IRQ 		4
#define TIMEOUT 	500
#define SIZE_KFIFO	100

MODULE_LICENSE("Dual BSD/GPL");

dev_t dev;

typedef struct {
    struct cdev cdev;
	spinlock_t rxsl;
	spinlock_t wxsl;		
	struct kfifo *rxkf;   // receiver fifo
	struct kfifo *wxkf;   // write fifo
    wait_queue_head_t rxwq;	// for IH synchron
	wait_queue_head_t txwq;
}seri_dev_t;

seri_dev_t *seri;

int seri_open(struct inode *inodep, struct file *filep);
ssize_t seri_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp);
ssize_t seri_read(struct file *filep, char __user *buff, size_t count, loff_t *offp);
int seri_release (struct inode *inode, struct file *filp);


struct file_operations fops= {
	.owner =     THIS_MODULE,
	.read =	     seri_read,
	.write =     seri_write,
	.open =	     seri_open,
	.release =   seri_release,
};

irqreturn_t int_handler(int irq, void *dev_id){
	unsigned char buff=0x00;
	unsigned char iir = inb(BASE_ADRESS+UART_IIR);

	if(iir & UART_IIR_RDI){
		buff=inb(BASE_ADRESS+UART_RX);
		kfifo_put(seri->rxkf,&buff,1);
		wake_up_interruptible(&seri->rxwq);
	}else if((iir & UART_IIR_THRI) && kfifo_len(seri->wxkf)){
		if (kfifo_get(seri->wxkf, &buff, 1)){
			outb(buff,BASE_ADRESS+UART_TX);
			wake_up_interruptible(&seri->txwq);
		}	
	}
	return IRQ_HANDLED;
}

int seri_open(struct inode *inodep, struct file *filep){
    filep->private_data=container_of(inodep->i_cdev,seri_dev_t, cdev);
    nonseekable_open(inodep,filep);

    if (filep->private_data == NULL){
        printk(KERN_WARNING "-Private data error !!");
		return -1;
    }

    return 0;
}

ssize_t seri_write(struct file *filep, const char __user *buff, size_t count, loff_t *offp){
    int i=1,flag;
	char *dev_buf=(char *)kmalloc(count*sizeof(char), GFP_KERNEL);
	char tmp;

	//enhac
	if((count>SIZE_KFIFO) && (filep->f_flags & O_NONBLOCK)){
		printk(KERN_ALERT "-Try again!!\n");
	    kfree(dev_buf);
		return -EAGAIN;
	}
	
	if(copy_from_user(dev_buf, buff, count)){
		printk(KERN_WARNING "-Copy error!!\n");
		kfree(dev_buf);
		return -1;
	}
	// envia o primeiro caracter para inicializar a transmissão e retira qualquer caracter que esteja no kfifo quando o Transmitter Holding Register fica empty
	if(inb(BASE_ADRESS+UART_LSR) & UART_LSR_THRE){
		kfifo_get(seri->wxkf,&tmp,1);
		outb(dev_buf[0], BASE_ADRESS+UART_TX);
	}
	
	while(i<count){
		flag=wait_event_interruptible_timeout(seri->rxwq,count>kfifo_len(seri->wxkf),TIMEOUT);
		//time out if condition=0 and timeout elapsed
		if(!flag){									
			printk(KERN_ALERT "-Write Time Out !!\n");
			break;
		}
		
		kfifo_put(seri->wxkf, &dev_buf[i], 1);	
		i++;
	}
	kfree(dev_buf);
    return i-1;
}


ssize_t seri_read(struct file *filep, char __user *buff, size_t count, loff_t *offp){
    char *dev_buf;
	int flag,i=kfifo_len(seri->rxkf);

	if(count>SIZE_KFIFO){
		return -ENOBUFS;
	}

	if(count>i) dev_buf=(char *)kmalloc(count*sizeof(char), GFP_KERNEL);
	else dev_buf=(char *)kmalloc(i*sizeof(char), GFP_KERNEL);

	if(i==0){
		//Improvement 3.3
		if (filep->f_flags & O_NONBLOCK){
			printk(KERN_ALERT "-Try again!!\n");
			kfree(dev_buf);
			return -EAGAIN;
		}
		while(i<count){
			flag=wait_event_interruptible_timeout(seri->rxwq,kfifo_len(seri->rxkf)>i,TIMEOUT);
			i=kfifo_len(seri->rxkf);
				//time out if condition=0 and timeout elapsed
			if(!flag){									
				printk(KERN_ALERT "-Read Time out !!\n");
				break;
			}
			//Improvement 3.5
			if(flag==-ERESTARTSYS){
				printk(KERN_ALERT "-Exiting read !!\n");
				return -ERESTARTSYS;
			}
		}
	}else printk(KERN_ALERT "-KFIFO buffer has characters!!\n");

	if(inb(BASE_ADRESS+UART_LSR) & UART_LSR_DR & UART_LSR_OE){
		printk(KERN_WARNING "-Over run error !!\n");
		kfree(dev_buf);
		return -EIO;
	}

	if(kfifo_get(seri->rxkf, dev_buf, i) != i){
		printk(KERN_WARNING "-Getting fifo error !!\n");
		kfree(dev_buf);
		return -1;
	}

    if(copy_to_user(buff,dev_buf,count)){
        printk(KERN_WARNING "-Copy error !!\n");
		kfree(dev_buf);
		return -1;
    }
	
	kfree(dev_buf);
    return i;
}

int seri_release (struct inode *inode, struct file *filp){
    printk(KERN_ALERT "-Seri Released !!\n");
	return 0;
}

static int seri_init(void){
	int result = alloc_chrdev_region(&dev,0,N_DEVS, "seri");
	unsigned char lcr=0;
	unsigned long iir=0;
	unsigned long ier=0;

    if (result < 0) {
        printk(KERN_ALERT "-seri: can't alloc!");
        return result; 	//returns negative error code
    }
	
    seri = kmalloc(sizeof(seri_dev_t), GFP_KERNEL);
	cdev_init(&seri->cdev, &fops);
    seri->cdev.ops = &fops;
    seri->cdev.owner = THIS_MODULE;
	spin_lock_init(&seri->rxsl);
	spin_lock_init(&seri->wxsl);
	init_waitqueue_head(&seri->rxwq);
	init_waitqueue_head(&seri->txwq);
	seri->rxkf = kfifo_alloc(SIZE_KFIFO, GFP_KERNEL, &seri->rxsl);
 	seri->wxkf = kfifo_alloc(SIZE_KFIFO, GFP_KERNEL, &seri->wxsl);

    if(cdev_add(&seri->cdev,dev,1)<0){
        printk(KERN_WARNING "-Adding error\n");
        return 0;
    }else printk(KERN_ALERT "-Device Added\n");
    
	if (request_region(BASE_ADRESS, 0x08, "seri")==NULL){
		printk(KERN_WARNING "-Requesting region, error\n");
		return -EBUSY; //error if the I/O port range requested is already being used
	}

	//interrupt-pending clearing
	iir=inb(BASE_ADRESS + UART_IIR);
	if((iir & UART_IIR_NO_INT)==0){		//most hardware devices won’t generate other interrupts until their “interrupt-pending” bit has been cleared.
		iir |= UART_IIR_NO_INT;
		outb(iir, BASE_ADRESS + UART_IIR);
	}

    if(request_irq(IRQ,int_handler,0,"seri", seri)){
        printk(KERN_WARNING"-Requesting interrupt, error");
        return -EBUSY; 					//error if another driver is already using the requested interrupt line
    }
	
	lcr = UART_LCR_WLEN8 | UART_LCR_STOP | UART_LCR_PARITY | UART_LCR_EPAR | UART_LCR_DLAB; 
	outb(lcr, BASE_ADRESS+UART_LCR); 
	
	//setting DL register 115200/b	b=1200
	outb(UART_DIV_1200, BASE_ADRESS+UART_DLL); //UART_DIV_1200=96 colocar no DLL
	outb(0x00, BASE_ADRESS+UART_DLM); 	//colocar 0 no DLM
	
	//reseting DLAB after setting bit rate
	lcr &= ~UART_LCR_DLAB;
	outb(lcr, BASE_ADRESS+UART_LCR);
	
	//interrupt enable 
	ier = inb(BASE_ADRESS+UART_IER);
	ier |= UART_IER_THRI | UART_IER_RDI;
	outb(ier, BASE_ADRESS+UART_IER);
	
	//6.1.2 nao sei se é isto, sera que ainda é preciso?
	while((inb(BASE_ADRESS + UART_LSR) & UART_LSR_THRE)==0){
		schedule();
	}
	return 0;
}

static void seri_exit(void){
	kfifo_free(seri->rxkf);
	kfifo_free(seri->wxkf);
	free_irq(IRQ,seri);
    unregister_chrdev_region (dev,1);
    cdev_del(&seri->cdev);
	release_region(BASE_ADRESS, 0x08);
    printk(KERN_ALERT "-Goodbye.\n");
}


module_init(seri_init);
module_exit(seri_exit);
