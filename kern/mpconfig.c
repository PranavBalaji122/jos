// Search for and parse the multiprocessor configuration table
// See http://developer.intel.com/design/pentium/datashts/24201606.pdf

#include <inc/types.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/env.h>
#include <kern/cpu.h>
#include <kern/pmap.h>

struct CpuInfo cpus[NCPU];
struct CpuInfo *bootcpu;
int ismp;
int ncpu;

// Per-CPU kernel stacks
unsigned char percpu_kstacks[NCPU][KSTKSIZE]
__attribute__ ((aligned(PGSIZE)));


// See MultiProcessor Specification Version 1.[14]

struct mp {             // floating pointer [MP 4.1]
	uint8_t signature[4];           // "_MP_"
	physaddr_t physaddr;            // phys addr of MP config table
	uint8_t length;                 // 1
	uint8_t specrev;                // [14]
	uint8_t checksum;               // all bytes must add up to 0
	uint8_t type;                   // MP system config type
	uint8_t imcrp;
	uint8_t reserved[3];
} __attribute__((__packed__));

struct mpconf {         // configuration table header [MP 4.2]
	uint8_t signature[4];           // "PCMP"
	uint16_t length;                // total table length
	uint8_t version;                // [14]
	uint8_t checksum;               // all bytes must add up to 0
	uint8_t product[20];            // product id
	physaddr_t oemtable;            // OEM table pointer
	uint16_t oemlength;             // OEM table length
	uint16_t entry;                 // entry count
	physaddr_t lapicaddr;           // address of local APIC
	uint16_t xlength;               // extended table length
	uint8_t xchecksum;              // extended table checksum
	uint8_t reserved;
	uint8_t entries[0];             // table entries
} __attribute__((__packed__));

struct mpproc {         // processor table entry [MP 4.3.1]
	uint8_t type;                   // entry type (0)
	uint8_t apicid;                 // local APIC id
	uint8_t version;                // local APIC version
	uint8_t flags;                  // CPU flags
	uint8_t signature[4];           // CPU signature
	uint32_t feature;               // feature flags from CPUID instruction
	uint8_t reserved[8];
} __attribute__((__packed__));

// mpproc flags
#define MPPROC_BOOT 0x02                // This mpproc is the bootstrap processor

// Table entry types
#define MPPROC    0x00  // One per processor
#define MPBUS     0x01  // One per bus
#define MPIOAPIC  0x02  // One per I/O APIC
#define MPIOINTR  0x03  // One per bus interrupt source
#define MPLINTR   0x04  // One per system interrupt source

// ACPI structures for MADT fallback when MP table only reports 1 CPU
struct acpi_rsdp {
	uint8_t signature[8];       // "RSD PTR "
	uint8_t checksum;
	uint8_t oemid[6];
	uint8_t revision;
	uint32_t rsdt_addr;
} __attribute__((__packed__));

struct acpi_header {
	uint8_t signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	uint8_t oemid[6];
	uint8_t oemtableid[8];
	uint32_t oemrev;
	uint32_t creatorid;
	uint32_t creatorrev;
} __attribute__((__packed__));

struct acpi_madt {
	struct acpi_header header;
	uint32_t lapic_addr;
	uint32_t flags;
	// variable-length entries follow
} __attribute__((__packed__));

#define MADT_TYPE_LAPIC 0

struct madt_lapic {
	uint8_t type;
	uint8_t length;
	uint8_t acpi_id;
	uint8_t apic_id;
	uint32_t flags;         // bit 0 = enabled
} __attribute__((__packed__));

static uint8_t
sum(void *addr, int len)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < len; i++)
		sum += ((uint8_t *)addr)[i];
	return sum;
}

