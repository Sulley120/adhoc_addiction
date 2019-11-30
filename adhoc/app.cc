#include "sysio.h"
#include "serf.h"
#include "ser.h"

#include "phys_cc1100.h"
#include "plug_null.h"
#include "tcvphys.h"
#include "tcv.h"

#define CC1100_BUF_SZ   60
#define Min_RSSI        800
#define Max_Degree      8

byte nodeID = -1;
byte pathID;
byte RSSI_C;
byte LQI_C;
byte hopCount;
byte child_array[Max_Degree];
word power = 0x0000;
int sfd;



struct msg {
	byte nodeID;
	byte pathID;
	byte connect;
	byte hopCount;
	word powerLVL;
};



/*Creates a node and assigns it correct struct values */
struct msg * node_init(byte destID, byte connect, byte hopCount, word ReadPower) {
	struct msg * node;
	node = (struct msg *)umalloc(sizeof(struct msg));
	node->nodeID = nodeID;
	node->pathID = destID;
	node->connect = connect;
	node->hopCount = hopCount;
	node->powerLVL = (byte) ReadPower;

	return node;
}

fsm root {

	struct msg * payload;
	lword t;
	address packet;
	word p1, tr;
	byte RSSI, LQI;
	word ReadPower;
	int count = 0;
	char c;

	/* initializes the root */
	state Init:

		phys_cc1100(0, CC1100_BUF_SZ); 
		tcv_plug(0, &plug_null);
		sfd = tcv_open(NONE, 0, 0);
		if (sfd < 0) {
			diag("unable to open TCV session.");
			syserror(EASSERT, "no session");
		}
		tcv_control(sfd, PHYSOPT_ON, NULL);
		

	/* Initializes the msg packet */
	state Init_t:
		tcv_control (sfd, PHYSOPT_SETPOWER, &power);
		tcv_control (sfd, PHYSOPT_GETPOWER, &ReadPower);		
		payload = node_init(-1, 1, 0, ReadPower);
		leds(1, 1);
	/* UART interfacing for sink node connections. */
	state ASK_ser:
		//ser_outf(ASK_ser, "NODE ID IS %x\n\r", nodeID);
		ser_outf(ASK_ser, "Would you like to make this the sink node? (y/n)\n\r");
	state WAIT_ser:
		ser_inf(WAIT_ser, "%c", &c);
       		/* Sets the sink nodeID to be 0. */
		if(c == 'y') {
			nodeID = 0;
		}
		payload->nodeID = nodeID;
		//ser_outf(ASK_ser, "THIS IS NOW A SINK NODE\n\r");

	// Sends a request to join the tree
	// TODO: Make this conditional on nodeID != 0
	state Sending:
		//ser_outf(Sending, "THIS IS NOW IN SENDING\n\r");
		//Sets timer
		t = seconds();
		packet = tcv_wnp(Sending, sfd, 10);
		packet[0] = 0;

		
		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->pathID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;p++;

		tcv_endp(packet);
		ufree(payload);

	// Node waits for response from a node in the tree
	state Receive_Connection:
		//leds(2,1);
		//mdelay(1000);
		//RSSI is check by potential parents
		packet = tcv_rnp(Receive_Connection, sfd);
		
		//Checks timer
		if((seconds()-t) > 90){
			proceed Power_Up; 
		}
	// Get info from responding node
	state Measuring:

		p1 = (tcv_left(packet))>>1;
		tr = packet[p1-1];
		RSSI = (byte) (tr>>8);
		LQI = (byte) tr;

	// Compare the responding nodes and only save the best one
	state Update:
		//Parent is set to the node id of the message
		//So the node id will always be id of the node who
		//sent the message
		struct msg* payload = (struct msg*)(packet + 1);

		//checks for multiple connections
		count ++;
		if(count > 1){
			leds_all(0);
			leds(3, 1);
			// If distance is short look for another
			// connection
			if((payload->hopCount + 1) > hopCount){
				proceed Receive_Connection;
			}
		}
		//updates if better connection
		RSSI_C = RSSI;
		LQI_C = LQI;
		hopCount = payload->hopCount + 1;
		pathID = payload->nodeID;

		tcv_endp(packet);
		proceed Receive_Connection;

	//increments power until max and if max sends to shutdown.
	state Power_Up:
		word cur_power;
		tcv_control (sfd, PHYSOPT_GETPOWER,(&cur_power));
		if(power == 7){
			proceed Shut_Down;
		}

		tcv_control (sfd, PHYSOPT_SETPOWER,(&cur_power + 1));
		proceed Sending;

	// If no suitable nodes respond, shut down.
	// TODO: Should red LED go on?
	state Shut_Down:
		leds_all(0);
		finish;

}

fsm request_response {
	srand((unsigned) time(&t));
	byte newID = (byte) (rand() % 255 + 1);
	struct msg * payload;
	payload = node_init(newID, 1, hopCount, powerLVL);

	state Sending:
		packet = tcv_wnp(Sending, sfd, 10);
		packet[0] = 0;

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->pathID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;

		tcv_endp(packet);
		ufree(payload);
		finish;
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

		/*checks to see if the message is coming from a child node. */
		int i;
		for (i = 0; i < (sizeof(child_array)/sizeof(byte)); i++) {
			if(payload->nodeID == child_array[i]) {
			       	check = 1;
			}
		}
		/* If the message comes from a parent node. */
		if(payload->nodeID == pathID) {
			leds(2, 1);

		}

		/* If the message comes from a child node. */
		else if(check == 1) {

		}
		/* If the message comes from a new connection */
		else if (payload->connect == 1) {
			if (RSSI < Min_RSSI || ((sizeof(child_array)/sizeof(byte)) == Max_Degree)) {
				proceed Receiving;
			}
			// If the new node's parent is us
			if (payload->pathID == nodeID) {
				/* add child to the node tree updating child_array */
				int i;
				for (i = 0; i < ((sizeof(child_array)/sizeof(byte))); i++) {
					if (child_array[i] == NULL) {
						child_array[i] = payload->nodeID;
						break;
					}
				}
			}
			else {
				// TODO: If received message has connect == 1 
				runfsm request_response;
			}
		}
}
