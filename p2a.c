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

#define AST_MMIO_BAR            1
#define AST_PCI_VID             0x1a03
#define AST_PCI_DID_VGA         0x2000
#define AST_PCI_DID_BMC         0x2402
#define AST_MMIO_LEN            (128 * 1024)
#define P2AB_RBAR               0xf004
#define P2AB_PKR                0xf000
#define P2AB_RBAR_REMAP_MASK    0xffff0000
#define P2AB_WINDOW_BASE        0x10000
#define P2AB_WINDOW_LEN         0x10000

struct p2ab {
    //struct ahb ahb;
    int res;
    void *mmio;
    unsigned int rbar;
};


struct p2ab g_p2ab;

int read_sysfs_id(int dirfd, const char *file)
{
	char id_string[7];
	int rc, fd;
	long id;

	fd = openat(dirfd, file, 0);
	if (fd < 0)
		return -1;

	memset(id_string, 0, sizeof(id_string));

	/* vdid format is: "0xABCD" */
	rc = read(fd, id_string, 6);
	close(fd);

	if (rc < 0)
		return -1;

	id = strtol(id_string, NULL, 16);
	if (id > 0xffff || id < 0)
		return -1;
	return id;
}

int pci_close(int fd)
{
    return close(fd);
}

static int __p2ab_writel(struct p2ab *ctx, size_t addr, unsigned int val)
{
    *((uint32_t *)(ctx->mmio + addr)) = htole32(val);
    //iob()
    return 0;
}

static inline int p2ab_unlock(struct p2ab *ctx)
{
    int rc;
    rc = __p2ab_writel(ctx, P2AB_PKR, 1);
    return rc;
}

int pci_open(unsigned short int vid, unsigned short int did, int bar)
{
    char *res;
    int rc;
    int fd;
	int found = 0;

	struct dirent *de;
	int dfd;
	DIR *d;
	char path[300]; /* de->d_name has a max of 255, and add some change */

	d = opendir("/sys/bus/pci/devices/");
	if (!d)
    {
        return -errno;
    }
	
	dfd = dirfd(d); /* we need an FD for openat() */

	while ((de = readdir(d))) {
		int this_vid, this_did;

		if (de->d_type != DT_LNK)
			continue;

		snprintf(path, sizeof(path), "%s/vendor", de->d_name);
		this_vid = read_sysfs_id(dfd, path);

		snprintf(path, sizeof(path), "%s/device", de->d_name);
		this_did = read_sysfs_id(dfd, path);

		if (this_vid == vid && this_did == did) {
            printf("de->d_name: %s\n",de->d_name);
			found = 1;
			break;
		}
	}

	/* NB: dfd and de->d_name are both invalidated by closedir() */
	if (!found) {
		closedir(d);
		return -ENOENT;
	}

	rc = asprintf(&res, "%s/resource%d", de->d_name, bar);
	if (rc == -1) {
		closedir(d);
		return -errno;
	}

	fd = openat(dfd, res, O_RDWR | O_SYNC);
	free(res);
	closedir(d);

    return fd;
}


int p2ab_init(unsigned short int vid, unsigned short int did)
{
    int rc;

    rc = pci_open(vid, did, AST_MMIO_BAR);
    if (rc < 0)
    {
        printf("pci open error: %d\n", rc); 
        return rc;
    }
        

    g_p2ab.res = rc;
    g_p2ab.mmio = mmap(0, AST_MMIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED,
                     g_p2ab.res, 0);

    if (g_p2ab.mmio == MAP_FAILED) 
    {
        rc = -errno;
        printf("map failed...\n");
        goto cleanup_pci;
    }    

    /* ensure the HW and SW rbar values are in sync */
    g_p2ab.rbar = 0;
    __p2ab_writel(&g_p2ab, P2AB_RBAR, g_p2ab.rbar);

    if ((rc = p2ab_unlock(&g_p2ab)) < 0)
    {
        printf("p2ab unlock failed...\n");
        goto cleanup_mmap;
    }
    
    return 0;

cleanup_mmap:
    munmap(g_p2ab.mmio, AST_MMIO_LEN);

cleanup_pci:
    pci_close(g_p2ab.res);

    return rc;
}


unsigned long long p2ab_map(struct p2ab *ctx, uint32_t phys, size_t len)
{
    uint32_t rbar;
    uint32_t offset;
    long long rc;

    rbar = phys & P2AB_RBAR_REMAP_MASK;
    offset = phys & ~P2AB_RBAR_REMAP_MASK;

    if (ctx->rbar == rbar)
        return offset;

    rc = __p2ab_writel(ctx, P2AB_RBAR, rbar);

    if (rc < 0)
        return rc;

    ctx->rbar = rbar;

    return offset;
}


int _p2ab_readl(struct p2ab *ctx, uint32_t phys, uint32_t *val)
{
    //struct p2ab *ctx = to_p2ab(ahb);
    uint32_t le;
    ssize_t rc;

    if (phys & 0x3)
    {
        printf("_p2ab_readl invalid phys\n");
        return -EINVAL;
    }
        

    rc = p2ab_map(ctx, phys, sizeof(*val));
    if (rc < 0)
    {
        printf("p2ab_map failed.\n");
        return rc;
    }
    le = *(uint32_t *)(ctx->mmio + P2AB_WINDOW_BASE + rc);
    *val = le32toh(le);
    return 0;
}

static inline int p2ab_readl(struct p2ab *ctx, unsigned int phys, unsigned int *val)
{
    //int rc = ctx->ops->readl(ctx, phys, val);
    int rc = _p2ab_readl(ctx, phys, val);
    if (!rc) {
        printf("%s: 0x%08"PRIx32": 0x%08"PRIx32"\n", __func__, phys, *val);
    }
    return rc;
}

int p2ab_writel(struct p2ab *ctx, uint32_t phys, uint32_t val)
{
    int rc;

    val = htole32(val);

    rc = p2ab_map(ctx, phys, sizeof(val));
    if (rc < 0)
        return rc;

    *(uint32_t *)(ctx->mmio + P2AB_WINDOW_BASE + rc) = val;
    return 0;
}


int main(int argc,char *argv[])
{
    int rc;
    unsigned int val  = 0x0;
    unsigned int reg  = 0x0;
    unsigned int value = 0x0;

    rc = p2ab_init(AST_PCI_VID, AST_PCI_DID_VGA);

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
            p2ab_readl(&g_p2ab, reg, &value);
            printf("read reg: 0x%08x, value: 0x%08x\n", reg, value);
        }
        else if(strncmp(argv[1],"write",strlen("write")) == 0)
        {
            reg = strtol(argv[2], NULL, 16);
            value = strtol(argv[3], NULL, 16);
            p2ab_writel(&g_p2ab, reg, value);
            printf("write reg: 0x%08x, value: 0x%08x\n", reg, value);
            p2ab_readl(&g_p2ab, reg, &value);
            printf("read reg: 0x%08x, value: 0x%08x\n", reg, value);           
        }
    }
    //p2ab_readl(&g_p2ab, 0x1e785004, &val);
    //p2ab_writel(&g_p2ab,0x1e785004, 0x123 );
    //p2ab_readl(&g_p2ab, 0x1e785004, &val);
    return 0;
}










