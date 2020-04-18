#include "pidmamon.h"

// Global vars
unsigned rev = 0;
int piCores;
int pi_ispi;
uint32_t pi_peri_phys;
uint32_t pi_dram_bus;
uint32_t pi_mem_flag;
uint8_t pi_is_2711;
uint32_t clk_osc_freq;
uint32_t clk_plld_freq;
uint32_t hw_pwm_max_freq;
uint32_t hw_clk_min_freq;
uint32_t hw_clk_max_freq;
uint8_t *dmaMem = 0;
uint8_t *dma15Mem = 0;
uint32_t *dmaEnableMem = 0;
int fdMem = -1;


void printSpaceOrX(uint8_t flag) {
  if (flag) {
    printf("XX|");
  } else {
    printf("  |");
  }
}

// Taken from pigpio.c
/*
2 2  2  2 2 2  1 1 1 1  1 1 1 1  1 1 0 0 0 0 0 0  0 0 0 0
5 4  3  2 1 0  9 8 7 6  5 4 3 2  1 0 9 8 7 6 5 4  3 2 1 0
W W  S  M M M  B B B B  P P P P  T T T T T T T T  R R R R
W  warranty void if either bit is set
S  0=old (bits 0-22 are revision number) 1=new (following fields apply)
M  0=256 1=512 2=1024 3=2GB 4=4GB
B  0=Sony 1=Egoman 2=Embest 3=Sony Japan 4=Embest 5=Stadium
P  0=2835, 1=2836, 2=2837 3=2711
T  0=A 1=B 2=A+ 3=B+ 4=Pi2B 5=Alpha 6=CM1 8=Pi3B 9=Zero a=CM3 c=Zero W
   d=3B+ e=3A+ 10=CM3+ 11=4B
R  PCB board revision
*/
unsigned gpioHardwareRevision(void)
{
   FILE * filp;
   char buf[512];
   char term;

   if (rev) return rev;

   filp = fopen ("/proc/cpuinfo", "r");

   if (filp != NULL)
   {
      while (fgets(buf, sizeof(buf), filp) != NULL)
      {
         if (!strncasecmp("revision\t:", buf, 10))
         {
            if (sscanf(buf+10, "%x%c", &rev, &term) == 2)
            {
               if (term != '\n') rev = 0;
            }
         }
      }
      fclose(filp);
   }

   /* (some) arm64 operating systems get revision number here  */

   if (rev == 0)
   {
      filp = fopen ("/proc/device-tree/system/linux,revision", "r");

      if (filp != NULL)
      {
         uint32_t tmp;
         if (fread(&tmp,1 , 4, filp) == 4)
         {
            /*
               for some reason the value returned by reading
               this /proc entry seems to be big endian,
               convert it.
            */
            rev = ntohl(tmp);
            rev &= 0xFFFFFF; /* mask out warranty bit */
         }
      }
      fclose(filp);
   }

   piCores = 0;
   pi_ispi = 0;
   rev &= 0xFFFFFF; /* mask out warranty bit */

   /* Decode revision code */

   if ((rev & 0x800000) == 0) /* old rev code */
   {
      if (rev < 0x0016) /* all BCM2835 */
      {
         pi_ispi = 1;
         piCores = 1;
         pi_peri_phys = 0x20000000;
         pi_dram_bus  = 0x40000000;
         pi_mem_flag  = 0x0C;
      }
      else
      {
         rev = 0;
      }
   }
   else /* new rev code */
   {
      switch ((rev >> 12) & 0xF)  /* just interested in BCM model */
      {

         case 0x0:   /* BCM2835 */
            pi_ispi = 1;
            piCores = 1;
            pi_peri_phys = 0x20000000;
            pi_dram_bus  = 0x40000000;
            pi_mem_flag  = 0x0C;
            break;

         case 0x1:   /* BCM2836 */
         case 0x2:   /* BCM2837 */
            pi_ispi = 1;
            piCores = 4;
            pi_peri_phys = 0x3F000000;
            pi_dram_bus  = 0xC0000000;
            pi_mem_flag  = 0x04;
            break;

         case 0x3:   /* BCM2711 */
            pi_ispi = 1;
            piCores = 4;
            pi_peri_phys = 0xFE000000;
            pi_dram_bus  = 0xC0000000;
            pi_mem_flag  = 0x04;
            pi_is_2711   = 1;
            clk_osc_freq = CLK_OSC_FREQ_2711;
            clk_plld_freq = CLK_PLLD_FREQ_2711;
            hw_pwm_max_freq = PI_HW_PWM_MAX_FREQ_2711;
            hw_clk_min_freq = PI_HW_CLK_MIN_FREQ_2711;
            hw_clk_max_freq = PI_HW_CLK_MAX_FREQ_2711;
            break;

         default:
            rev=0;
            pi_ispi = 0;
            break;
      }
   }

   return rev;
}


void readEnabled() {
   uint32_t enabled = *dmaEnableMem;
   printf("|ENABLED       |");
   for (int i = 0; i <= 14; i++) {
     printSpaceOrX(((enabled >> i) & 0x01));
   }
   // DMA15 doesn*t have an entry in the ENABLE reg
   printf("??|  %08x\n", enabled);
}

void readDMAStatus() {

}

void readReservedViaMailbox() {
  int fd = open("/dev/vcio", 0);
  if (fd == -1)
  {
    printf("Unable to open /dev/vcio, please run as root");
    exit(1);
  }

  uint32_t property[32] =
  {
    0x00000000,
    0x00000000,
    0x00060001,
    0x00000004,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
  };

  property[0] = 10 * sizeof(property[0]);

  if (ioctl(fd, _IOWR(100, 0, char *), property) == -1)
  {
    printf("Unable to ioctl, lease run as root.");
    exit(1);
  }

  close(fd);

  uint32_t dmaReserved = property[5];
  //printf("DMASTAT: %04x\n", dmaReserved);
  printf("|RESERVED BY FW|");
  for (int i = 0; i <= 15; i++) {
    printSpaceOrX(!((dmaReserved >> i) & 0x01));
  }
  printf("  %08x\n", dmaReserved);
}

void printHeaderOrFooter() {
  printf("+--------------+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+\n");
  printf("| DMA CHANNEL  |00|01|02|03|04|05|06|07|08|09|10|11|12|13|14|15|\n");
  printf("+--------------+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+\n");
}

int main() {
  gpioHardwareRevision();
  if ((fdMem = open("/dev/mem", O_RDWR | O_SYNC) ) < 0)
  {
    printf("Unable to open /dev/mem, please run as root");
    exit(1);
  }
  dmaMem = (uint8_t*)mmap(0, DMA_LEN, PROT_READ, MAP_SHARED, fdMem, DMA_BASE);
  dmaEnableMem = (uint32_t*)(dmaMem + 0xFF0);
  //printf("DMA MEM Locally AT %08x\n", dmaMem);
  //printf("DMA ENABLE MEM Locally AT %08x\n", dmaEnableMem);
  printf("DMA ENABLE: %08x\n", *dmaEnableMem);


  printHeaderOrFooter();

  readReservedViaMailbox();
  readEnabled();

  printHeaderOrFooter();

  close(fdMem);
}
