#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include "kyouko_regs.h"

MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("nsoni && neerajj");

#define PCI_VENDOR_ID_CCORSI 0x1234
#define PCI_DEVICE_ID_CCORSI_VGPU 0x1111
#define K_BUFFER_SIZE 0x10000
#define NUM_BUFS 0x08
#define PAGESIZE 0x1000

static DECLARE_WAIT_QUEUE_HEAD(q);

static spinlock_t lock;
static unsigned int current_usr;
static unsigned long flags;
static bool dma_flag=false;
static bool fill_check=false;
static bool mmap_flag = false;

static struct kyouko1 {
	unsigned long p_control_base;
	unsigned int *k_control_base;
	struct cdev whatever;
	struct pci_dev* pci_dev;
}kyouko;

static struct _dma_buffer{
	unsigned int *k_dma_base;
	unsigned long u_dma_base;
	dma_addr_t dma_handler;
	unsigned int count;
}dma_buffer[NUM_BUFS];

static struct _dma_buffer_status{
	unsigned int fill;
	unsigned int drain;
}dma_buffer_status;

unsigned int K_READ_REG (unsigned int reg) {
	unsigned int value;
	udelay(1);
	rmb();
	value = *(kyouko.k_control_base+(reg>>2));
	return (value);
}

void K_WRITE_REG(unsigned int reg, unsigned int val) {
	udelay(1);
	*(kyouko.k_control_base + (reg>>2)) = val;
}

static struct pci_device_id kyouko_dev_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CCORSI,PCI_DEVICE_ID_CCORSI_VGPU) },
	{0}
};

int kyouko_probe(struct pci_dev* pci_dev_1, const struct pci_device_id* pci_id) {
	kyouko.pci_dev = pci_dev_1;
	kyouko.p_control_base = pci_resource_start(kyouko.pci_dev,1);
	if(pci_enable_device(kyouko.pci_dev))
		return -EAGAIN;
	pci_set_master(kyouko.pci_dev);
	return 0;
}

void kyouko_remove(struct pci_dev* pci_dev) { }

struct pci_driver kyouko_pci_drv = {
	.name= "nn",
	.id_table= kyouko_dev_ids,
	.probe= kyouko_probe,
	.remove= kyouko_remove
};

int kyouko_open(struct inode *inode, struct file *fp){
	kyouko.k_control_base = ioremap(kyouko.p_control_base, PAGESIZE);
	printk(KERN_ALERT "opened device \n");
	return 0;
}

int kyouko_mmap(struct file* fp, struct vm_area_struct* vma) {
	if(!mmap_flag){
		current_usr = current_fsuid();
		if(current_usr == 0) {
			printk(KERN_ALERT "Root authenticated!\n");
			io_remap_pfn_range(vma,vma->vm_start,(kyouko.p_control_base)>>PAGE_SHIFT,vma->vm_end - vma->vm_start, vma->vm_page_prot);
			return 0;
		}
		else {
			printk(KERN_ALERT "Danger, not root!\n");
			return -EACCES;
		}
	}
	else{
		io_remap_pfn_range(vma,vma->vm_start,(dma_buffer[dma_buffer_status.fill].dma_handler)>>PAGE_SHIFT,vma->vm_end - vma->vm_start, vma->vm_page_prot);
	return 0;
	}
}

irqreturn_t dma_intr(int irq, void *dev_id, struct pt_regs *regs) {
	unsigned int iflags;
	iflags = K_READ_REG(CfgFlags);
	K_WRITE_REG(CfgFlags, 0);
	if((iflags & 0x01) == 0) {
		return (IRQ_NONE);
	}
	spin_lock_irqsave(&lock, flags);
	dma_buffer_status.drain = (dma_buffer_status.drain+1)%NUM_BUFS;
	if(fill_check == true){
		fill_check = false;
		wake_up_interruptible(&q);
	}
	if(!(fill_check==false && dma_buffer_status.fill==dma_buffer_status.drain)){
		K_WRITE_REG(CmdDMABuffer, dma_buffer[dma_buffer_status.drain].dma_handler);
		K_WRITE_REG(CmdDMACount, dma_buffer[dma_buffer_status.drain].count);
	}
	spin_unlock_irqrestore(&lock, flags);
	return (IRQ_HANDLED);
}

void initiate_transfer(void){
	spin_lock_irqsave(&lock, flags);
	if(dma_buffer_status.fill==dma_buffer_status.drain){
		dma_buffer_status.fill = (dma_buffer_status.fill+1)%NUM_BUFS;
		spin_unlock_irqrestore(&lock, flags);
		K_WRITE_REG(CmdDMABuffer,dma_buffer[dma_buffer_status.drain].dma_handler);
		K_WRITE_REG(CmdDMACount,dma_buffer[dma_buffer_status.drain].count);
		return;
	}
	dma_buffer_status.fill = (dma_buffer_status.fill+1)%NUM_BUFS;
	if(dma_buffer_status.fill==dma_buffer_status.drain){
		fill_check = true;
	}
	spin_unlock_irqrestore(&lock, flags);
	wait_event_interruptible(q, fill_check==false);
	return;
} 

