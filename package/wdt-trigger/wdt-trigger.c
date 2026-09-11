#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <stdio.h>

int main(int argc, char **argv) {
    const char *dev = (argc > 1) ? argv[1] : "/dev/watchdog";
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("Could not open watchdog dev file");
        return 1;
    }

    int bootstatus = 0;
    ioctl(fd, WDIOC_GETBOOTSTATUS, &bootstatus);
    printf("WDIOC_GETBOOTSTATUS: 0x%x; WDIOF_CARDRESET: 0x%x\n", bootstatus, WDIOF_CARDRESET);

    int timeleft = 0;
    printf("Calling WDIOC_GETTIMELEFT on %s...\n", dev);
    ioctl(fd, WDIOC_GETTIMELEFT, &timeleft);
    printf("timeleft = %d\n", timeleft);

    close(fd);
    return 0;
}