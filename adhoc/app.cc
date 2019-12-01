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
byte parentID;
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
// TODO: Do we need a senderID byte for the node that originally sent the message?
struct msg {
	byte nodeID;
	byte destID;
	byte sourceID;
	byte connect;
	byte hopCount;
	byte powerLVL;
};

// Function to create and initialize a msg struct
struct msg * msg_init(byte destID, byte sourceID, byte connect, byte hopCount, byte ReadPower) {
	struct msg * node;
	node = (struct msg *)umalloc(sizeof(struct msg));
	node->nodeID = nodeID;
	node->destID = destID;
	node->sourceID = sourceID;
	node->connect = connect;
	node->hopCount = hopCount;
	node->powerLVL = ReadPower;

	return node;
}

// Main fsm
fsm root {
	struct msg * payload;
	lword t;
	lword timeout;
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
		leds(1, 1);

	/* Ask if this node will be the sink */
	state ASK_ser:
		timeout = seconds()
		ser_outf(ASK_ser, "Would you like to make this the sink node? (y/n)\n\r");

	/* Wait for response */
	state WAIT_ser:

		if(((seconds())-timeout)>30){proceed Sending;}
		ser_inf(WAIT_ser, "%c", &c);
       	/* Sets the sink nodeID to be 0. */
		if(c == 'y') {
			nodeID = 0;
			parentID = 0;
			hopCount = 0;
			// Run the receive fsm and don't try to connect to the tree
			call receive;
			proceed End;
		}
		//payload->nodeID = nodeID;

	// Sends a request to join the tree if not sink node
	state Sending:
		payload = msg_init(-1, nodeID, 1, 0, ReadPower);
		//Sets timer
		t = seconds();
		packet = tcv_wnp(Sending, sfd, 12);
		packet[0] = 0;

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->sourceID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;

		tcv_endp(packet);
		ufree(payload);

	// Wait to receive a response from a node in the tree
	state Wait_Connection:
		// At end of 1.5 seconds, check if the node received a connection
		if((seconds()-t) > 1.5){
			// If connection end
			if(count >= 1){
				call receive;
				proceed Connected;
			}
			//else power up
            proceed Power_Up;
        }
		// RSSI is checked by potential parents
		packet = tcv_rnp(Receive_Connection, sfd);

	// Get information from received packet
	state Measuring:
		p1 = (tcv_left(packet))>>1;
		tr = packet[p1-1];
		RSSI = (byte) (tr>>8);
		LQI = (byte) tr;

	// Compare the responding nodes and only save the best one
	state Update:
		struct msg* payload = (struct msg*)(packet + 1);
		if (payload->connect == 1) {
			// Checks for multiple responses
			count ++;
			if(count > 1){
				leds_all(0);
				leds(2, 1);
				// If the newly received response is further away than a previous response
				if((payload->hopCount + 1) > hopCount){
					proceed Receive_Connection;
				}
			}
			// Updates if better connection
			RSSI_C = RSSI;
			LQI_C = LQI;
			hopCount = payload->hopCount + 1;
			/* parentID is set to the nodeID of the message
			So the node id will always be id of the node who
			sent the message */
			parentID = payload->nodeID;
			nodeID = payload->destID;
		}

		tcv_endp(packet);
		// Go back to listening for connections until 1.5 seconds passes
		proceed Wait_Connection;

	// Increments power until max and if max sends to shutdown.
	state Power_Up:
		if(power == 7){
			proceed Shut_Down;
		}
		power++;
		tcv_control (sfd, PHYSOPT_SETPOWER,(&power));
		proceed Sending;

	// If no suitable nodes respond, shut down.
	state Shut_Down:
		leds_all(0);
		finish;

	// Generates final connection message
	state Prep_Message:
		payload = (struct msg *)umalloc(sizeof(struct msg));
		payload->nodeID = nodeID;
       		payload->destID = destID;
		payload->connect = 1;
		payload->hopCount = hopCount;
		payload->powerLVL = (byte) ReadPower;

	// Informs parent that it now has a child
	state Connected:
		payload = msg_init(destID, nodeID, 1, hopCount, ReadPower);
		packet = tcv_wnp(Connected, sfd, 12);
		packet[0] = 0;

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->sourceID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;

		tcv_endp(packet);
		ufree(payload);
		call receiving;
		
	// If root finishes the whole program stops. Keep root running.
	// TODO: Is there a better way for root to continue infinitely? This will end eventually
	state End:
		delay(10000000, End);
}

