//
// Simple GPIO memory-mapped keyboard emulator example by wasti.
// originally from Snarky (github.com/jwatte) and YoungJin Suh (http://valentis.pe.kr / valentis@chollian.net)
// build with:
//  g++ -O1 -g -o keyboard_emu keyboard_emu.cpp -Wall -std=gnu++17
// run with:
//  sudo ./keyboard_emu
//

#include <stdio.h>
#include <stdlib.h>                     // for exit()
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <cstring>
#include <linux/uinput.h>

#include "gpionano.h"

//for emitting keyboard events to the OS
//https://www.kernel.org/doc/html/v4.12/input/uinput.html
void emit(int fk, int type, int code, int val)
{
   struct input_event ie;

   ie.type = type;
   ie.code = code;
   ie.value = val;
   /* timestamp values below are ignored */
   ie.time.tv_sec = 0;
   ie.time.tv_usec = 0;

   write(fk, &ie, sizeof(ie));
}

int main(int argc, char** argv)
{
	//  read physical memory (needs root)
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "usage : $ sudo %s (with root privilege)\n", argv[0]);
        exit(1);
    }
	
	//  set up our emulated keyboard
	struct uinput_setup usetup;
	int fk = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	
	ioctl(fk, UI_SET_EVBIT, EV_KEY);

	//  You must define each key that should exist in our emulated keyboard.
	//	A list of common input event codes can be found at:
	//  https://code.woboq.org/qt5/include/linux/input-event-codes.h.html
	ioctl(fk, UI_SET_KEYBIT, KEY_SPACE);
	ioctl(fk, UI_SET_KEYBIT, KEY_A);
	ioctl(fk, UI_SET_KEYBIT, 48);  // KEY_B is defined as 48


	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor = 0x1234; /* sample vendor */
	usetup.id.product = 0x5678; /* sample product */
	strcpy(usetup.name, "Jetson GPIO");

	//actually initalizes/"plugs in" the keyboard
	ioctl(fk, UI_DEV_SETUP, &usetup);
	ioctl(fk, UI_DEV_CREATE);

    //  map a particular physical address into our address space
    int pagesize = getpagesize();
    int pagemask = pagesize-1;

    //  This page will actually contain all the GPIO controllers, because they are co-located
    void *base = mmap(0, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (GPIO_77 & ~pagemask));
    if (base == NULL) {
        perror("mmap()");
        exit(1);
    }

    //  set up a pointer for convenient access -- this pointer is to the selected GPIO controller
    gpio_t volatile *pinSwt = (gpio_t volatile *)((char *)base + (GPIO_77 & pagemask));
    
    // for Switch : GPIO IN 
    pinSwt->CNF = 0x00FF;
    pinSwt->OE = INPUT;
    pinSwt->IN = 0x00; 			// initial value
    
    //  disable interrupts
    pinSwt->INT_ENB = 0x00;

    // parameter for Input
    pinSwt->INT_STA = 0xFF;		// for Active_low
    pinSwt->INT_LVL = GPIO_INT_LVL_EDGE_BOTH;
    pinSwt->INT_CLR = 0xffffff; //clear any (pre)existing interrupt

	bool switchPressed = 0;
	
	//loop infinitely, and pass input to gpio as keyboard events
	while(true) {
		if(pinSwt->IN>>5 == 0) {
			if(!switchPressed) {
				emit(fk, EV_KEY, KEY_A, 1);
				emit(fk, EV_SYN, SYN_REPORT, 0);
				switchPressed = 1;
			}
		} else {
			if(switchPressed) {
				emit(fk, EV_KEY, KEY_A, 0);
				emit(fk, EV_SYN, SYN_REPORT, 0);
				switchPressed = 0;
			}
		}
	}

    /* unmap */
    munmap(base, pagesize);

    /* close the /dev/mem */
    close(fd);
	
	/* close and destroy the emulated keyboard */
	ioctl(fk, UI_DEV_DESTROY);
	close(fk);
	
    printf("\nGood Bye!!!\n");
    
    return 0;
}
