
#include "sysio.h"
#include "serf.h"
#include "ser.h"
/* VNETI itself and the physical driver & plugin */
#include "phys_cc1100.h"
#include "plug_null.h"
#include "tcv.h"

#define CC1100_BUF_SZ	60

/* session descriptor for the single VNETI session */
int sfd;

/* Base ID */
byte node_id = 1;

struct msg {
  byte sender_id;
  byte receiver_id;
  byte sequence_number;
  byte request_power;
};


fsm receiver {
    address packet;
    word p1,tr;
    byte RSSI, LQI;
    
    state Receiving:
        packet = tcv_rnp(Receiving,sfd);

    /* This part grab the RSSI and LQI from the received packet */
	/* Note RSSI and LQI are the 2 bytes directly after the payload */
    state Measuring:
	p1 = (tcv_left(packet))>>1;
	tr = packet[p1-1];
	RSSI = (byte)(tr>>8);
	LQI = (byte) tr;

    state OK:
        struct msg* payload = (struct msg*)(packet+1);
        ser_outf(NONE,"MSG %d %d %d %d RSSI: %d LQI %d \r\n",payload->sender_id,payload->receiver_id,payload->sequence_number,payload->request_power,RSSI,LQI);
    
        tcv_endp(packet);
        proceed Receiving;
}

fsm root {
    int c;
    struct msg * payload;
    address packet;    

	/*Initialization*/
    state INIT:
        phys_cc1100(0, CC1100_BUF_SZ);
        tcv_plug(0, &plug_null);
        sfd = tcv_open(NONE, 0, 0);
        if (sfd < 0) {
            diag("unable to open TCV session");
            syserror(EASSERT, "no session");
        }
        tcv_control(sfd, PHYSOPT_ON, NULL);
	leds_all(0);
        runfsm receiver;
	
///////////////////////////////////////////////////////////

    state ASK_ser:
	ser_out(ASK_ser, "Please enter the power level (0 to 7):\n\r");
    state WAIT_ser:
        ser_inf(WAIT_ser, "%d",&c);
        payload= (struct msg *)umalloc(sizeof(struct msg));
        payload->sender_id = 1;
        payload->receiver_id = 2;
        payload->sequence_number = 0;
        payload->request_power = (byte) c;

    /*Begin sending the message*/
    state Sending:
        packet = tcv_wnp(Sending, sfd, 8);
        packet[0] = 0;
    
        char * p = (char *)(packet+1);
        *p = payload->sender_id;p++;
        *p = payload->receiver_id;p++;
        *p = payload->sequence_number;p++;
        *p = payload->request_power;
    
    
        tcv_endp(packet);
        ufree(payload);
        proceed WAIT_ser;
    	
    
}
