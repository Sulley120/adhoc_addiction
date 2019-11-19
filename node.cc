
#include "sysio.h"
#include "serf.h"

/* VNETI itself and the physical driver & plugin */
#include "phys_cc1100.h"
#include "plug_null.h"
#include "tcvphys.h"

#define CC1100_BUF_SZ	60

/* session descriptor for the single VNETI session */
int sfd;

/* node ID */
byte node_id = 2;
word power = 0x0007;

struct msg {
  byte sender_id;
  byte receiver_id;
  byte sequence_number;
    byte request_power;
};


fsm receiver{
	address packet;

    state Receiving:
        packet = tcv_rnp(Receiving,sfd);
    state OK:
        struct msg* payload = (struct msg*)(packet+1);
        if(payload->sender_id==0x01)
        {	
        //////// Readjust power level
	power = (word) payload->request_power;
	if(power > 7) power=7;
	///////////////////////////////////
        }
    
        tcv_endp(packet);
        proceed Receiving;

}

fsm root {
    byte count = 0;
    struct msg * payload;
    address packet;
    word ReadPower;
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
	

///////////////////////////////////
    state Init_t:
	tcv_control (sfd, PHYSOPT_SETPOWER,&power);
	tcv_control (sfd, PHYSOPT_GETPOWER,&ReadPower);
        payload= (struct msg *)umalloc(sizeof(struct msg));
        payload->sender_id = 2;
        payload->receiver_id = 1;
        payload->sequence_number = count;
        payload->request_power = (byte) ReadPower;
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
        if(count == 100)
            count=0;
        else
            count++;
        delay(2000,Init_t);

}
