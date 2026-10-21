#include <libuser.h>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    smp_info_t info;
    memset(&info, 0, sizeof(info));

    if (sys_smp_info(&info) != 0)
    {
        printf("Error: Failed to query SMP CPU topology via sys_smp_info!\n");
        return 1;
    }

    printf("==============================================================\n");
    printf("  myOS Symmetric Multiprocessing (SMP) CPU Topology Report\n");
    printf("==============================================================\n");
    printf(" Total Cores Detected : %u\n", info.total_cpus);
    printf(" Active Online Cores  : %u\n", info.online_cpus);
    printf("--------------------------------------------------------------\n");
    printf("  CORE ID   LAPIC ID   ROLE                     STATUS\n");
    printf("--------------------------------------------------------------\n");

    for (uint32_t i = 0; i < info.total_cpus && i < 16; i++)
    {
        const char* role = info.cpus[i].is_bsp ? "Bootstrap Processor" : "Application Processor";
        const char* status = info.cpus[i].online ? "ONLINE" : "OFFLINE";
        printf("  CPU %-5u %-10u %-24s %s\n",
               info.cpus[i].cpu_id, info.cpus[i].lapic_id, role, status);
    }

    printf("--------------------------------------------------------------\n");
    printf(" Architecture: x86 (IA-32) SMP Multi-Core Execution Engine\n");
    printf("==============================================================\n");

    return 0;
}
