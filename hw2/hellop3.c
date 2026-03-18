#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

MODULE_LICENSE("Dual BSD/GPL");

static int a = 1;
static int b = 1;
static char *c = "default";

module_param(a, int, S_IRUGO);
module_param(b, int, S_IRUGO);
module_param(c, charp, S_IRUGO);

static int hellop3_init(void)
{
        int i;
        int total = a * b;

        for (i = 0; i < total; i++) {
                printk(KERN_ALERT "hellop3: %s\n", c);
        }
        return 0;
}

static void hellop3_exit(void)
{
        printk(KERN_ALERT "hellop3 unloaded\n");
}

module_init(hellop3_init);
module_exit(hellop3_exit);