/* Sends a connection response to a new node. Generates a random nodeID
and sends that ID in the destID field */
fsm request_response {
	srand((unsigned) time(&t));
	byte newID = (byte) ((rand() % 254) + 1); // Number 1-254
	struct msg * payload;
	payload = msg_init(newID, nodeID, 1, hopCount, powerLVL);

	state Sending:
		packet = tcv_wnp(Sending, sfd, 12);
		packet[0] = 0;

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->sourceID;p++;
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

	/* Initializes the msg packet */
	state Init_t:
		tcv_control (sfd, PHYSOPT_GETPOWER, &ReadPower);
		payload = msg_init(destID, 0, hopCount, ReadPower);

	state Sending:
		packet = tcv_wnp(Sending, sfd, 12);
		packet[0] = 0;
		payload->destID = parentID; // Set node to send to its parent's ID

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
 * if they receive information from their parent.
fsm child_send {
	struct msg * payload;
	int count = 0;
	address packet;
	word ReadPower;

	// Initializes the msg packet
	state Init_t:
		tcv_control (sfd, PHYSOPT_GETPOWER, &ReadPower);

		payload = msg_init(destID, 0, hopCount, ReadPower);
		If so I feel this should be turned on in receive, not when it sends a message to its children 
		leds(2, 1); // I think LED 2 is yellow; turns on yellow light
		payload = msg_init(destID, 1, hopCount, ReadPower);

	state Sending:
		packet = tcv_wnp(Sending, sfd, 12);
		packet[0] = 0;
		destID = child_array[count++];
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

		if (child_array[count] == NULL) return;

		delay(2000, Init_t); //In two seconds, DO IT AGAIN
} */

/* Receives messages from other nodes and acts accordingly */
fsm receive {
	address packet;
	int fromChild;
	struct msg * payload;
	byte readPower;
	word p1, tr;
	byte RSSI, LQI;
	int ledOn = 0;

	state Receiving:
		packet = tcv_rnp(Receiving,sfd);

	/* state to get the RSSI and LQI from the recieved packet. */
	state Measuring:
		p1 = (tcv_left(packet))>>1;
		tr = packet[p1-1];
		RSSI = (byte)(tr>>8);
		LQI = (byte) tr;

	state CheckSource:
		struct msg* payload = (struct msg*)(packet+1);

		/*checks to see if the message is coming from a child node. */
		int i;
		for (i = 0; i < (sizeof(child_array)/sizeof(byte)); i++) {
			if(payload->nodeID == child_array[i]) {
			       	fromChild = 1;
			}
		}
		/* If the message comes from the parent node. */
		if(payload->nodeID == parentID) {
			proceed FromParent
		}

		/* If the message comes from a child node. */
		else if(fromChild == 1) {
			proceed FromChildInit;
		}
		/* If the message comes from a new connection */
		else if (payload->connect == 1) {
			// If RSSI less than the minimum or the max # of children for this node has been reached
			if (RSSI < Min_RSSI || ((sizeof(child_array)/sizeof(byte)) == Max_Degree)) {
				proceed Receiving;
			}
			proceed FromUnknown;
		}

	state FromChildInit:
		// If this is the sink node, print info
		if (nodeID == 0) {
			ser_outf(Receiving, "NodeID: %02X powerLVL: %02X\n\r", payload->nodeID, payload->powerLVL);
			proceed Receiving;
		}
		else {
			payload = msg_init(parentID, payload->sourceID, 0, hopCount, power);
		}

	state FromChild:
		packet = tcv_wnp(Connected, sfd, 12);
		packet[0] = 0;

		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->sourceID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;

		tcv_endp(packet);
		ufree(payload);
		proceed Receiving;

	state FromParentInit:
		ledOn++;
		leds(0, ledOn);
		for (i = 0; i < ((sizeof(child_array)/sizeof(byte))); i++) {
			if (child_array[i] == NULL) {
				break;
			}
			else {
				payload = msg_init(child_array[i], payload->sourceID, 0, hopCount, power);
				packet = tcv_wnp(Connected, sfd, 12);
				packet[0] = 0;

				char * p = (char *)(packet+1);
				*p = payload->nodeID;p++;
				*p = payload->destID;p++;
				*p = payload->sourceID;p++;
				*p = payload->connect;p++;
				*p = payload->hopCount;p++;
				*p = payload->powerLVL;

				tcv_endp(packet);
				ufree(payload);
			}
		}

	state FromUnknown:
		// If the new node's parent is us
		if (payload->parentID == nodeID) {
			/* add child to the node tree updating child_array */
			int i;
			for (i = 0; i < ((sizeof(child_array)/sizeof(byte))); i++) {
				if (child_array[i] == NULL) {
					child_array[i] = payload->nodeID;
					break;
				}
			}
		}
		// If this is a totally new unknown node, send connection response.
		else {
			runfsm request_response;
		}
}