// Find the ACPI RSDP by searching EBDA and BIOS ROM area.
static struct acpi_rsdp *
acpi_find_rsdp(void)
{
	struct acpi_rsdp *rsdp;
	uint8_t *bda = (uint8_t *)KADDR(0x40 << 4);
	uint32_t ebda;
	uint8_t *p, *end;

	// Search first KB of EBDA
	ebda = *(uint16_t *)(bda + 0x0E);
	if (ebda) {
		p = (uint8_t *)KADDR(ebda << 4);
		end = p + 1024;
		for (; p < end; p += 16) {
			rsdp = (struct acpi_rsdp *)p;
			if (memcmp(rsdp->signature, "RSD PTR ", 8) == 0 &&
			    sum(rsdp, 20) == 0)
				return rsdp;
		}
	}

	// Search BIOS ROM area
	p = (uint8_t *)KADDR(0xE0000);
	end = (uint8_t *)KADDR(0x100000);
	for (; p < end; p += 16) {
		rsdp = (struct acpi_rsdp *)p;
		if (memcmp(rsdp->signature, "RSD PTR ", 8) == 0 &&
		    sum(rsdp, 20) == 0)
			return rsdp;
	}
	return NULL;
}

// Parse ACPI MADT to discover CPUs.
// Returns the number of enabled CPUs found, or 0 on failure.
static int
acpi_madt_detect_cpus(void)
{
	struct acpi_rsdp *rsdp;
	struct acpi_header *rsdt;
	struct acpi_madt *madt = NULL;
	uint32_t *entries;
	int nentries, i;

	rsdp = acpi_find_rsdp();
	if (!rsdp)
		return 0;

	rsdt = (struct acpi_header *)KADDR(rsdp->rsdt_addr);
	if (memcmp(rsdt->signature, "RSDT", 4) != 0 || sum(rsdt, rsdt->length) != 0)
		return 0;

	nentries = (rsdt->length - sizeof(struct acpi_header)) / 4;
	entries = (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_header));

	for (i = 0; i < nentries; i++) {
		struct acpi_header *h = (struct acpi_header *)KADDR(entries[i]);
		if (memcmp(h->signature, "APIC", 4) == 0) {
			madt = (struct acpi_madt *)h;
			break;
		}
	}

	if (!madt)
		return 0;

	// Parse MADT entries to find Local APIC entries
	int found = 0;
	uint8_t *p = (uint8_t *)madt + sizeof(struct acpi_madt);
	uint8_t *end = (uint8_t *)madt + madt->header.length;

	lapicaddr = madt->lapic_addr;

	while (p < end) {
		uint8_t type = p[0];
		uint8_t len = p[1];
		if (len < 2) break;

		if (type == MADT_TYPE_LAPIC && len >= sizeof(struct madt_lapic)) {
			struct madt_lapic *la = (struct madt_lapic *)p;
			if (la->flags & 1) {  // enabled
				if (found < NCPU) {
					cpus[found].cpu_id = found;
					if (found == 0)
						bootcpu = &cpus[0];
					found++;
				}
			}
		}
		p += len;
	}
	return found;
}

// Look for an MP structure in the len bytes at physical address addr.
static struct mp *
mpsearch1(physaddr_t a, int len)
{
	struct mp *mp = KADDR(a), *end = KADDR(a + len);

	for (; mp < end; mp++)
		if (memcmp(mp->signature, "_MP_", 4) == 0 &&
		    sum(mp, sizeof(*mp)) == 0)
			return mp;
	return NULL;
}

// Search for the MP Floating Pointer Structure, which according to
// [MP 4] is in one of the following three locations:
// 1) in the first KB of the EBDA;
// 2) if there is no EBDA, in the last KB of system base memory;
// 3) in the BIOS ROM between 0xE0000 and 0xFFFFF.
static struct mp *
mpsearch(void)
{
	uint8_t *bda;
	uint32_t p;
	struct mp *mp;

	static_assert(sizeof(*mp) == 16);

	// The BIOS data area lives in 16-bit segment 0x40.
	bda = (uint8_t *) KADDR(0x40 << 4);

	// [MP 4] The 16-bit segment of the EBDA is in the two bytes
	// starting at byte 0x0E of the BDA.  0 if not present.
	if ((p = *(uint16_t *) (bda + 0x0E))) {
		p <<= 4;	// Translate from segment to PA
		if ((mp = mpsearch1(p, 1024)))
			return mp;
	} else {
		// The size of base memory, in KB is in the two bytes
		// starting at 0x13 of the BDA.
		p = *(uint16_t *) (bda + 0x13) * 1024;
		if ((mp = mpsearch1(p - 1024, 1024)))
			return mp;
	}
	return mpsearch1(0xF0000, 0x10000);
}