static long kyouko_ioctl(struct file *fp, unsigned int cmd, unsigned long arg){
	int i, result;
	unsigned long count_addr;
	if(_IOC_TYPE(cmd) != 0xCC) {
		printk(KERN_ALERT "DANGER: TYPE of command doesn't match...\n");
		return -EINVAL;
	}

	switch(cmd) {
		case VMODE:
			switch(arg) {
				case GRAPHICS_ON:
					printk(KERN_ALERT "On\n");
					K_WRITE_REG(CfgFlags, 0);
					K_WRITE_REG(CfgMode, 0x03);
					K_WRITE_REG(CfgFrame, 0x1008888);
					K_WRITE_REG(CfgAccel, 0x02);
					K_WRITE_REG(CfgWidth, 800);
					K_WRITE_REG(CfgHeight, 600);
					for(i=0; i<16; i++) K_WRITE_REG(VtxTransform+4*i, 0);
					K_WRITE_REG(VtxTransform+4*0, 0x3f800000);
					K_WRITE_REG(VtxTransform+4*5, 0x3f800000);
					K_WRITE_REG(VtxTransform+4*10, 0xbf800000);
					K_WRITE_REG(VtxTransform+4*15, 0x3f800000);
					K_WRITE_REG(CmdReboot, 1);
					K_WRITE_REG(CmdSync, 1);
					break;
				case GRAPHICS_OFF:
					while(K_READ_REG(InFIFO)>0);
					printk(KERN_ALERT "Off\n");
					K_WRITE_REG(CfgMode, 0);
					K_WRITE_REG(CfgFrame, 0);
					K_WRITE_REG(CfgAccel, 0);
					K_WRITE_REG(CfgWidth, 0);
					K_WRITE_REG(CfgHeight, 0);
					K_WRITE_REG(CmdReboot, 1);
					K_WRITE_REG(CmdSync, 1);
					break;
				default:
					printk(KERN_ALERT "default in VMODE cmd!");
					break;
			}
		break;
		case BIND_DMA:
			if(!dma_flag){
				for(i=0;i<NUM_BUFS;i++){
					dma_buffer[i].k_dma_base = pci_alloc_consistent(NULL, K_BUFFER_SIZE, &dma_buffer[i].dma_handler);
					spin_lock_irqsave(&lock, flags);
					dma_buffer_status.fill = i;
					mmap_flag = true;
					dma_buffer[i].u_dma_base = do_mmap(fp, 0, K_BUFFER_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, 0);
					mmap_flag = false;
					spin_unlock_irqrestore(&lock, flags);
					dma_buffer[i].count = 0;
				}			
				pci_enable_msi(kyouko.pci_dev);			
				result = request_irq(kyouko.pci_dev->irq, (irq_handler_t)dma_intr, IRQF_SHARED|IRQF_DISABLED, "dma_intr", &kyouko);
				dma_flag = true;
				dma_buffer_status.fill = 0;
				dma_buffer_status.drain = 0;
			}
			if(copy_to_user((unsigned long *)arg, &dma_buffer[dma_buffer_status.fill].u_dma_base, sizeof(unsigned long)))
				return -EFAULT;
			break;
		case START_DMA:
			if(!copy_from_user(&count_addr, (unsigned long *)arg, sizeof(unsigned long))){
				if(count_addr!=0){
					dma_buffer[dma_buffer_status.fill].count = (((count_addr<<2)-1)<<1);
					initiate_transfer();
				}
				if(copy_to_user((unsigned long *)arg, &dma_buffer[(dma_buffer_status.fill)%NUM_BUFS].u_dma_base, sizeof(unsigned long)))					
					return -EFAULT;
			}
			else
				return -EFAULT;
			break;
		default:
			printk(KERN_ALERT "default cmd!");
			break;
	}
	return 0;
}

int kyouko_release(struct inode *inode, struct file *fp){
	unsigned int i;
	struct mm_struct *mm = current->mm;
	while(K_READ_REG(InFIFO)>0);
	if(dma_flag) {
		for(i=0;i<NUM_BUFS;i++){
			pci_free_consistent(kyouko.pci_dev, K_BUFFER_SIZE, dma_buffer[i].k_dma_base, dma_buffer[i].dma_handler);
			if(do_munmap(mm, dma_buffer[i].u_dma_base, K_BUFFER_SIZE)) {
				return -ENOMEM;
			}
		}
		free_irq(kyouko.pci_dev->irq, &kyouko);
		pci_disable_msi(kyouko.pci_dev);
	}
	iounmap(kyouko.k_control_base);
	printk(KERN_ALERT "kyouko closed\n");
	return 0;
}

struct file_operations kyouko_fops = {
	.open= kyouko_open,
	.release= kyouko_release,
	.mmap= kyouko_mmap,
	.unlocked_ioctl = kyouko_ioctl,
	.owner= THIS_MODULE
};

static int my_init_function(void){
	printk(KERN_ALERT "initialize kyouko\n");
	
	//---
	// REGISTER CHARACTER DEVICE
	//---
	cdev_init(&(kyouko.whatever),&kyouko_fops);
	cdev_add(&(kyouko.whatever),MKDEV(127,500),1);

	//---
	//SCAN PCI BUS
	//---
	if(pci_register_driver(&kyouko_pci_drv))
		return -1;	
	return 0;
}

static void my_exit_function(void){
	pci_clear_master(kyouko.pci_dev);
	pci_disable_device(kyouko.pci_dev);
	pci_unregister_driver(&kyouko_pci_drv);
	cdev_del(&(kyouko.whatever));
	printk(KERN_ALERT "exit kyouko\n");
}

module_init(my_init_function);
module_exit(my_exit_function);
