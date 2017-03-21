#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <string.h>
#include <stdio.h>
static const char *const evval[3] = { "RELEASED", "PRESSED ", "REPEATED"
};
int main(void)
{
    const char *dev =
        //"/dev/input/by-id/usb-_USB_Keyboard-event-kbd";
	"/dev/input/by-path/platform-2-0038-event-";
    struct input_event ev;
    ssize_t n;
    int fd;
    int i;
    fd = open(dev, O_RDONLY);
    if (fd == -1) {
	printf("Cannot open %s: %s.\n", dev, strerror(errno));
	return EXIT_FAILURE;
    }
    for (i = 0; i < 8; i++) {	//while (1) {
	n = read(fd, &ev, sizeof ev);

#if 0
	if (n == (ssize_t) - 1) {
	    if (errno == EINTR)
		continue;

	    else
		break;
	} else
#endif				/*  */
	if (n != sizeof ev) {
	    printf(" err size : %d\n", n);
	    errno = EIO;
	    continue;		//break;
	}
	if (ev.type == EV_KEY && ev.value >= 0 && ev.value <= 2)
	    printf("%s 0x%04x (%d)\n", evval[ev.value], (int) ev.code,
		   (int) ev.code);
        else 
	    printf("not key press, value:0x%08x  0x%04x (%d)\n", ev.value, (int) ev.code,
		   (int) ev.code);
    } fflush(stdout);
    close(fd);

    //fprintf(stderr, "%s.\n", strerror(errno));
    return EXIT_FAILURE;
}
//lrwxrwxrwx 1 root root 9 2014-04-16 13:32 /dev/input/by-id/usb-_USB_Keyboard-event-kbd -> ../event1