// Search for an MP configuration table.  For now, don't accept the
// default configurations (physaddr == 0).
// Check for the correct signature, checksum, and version.
static struct mpconf *
mpconfig(struct mp **pmp)
{
	struct mpconf *conf;
	struct mp *mp;

	if ((mp = mpsearch()) == 0)
		return NULL;
	if (mp->physaddr == 0 || mp->type != 0) {
		cprintf("SMP: Default configurations not implemented\n");
		return NULL;
	}
	conf = (struct mpconf *) KADDR(mp->physaddr);
	if (memcmp(conf, "PCMP", 4) != 0) {
		cprintf("SMP: Incorrect MP configuration table signature\n");
		return NULL;
	}
	if (sum(conf, conf->length) != 0) {
		cprintf("SMP: Bad MP configuration checksum\n");
		return NULL;
	}
	if (conf->version != 1 && conf->version != 4) {
		cprintf("SMP: Unsupported MP version %d\n", conf->version);
		return NULL;
	}
	if ((sum((uint8_t *)conf + conf->length, conf->xlength) + conf->xchecksum) & 0xff) {
		cprintf("SMP: Bad MP configuration extended checksum\n");
		return NULL;
	}
	*pmp = mp;
	return conf;
}

void
mp_init(void)
{
	struct mp *mp;
	struct mpconf *conf;
	struct mpproc *proc;
	uint8_t *p;
	unsigned int i;

	bootcpu = &cpus[0];
	if ((conf = mpconfig(&mp)) == 0)
		return;
	ismp = 1;
	lapicaddr = conf->lapicaddr;

	for (p = conf->entries, i = 0; i < conf->entry; i++) {
		switch (*p) {
		case MPPROC:
			proc = (struct mpproc *)p;
			if (proc->flags & MPPROC_BOOT)
				bootcpu = &cpus[ncpu];
			if (ncpu < NCPU) {
				cpus[ncpu].cpu_id = ncpu;
				ncpu++;
			} else {
				cprintf("SMP: too many CPUs, CPU %d disabled\n",
					proc->apicid);
			}
			p += sizeof(struct mpproc);
			continue;
		case MPBUS:
		case MPIOAPIC:
		case MPIOINTR:
		case MPLINTR:
			p += 8;
			continue;
		default:
			cprintf("mpinit: unknown config type %x\n", *p);
			ismp = 0;
			i = conf->entry;
		}
	}

	bootcpu->cpu_status = CPU_STARTED;
	if (!ismp) {
		// Didn't like what we found; fall back to no MP.
		ncpu = 1;
		lapicaddr = 0;
		cprintf("SMP: configuration not found, SMP disabled\n");
		return;
	}

	// ACPI MADT fallback: if the MP table only reports 1 CPU,
	// try ACPI MADT which may have the correct count.
	if (ncpu == 1) {
		int acpi_ncpu = acpi_madt_detect_cpus();
		if (acpi_ncpu > 1) {
			ncpu = acpi_ncpu;
			bootcpu = &cpus[0];
			bootcpu->cpu_status = CPU_STARTED;
		}
	}

	cprintf("SMP: CPU %d found %d CPU(s)\n", bootcpu->cpu_id,  ncpu);

	if (mp->imcrp) {
		// [MP 3.2.6.1] If the hardware implements PIC mode,
		// switch to getting interrupts from the LAPIC.
		cprintf("SMP: Setting IMCR to switch from PIC mode to symmetric I/O mode\n");
		outb(0x22, 0x70);   // Select IMCR
		outb(0x23, inb(0x23) | 1);  // Mask external interrupts.
	}
}
