#include "sysio.h"
#include "serf.h"
#include "ser.h"
#include "phys_cc1100.h"
#include "plug_null.h"
#include "tcvphys.h"
#include "tcv.h"

// Define constants
#define CC1100_BUF_SZ   60
#define Min_RSSI        100
#define Max_Degree      8

// Initialize globals
byte nodeID = -1;
byte parentID;
byte destID;
byte RSSI_C;
byte LQI_C;
byte hopCount;
byte child_array[Max_Degree];

struct msg * network_nodes[Max_Degree];
int numChildren = 0;
int numNode = 0;
int yLED_toggle = 0; // For toggling yellow LED through sink
int sfd;
word power = 0x0000;

// Message struct
struct msg {
	byte nodeID;
	byte destID;
	byte sourceID;
	byte connect;
	byte hopCount;
	byte powerLVL;
};

// Function to create and initialize a msg struct
struct msg * msg_init(byte destID, byte sourceID, byte connect, byte hopCount, word ReadPower) {
	struct msg * node;
	node = (struct msg *)umalloc(sizeof(struct msg));
	node->nodeID = nodeID;
	node->destID = destID;
	node->sourceID = sourceID;
	node->connect = connect;
	node->hopCount = hopCount;
	node->powerLVL = (byte) ReadPower;

	return node;
}

/* Sends a connection response to a new node. Generates a random nodeID
and sends that ID in the destID field */
fsm request_response {
	address packet;
	struct msg * payload;

	state Init:
		byte newID = (byte) ((rnd() % 254) + 1); // Number 1-254
		payload = msg_init(newID, nodeID, 1, hopCount, power);

	state Sending:
		// Debug message to print out every variable
		//ser_outf(Sending, "SENDING RESPONSE, MSG is NEWNODE: %x  SOURCENODE: %x  CONNECT: %x\n\rHOPCOUNT: %x POWER: %x\n\r\n\r", payload->destID, payload->sourceID, payload->connect, payload->hopCount, payload->powerLVL);
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

	state End:
		ser_outf(End, "FINISHED SENDING RESPONSE\n\r");	
		finish;
}

/* Parent send is the fsm for nodes to send information to their parents
 * if they receive information from their children. */
