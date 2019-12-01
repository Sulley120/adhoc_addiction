#include "sysio.h"
#include "serf.h"
#include "ser.h"
#include "phys_cc1100.h"
#include "plug_null.h"
#include "tcvphys.h"
#include "tcv.h"

// Define constants
#define CC1100_BUF_SZ   60
#define Min_RSSI        800
#define Max_Degree      8

// Initialize globals
byte nodeID = -1;
byte pathID;
byte destID;
byte RSSI_C;
byte LQI_C;
byte hopCount;
byte child_array[Max_Degree];
/* TODO: 
Right now nodes slowly increase power until a node in the tree responds.
But a node could potentially have enough power to talk to its parent, but not enough power to talk to its child or a new node
It has the potential to hear a new node because its power is high, but it wont be able to respond because its power is too low
When a node receives a request to join it should increase its power to match the new node it hears.
That way it is guaranteed to be able to talk to its children.
Our sink node also starts at power 0, but the solution above probably solves this.

Mohammed has stressed power consumption should be kept as low as possible at all times, 
but this solution would bump the power of all nodes in range of a new node. Not super power efficient. 
Is there a better idea?

Thank you for coming to my TED Talk.
 */
word power = 0x0000;
int sfd;

// Message struct
struct msg {
	byte nodeID;
	byte destID;
	byte connect;
	byte hopCount;
	byte powerLVL;
};

// Function to create and initialize a msg struct
struct msg * msg_init(byte destID, byte connect, byte hopCount, byte ReadPower) {
	struct msg * node;
	node = (struct msg *)umalloc(sizeof(struct msg));
	node->nodeID = nodeID;
	node->pathID = destID;
	node->connect = connect;
	node->hopCount = hopCount;
	node->powerLVL = ReadPower;

	return node;
}

// Main fsm
fsm root {
	struct msg * payload;
	lword t;
	address packet;
	word p1, tr;
	byte RSSI, LQI;
	byte ReadPower;
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
		payload = msg_init(-1, 1, 0, ReadPower);
		leds(1, 1);

	/* Ask if this node will be the sink */
	state ASK_ser:
		ser_outf(ASK_ser, "Would you like to make this the sink node? (y/n)\n\r");

	/* Wait for response */
	/* TODO: Will all nodes be connected through serial to receive this message?
	Can we just have a timeout, where if nobody answers within a time we assume
	this won't be the sink node? */
	state WAIT_ser:
		ser_inf(WAIT_ser, "%c", &c);
       		/* Sets the sink nodeID to be 0. */
		if(c == 'y') {
			nodeID = 0;
			pathID = 0;
			hopCount = 0;
		}
		payload->nodeID = nodeID;

	// Sends a request to join the tree
	// TODO: Make this conditional on nodeID != 0
	state Sending:
		//Sets timer
		t = seconds();
		packet = tcv_wnp(Sending, sfd, 10);
		packet[0] = 0;

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;

		tcv_endp(packet);
		ufree(payload);

	// Wait to receive a response from a node in the tree
	state Wait_Connection:
		// At end of 1.5 seconds, check if the node received a connection
		// TODO: Is this 90 seconds? Shouldn't it be 1.5?
		if((seconds()-t) > 90){
			// If connection end
			if(count >= 1){
				runfsm receive;
				proceed Connected;	
			}
			//else power up
            proceed Power_Up;
        }
		// RSSI is checked by potential parents
		packet = tcv_rnp(Receive_Connection, sfd);

	// Get information from received packet
	/* TODO: We need to make sure these responses are actually connection responses
	and not just nodes in the tree talking to each other */
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
			// If distance receiveis short look for another
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
		nodeID = payload->destID;

		tcv_endp(packet);
		proceed Wait_Connection;
	
	//increments power until max and if max sends to shutdown.
	state Power_Up:
		if(power == 7){
			proceed Shut_Down;
		}
		power++;

		tcv_control (sfd, PHYSOPT_SETPOWER,(&power);
		proceed Sending;

	// If no suitable nodes respond, shut down.
	// TODO: Should red LED go on?
	state Shut_Down:
		leds_all(0);
		finish;
	
	//generates final connection message
	state Prep_Message:
		
		payload = (struct msg *)umalloc(sizeof(struct msg));
		payload->nodeID = nodeID;
       		payload->destID = destID;
		payload->connect = 1;
		payload->hopCount = hopCount;
		payload->powerLVL = (byte) ReadPower;	

	// Informs parent that it now has a child
	state Connected:
		
		packet = tcv_wnp(sending, sfd, 10);

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;
		

	state End:
		delay(10000000, End);
}

fsm request_response {
	srand((unsigned) time(&t));
	byte newID = (byte) (rand() % 255 + 1);
	struct msg * payload;
	payload = msg_init(newID, 1, hopCount, powerLVL);

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

/* Parent send is the fsm for nodes to send information to their parents
 * if they receive information from their children. */
fsm parent_send {
	struct msg * payload;
	address packet;
	word ReadPower;

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
		// TODO: Update this to new msg_init arguments
		payload = msg_init(ReadPower);

	state Sending:
		//ser_outf(Sending, "THIS IS NOW IN SENDING\n\r");
		packet = tcv_wnp(Sending, sfd, 10);
		packet[0] = 0;
		payload->destID = pathID; // Set node to send to its parent's ID

		// Fill packet:
		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;p++;

		tcv_endp(packet);
		ufree(payload);
		delay(2000, Init_t); //In two seconds, DO IT AGAIN
}

/* Child send is the fsm for nodes to send information to their children
 * if they receive information from their parent. */
fsm child_send {
	struct msg * payload;
	int count = 0;
	address packet;
	word ReadPower;

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
		// TODO: Update this to new msg_init arguments
		payload = msg_init(ReadPower);
		leds(2, 1); // I think LED 2 is yellow; turns on yellow light

	state Sending:
		//ser_outf(Sending, "THIS IS NOW IN SENDING\n\r");
		packet = tcv_wnp(Sending, sfd, 10);
		packet[0] = 0;

		destID = children[count++];
		payload->nodeID = destID; // Set node to send to its changing child ID

		// Fill packet:
		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;p++;

		tcv_endp(packet);
		ufree(payload);

		if (children[count] == NULL) return;

		// Probably don't need two second delay, maybe make it half a second.
		delay(2000, Init_t); //In two seconds, DO IT AGAIN
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


fsm info_sender {

	address packet;
	struct msg * payload;

	state Init_Message:

		payload = (struct msg *)umalloc(sizeof(struct msg));
		payload->nodeID = nodeID
		payload->destID = destID
		payload->connect = connect
		payload->hopConnect = hopConnect;
		payload->powerLVL = power;

	state Sending:

		packet = tcv_wnp(Sending, sfd, 10);
		packet[0] = 0;

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL = power;

		tcv_endp(packet);
		ufree(payload);

	state Wait:
		delay(2000, Init_Message);

}
