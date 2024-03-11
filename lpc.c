#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <stddef.h>
#include <sys/io.h>

//#define LPC_HICR7               0x1e789088
//#define LPC_HICR8               0x1e78908c
//#define L2AB_WINDOW_SIZE        (1 << 27)

#define SIO_ADDR(base)           (base)
#define SIO_DATA(base)           (base + 1)

enum sio_dev_num
{
    sio_suart1 = 0x02,
    sio_suart2 = 0x03,
    sio_wakeup = 0x04,
    sio_gpio   = 0x07,
    sio_suart3 = 0x0b,
    sio_suart4 = 0x0c,
    sio_ilpc   = 0x0d,  //iLPC2AHB
    sio_mbox   = 0x0e,
};

int lpc_init()
{
    int rc;
    /* YOLO */
    rc = iopl(3);
    if (rc < 0) {
        perror("iopl");
        return rc;
    }
    return 0;
}

int lpc_writeb(size_t addr, uint8_t val)
{
    outb_p(val, addr);
    return 0;
}

int sio_unlock(unsigned short int base)
{
    int rc;

    rc  = lpc_writeb(SIO_ADDR(base), 0xa5);
    rc |= lpc_writeb(SIO_ADDR(base), 0xa5);

    return rc;
}

int lpc_readb(size_t addr, uint8_t *val)
{
    *val = inb_p(addr);

    return 0;
}

int sio_writeb(unsigned char base, uint32_t addr, uint8_t val)
{
    int rc;

    rc = lpc_writeb(SIO_ADDR(base), addr);
    if (rc)
        return rc;

    return lpc_writeb(SIO_DATA(base), val);
}

int sio_readb(unsigned char base, uint32_t addr, uint8_t *val)
{
    int rc;

    rc = lpc_writeb(SIO_ADDR(base), addr);
    if (rc)
        return rc;

    return lpc_readb(SIO_DATA(base), val);
}

int sio_select(unsigned char base, unsigned char log_dev_num)
{
    return sio_writeb(base, 0x07, log_dev_num);
}


int sio_lock(unsigned char base)
{
    return lpc_writeb(SIO_ADDR(base), 0xaa);
}

/* Little-endian */
int ilpcb_readl(unsigned char base, uint32_t addr, uint32_t *val)
{
    uint32_t extracted;
    uint8_t data;
    int locked;
    int rc;

    rc = sio_unlock(base);
    if (rc)
        goto done;

    /* Select iLPC2AHB */
    rc = sio_select(base, sio_ilpc);
    if (rc)
        goto done;

    /* Enable iLPC2AHB */
    rc = sio_writeb(base, 0x30, 0x01);
    if (rc)
        goto done;

    /* 4-byte access */
    rc = sio_writeb(base, 0xf8, 2);
    if (rc)
        goto done;

    /* Address */
    rc |= sio_writeb(base, 0xf0, addr >> 24);
    rc |= sio_writeb(base, 0xf1, addr >> 16);
    rc |= sio_writeb(base, 0xf2, addr >>  8);
    rc |= sio_writeb(base, 0xf3, addr      );
    if (rc)
        goto done;

    /* Trigger */
    rc = sio_readb(base, 0xfe, &data);
    if (rc)
        goto done;

    /* Value */
    extracted = 0;
    rc |= sio_readb(base, 0xf4, &data);
    extracted = (extracted << 8) | data;
    rc |= sio_readb(base, 0xf5, &data);
    extracted = (extracted << 8) | data;
    rc |= sio_readb(base, 0xf6, &data);
    extracted = (extracted << 8) | data;
    rc |= sio_readb(base, 0xf7, &data);
    extracted = (extracted << 8) | data;
    if (rc)
        goto done;

    *val = extracted;

done:
    locked = sio_lock(base);
    if (locked) {
        errno = -locked;
        perror("sio_lock");
    }

    return rc;
}

ssize_t ilpcb_write(unsigned char base, uint32_t addr, unsigned int value)
{
    int locked;
    ssize_t rc;

    rc = sio_unlock(base);
    if (rc)
        goto done;

    /* Select iLPC2AHB */
    rc = sio_select(base, sio_ilpc);
    if (rc)
        goto done;

    /* Enable iLPC2AHB */
    rc = sio_writeb(base, 0x30, 0x01);
    if (rc)
        goto done;

    /* 4-byte access */
    rc = sio_writeb(base, 0xf8, 2);
    if (rc)
        goto done;

    /* Address */
    rc |= sio_writeb(base, 0xf0, addr >> 24);
    rc |= sio_writeb(base, 0xf1, addr >> 16);
    rc |= sio_writeb(base, 0xf2, addr >>  8);
    rc |= sio_writeb(base, 0xf3, addr      );
    if (rc)
        goto done;

    rc = sio_writeb(base, 0xf7, (value >> 0 ) & 0xff);
    rc |= sio_writeb(base, 0xf6, (value >> 8 ) & 0xff);
    rc |= sio_writeb(base, 0xf5, (value >> 16) & 0xff);
    rc |= sio_writeb(base, 0xf4, (value >> 24) & 0xff);
    if (rc)
        goto done;

    /* Trigger */
    rc = sio_writeb(base, 0xfe, 0xcf);
    if (rc)
        goto done;
done:
    locked = sio_lock(base);
    if (locked) {
        errno = -locked;
        perror("sio_lock");
    }

    return 1;
}


int main(int argc,char *argv[])
{
    int rc;
    unsigned int  value = 0x0;
    unsigned char base = 0x2e;
    unsigned int  val = 0x87654;
    unsigned int  reg  = 0x0;
    unsigned char buf[4] = {0,0,0,0};
    rc = lpc_init();
    // ilpcb_readl(base, 0x1e785004, &value);
    // printf("value: 0x%0x\n", value);
    
    // rc = ilpcb_write(base, 0x1e785004, val);
    // ilpcb_readl(base, 0x1e785004, &value);
    // printf("value: 0x%0x\n", value);    
    if(argc < 3)
    {
        printf("usage:\n");
        printf("%s read  reg\n",argv[0]);
        printf("%s write reg value\n",argv[0]);
        return -1;
    }
    else
    {
        if(strncmp(argv[1],"read",strlen("read")) == 0)
        {
            reg = strtol(argv[2], NULL, 16);
            ilpcb_readl(base, reg, &value);
            printf("read reg: 0x%08x, value: 0x%08x\n", reg, value);
        }
        else if(strncmp(argv[1],"write",strlen("write")) == 0)
        {
            reg = strtol(argv[2], NULL, 16);
            value = strtol(argv[3], NULL, 16);
            ilpcb_write(base, reg, value);
            printf("write reg: 0x%08x, value: 0x%08x\n", reg, value);
            ilpcb_readl(base, reg, &value);
            printf("read reg: 0x%08x, value: 0x%08x\n", reg, value);           
        }
    }

    return 0;
}