fsm parent_send {
	struct msg * payload;
	address packet;

	/* Initializes the msg packet */
	state Init_t:
		ser_outf(Init_t, "WE ARE INSIDE PARENT SEND NOW\n\r");
		payload = msg_init(destID, nodeID, 0, hopCount, power);

	state Sending:
		packet = tcv_wnp(Sending, sfd, 12);
		packet[0] = 0;
		payload->destID = parentID; // Set node to send to its parent's ID

		// Fill packet:
		char * p = (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->destID;p++;
		*p = payload->sourceID;p++;
		*p = payload->connect;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;p++;

		tcv_endp(packet);
		ufree(payload);

	state End:
		ser_outf(End, "SENT TO PARENT SUCCESSFULLY\n\r");	
		delay(2000, Init_t); //In two seconds, begin initializing and send another message
}

/* Receives messages from other nodes and acts accordingly */
fsm receive {
	address packet;
	int fromChild;
	struct msg * payload;
	word p1, tr;
	byte RSSI, LQI;

	// Initalize payload vars locally within fsm as it is easier to work with them
	byte payload_powerLVL;
	byte payload_nodeID;
	byte payload_connect;
	byte payload_sourceID;
	byte payload_hopCount;
	byte payload_destID;

	state Receiving:
		packet = tcv_rnp(Receiving, sfd);

	/* state to get the RSSI and LQI from the recieved packet. */
	state Measuring:
		p1 = (tcv_left(packet))>>1;
		tr = packet[p1-1];
		RSSI = (byte)(tr>>8);
		LQI = (byte) tr;

	state CheckSource:
		struct msg* payload = (struct msg*)(packet+1);
		payload_powerLVL = payload->powerLVL;
		payload_nodeID = payload->nodeID;
		payload_connect = payload->connect;
		payload_sourceID = payload->sourceID;
		payload_hopCount = payload->hopCount;
		payload_destID = payload->destID;
		ser_outf(CheckSource, "CHECKSOURCE STATE, PAYLOAD:\n\rsourceID: %x   nodeID: %x   DestID: %x   POWER: %x   CONNECT: %x   RSSI: %d\n\r", payload_sourceID, payload_nodeID, payload_destID, payload_powerLVL, payload_connect, (int)RSSI);

		/*checks to see if the message is coming from a child node. */
		int i;
		for (i = 0; i < numChildren; i++) {
			if(payload->nodeID == child_array[i]) {
				fromChild = 1;
			}
		}
		/* If the message comes from the parent node. */
		if((payload_nodeID == parentID) && (payload_destID == nodeID)) {
			proceed FromParent;
		}

		/* If the message comes from a child node. */
		else if(fromChild == 1) {
			proceed FromChildInit;
		}
		/* If the message comes from a new connection */
		else if (((int)payload_connect) == 1) {
			// If RSSI less than the minimum or the max # of children for this node has been reached
			if (RSSI < Min_RSSI || numChildren == Max_Degree) {
				proceed Receiving;
			}
			proceed FromUnknown;
		}

	state FromChildInit:
		ser_outf(FromChildInit, "FROM CHILD STATE\n\r");
		// If this is the sink node, print info
		if (nodeID == 0) {
			int j;
			for(j = 0; j < numNode; j++) {
				if(payload->sourceID == network_nodes[j]->sourceID) {
					network_nodes[j] = payload;
					proceed Receiving;
				}
			}
			network_nodes[numNode] = payload;
			numNode++;
			ser_outf(Receiving, "NodeID: %x powerLVL: %x\n\r", payload->nodeID, payload->powerLVL);
			proceed Receiving;
		}
		else {
			payload = msg_init(parentID, payload_sourceID, 0, hopCount, power);
		}

	state FromChild:
		packet = tcv_wnp(FromChild, sfd, 12);
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

	state FromParent:
		ser_outf(FromParent, "FROM PARENT STATE\n\r");

		yLED_toggle++;
		leds(0, (yLED_toggle%2));

		int i;
		for (i = 0; i < numChildren; i++) {
			payload = msg_init(child_array[i], payload_sourceID, 0, hopCount, power);
			packet = tcv_wnp(FromParent, sfd, 12);
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
		proceed Receiving;

	state FromUnknown:
		ser_outf(FromUnknown, "FROM UNKNOWN STATE, NODE ID IS: %x    MSG DEST ID IS: %x\n\r", nodeID, payload_destID);
		if ((word)payload_powerLVL > power) {
			power = (word)payload_powerLVL;
			tcv_control (sfd, PHYSOPT_SETPOWER, &power);
		}
		// If the new node's parent is us
		if (payload_destID == nodeID) {
			/* add child to the node tree updating child_array */
			//leds(2,1);
			child_array[numChildren] = payload_nodeID;
			numChildren++;
			proceed Receiving;
		}
		// If this is a totally new unknown node, send connection response.
		else {
			call request_response(Receiving);
		}
}

fsm Broadcast {
	address packet;
	struct msg * payload;

	state Init:
		// ser_outf(Sending, "INSIDE SENDING NOW, POWER = %x\n\r", power);
		payload = msg_init(-1, nodeID, 1, 0, power);

	state Send:
		packet = tcv_wnp(Send, sfd, 12);
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

fsm Listen {
	address packet;
	word p1, tr;
	byte RSSI, LQI;
	int count = 0;

	// Wait to receive a response from a node in the tree
	state Wait_Connection:
		// At end of 1.5 seconds, check if the node received a connection
		ser_outf(Wait_Connection, "ARE WE AT LEAST WAITING FOR A CONNECTION?\n\r");
		// RSSI is checked by potential parents
		packet = tcv_rnp(Wait_Connection, sfd);

	// Get information from received packet
	state Measuring:
		ser_outf(Measuring, "RECEIVED A MESSAGE, MEASURING\n\r");
		p1 = (tcv_left(packet))>>1;
		tr = packet[p1-1];
		RSSI = (byte) (tr>>8);
		LQI = (byte) tr;

	// Compare the responding nodes and only save the best one
	state Update:
		struct msg* payload = (struct msg*)(packet + 1);
		ser_outf(Update, "RECEIVED MESSAGE: NODEID: %x   DESTID: %x   SOURCEID: %x\n\rCONNECT: %x   HOPCOUNT: %x   POWER: %x\n\r", payload->nodeID, payload->destID, payload->sourceID, payload->connect, payload->hopCount, payload->powerLVL);
		//ser_outf(Update, "RECEIVED RESPONSE, #RESPONSES = %d\n\rINT PAYLOAD CONNECT: %x\n\r", count, payload->connect);
		if (payload->connect == (byte)1) {
			// Checks for multiple responses
			count ++;
			if(count > 1){
				leds_all(0);
				//leds(2, 1);
				// If the newly received response is further away than a previous response
				if((payload->hopCount + 1) > hopCount){
					proceed Wait_Connection;
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
}

/* FSM to allow console commands from the sink */
fsm sink_interface {
	char option;
	struct msg * node;
	struct msg * payload;
	address packet;

	state ASK_SER:
		ser_outf(ASK_SER, "Enter (t) for LED Toggle or (h) for network diagnostic:\n\r");
	
	state WAIT_SER:
		ser_inf(WAIT_SER, "%c", &option);

		/* Will send a message to each of the sink's children to toggle yellow LED and rebroadcast the message */
		if(option == 't') {
			int i;
			for (i = 0; i < numChildren; i++) {
				payload = msg_init(child_array[i], 0, 0, hopCount, power);
				packet = tcv_wnp(WAIT_SER, sfd, 12);
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
			proceed ASK_SER;
		}
		/* Otherwise we print out the info for each child node via LOOP_STATE */
		if(option =='h') {
			ser_outf(WAIT_SER, "Number of nodes in the network: %d\n\r", numNode);
			proceed LOOP_STATE;
		}
	state LOOP_STATE:
		int i;
		for(i = 0; i < numNode; i++){
			node = network_nodes[i];	
			ser_outf(LOOP_STATE, "Node ID: %x, Power Level: %x, Hop Count: %x\n\rParent Node:, Parent Signal Strength: n/a\n\r", node->nodeID, node->powerLVL, node->hopCount);
		}
		proceed ASK_SER;
}

// Main fsmmak
fsm root {
	struct msg * payload;
	address packet;
	word ReadPower;
	int count = 0;
	int pid;
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
		//tcv_control (sfd, PHYSOPT_GETPOWER, &ReadPower);		
		leds(1, 1);

	/* Ask if this node will be the sink */
	state ASK_ser:
		ser_outf(ASK_ser, "Would you like to make this the sink node? (y/n)\n\r");
		delay(30000, Sending);

	/* Wait for response */
	state WAIT_ser:
		ser_inf(WAIT_ser, "%c", &c);

		/* Sets the sink nodeID to be 0. */
		if(c == 'y') {
			nodeID = 0;
			parentID = 0;
			hopCount = 0;
			leds_all(0);
			leds(0,1);
			runfsm receive;
			call sink_interface(End);
			// Run the receive fsm and don't try to connect to the tree
		}
		else {
			proceed Sending;
		}

	// Sends a request to join the tree if not sink node
	state Sending:
		call Broadcast(CallListen);

	// Begin listening for nodes
	state CallListen:
		pid = runfsm Listen;
		
	state SetDelay:
		delay(1500, Check_Connections);
		release;

	state Check_Connections:
		ser_outf(Check_Connections, "CHECK STATE\n\r");
		kill(pid);
		// If connection end
		if(RSSI_C >= (byte)Min_RSSI){
			proceed Prep_Message;
		}
		//else power up
		proceed Power_Up;

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
		ser_outf(Prep_Message, "ARE WE MAKING IT HERE EVER?\n\r");
		payload = msg_init(destID, nodeID, 1, hopCount, power);

	// Informs parent that it now has a child
	state Connected:
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
		leds_all(0);
		leds(2,1);
		runfsm parent_send;

	// If root finishes the whole program stops. Keep root running.
	state End:
		call receive(End);
}
