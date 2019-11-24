#include "sysio.h"
#include "serf.h"

#include "phys_cc1100.h"
#include "plug_null.h"
#include "tcvphys.h"

#define CC1100_BUF_SZ	60
#define Min_RSSI	800
#define Max_Degree	8
int sfd;

byte nodeID = 1;
byte pathID;
byte child_array[Max_Degree]; 

struct msg {
	byte nodeID;
	byte pathID;
	byte hopCount;
	byte powerLVL;
}



/*Creates a node and assigns it correct struct values */
msg node_init(word ReadPower) {
	
	struct msg * node;
	node = (struct msg *)umalloc(sizeof(struct msg));

	node->nodeID = nodeID;
	node->pathID = 2;
	node->hopcount = 0;
	node->powerLVL = (byte) ReadPower; 
	
	return node;
}

fsm root {

	char c;
	struct *msg payload;
	/* initializes the root */
	state Init:
		phys_cc1100(0, CC1100_BUF_SIZE);
		tcv_plug(0, &plug_null);
		sfd = tcv_open(NONE, 0, 0);
		if (sfd < 0) {
			diag("unable to open TCV session.");
			syserror(EASSERT, "no session");
		}
		tcv_control(sfd, PHYSIOT_ON, NULL);
		

	/* Initializes the msg packet */
	state Init_t:
		tcv_control (sfd, PHYSOPT_SETPOWER, &power);
		tcv_control (sfd, PHYSOPT_GETPOWER, &ReadPower);
		node_init(ReadPower);			
		leds(1, 1);
	/* UART interfacing for sink node connections. */
	state ASK_ser:
		ser_out(ASK_ser, "Would you like to make this the sink node? (y/n)");
	state WAIT_ser:
		ser_inf(WAIT_ser, "%c", &c);
       		/* Sets the sink nodeID to be 0. */
		if(c == 'y') {
			payload->nodeID = 0;
		}
			

	state Sending:

}

fsm additional_send {

		
}

fsm receive {
	address packet;
	int check;
	word p1, tr;
	byte RSSI, LQI;	
	state Receiving:
		packet = tcv_rnp(Receiving,sfd);

	/* state to get the RSSI and LQI from the recieved packet. */
	state Measureing:
		p1 = (tcv_left(packet))>>1;
		tr = packet[p1-1];
		RSSI = (byte)(tr>>8);
		LQI = (byte) tr;

	state OK:
		struct msg* payload = (struct msg*)(packet+1);
		
		/*checks to see if the message is coming from a known node. */
		for (int i = 0; i < (sizeof(child_array)/sizeof(byte)); i++) {
			if(payload->nodeID == child_array[i]) {
			       	check = 1;
				}		
		/* If the message comes from a parent node. */
		if(payload->nodeID == pathID) {

		}

		/* If the message comes from a child node. */
		else if(check == 1) {

		}
		/* If the message comes from a new connection */
		else {
			if (RSSI < Min_RSSI || ((sizeof(child_array)/sizeof(byte)) == Max_Degree)) {
				proceed Receiving;		
			}
			else {
				/* add child to the node tree updating child_array */
		}
}





